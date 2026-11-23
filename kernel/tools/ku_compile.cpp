#include "dao/selfhost.hpp"
#include "dao/ku_migration.hpp"
#include "dao/module_store.hpp"
#include "dao/source_host.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#ifndef DAO_SELFHOST_COMPILER_SOURCE
#error "DAO_SELFHOST_COMPILER_SOURCE must name kernel/selfhost/compiler.ku"
#endif

namespace {
struct FileResolver {
    std::filesystem::path root;
};

// Columns count Unicode scalars; the excerpt expands tabs and common wide characters.
void print_compile_error(const char* prefix, const dao_error& error,
                         const dao::CompilerDiagnostic& diagnostic,
                         const FileResolver& resolver, std::string_view input_name) {
    if (diagnostic.offset == std::string::npos) {
        std::cerr << prefix << error.message << '\n';
        return;
    }
    std::filesystem::path name = diagnostic.source_name;
    if (diagnostic.source_name != input_name) {
        if (!name.has_extension()) name += ".ku";
        name = std::filesystem::weakly_canonical(resolver.root / name);
    }
    const std::string_view source = diagnostic.source;
    size_t line = 1;
    size_t start = 0;
    for (size_t index = 0; index < diagnostic.offset; ++index) {
        if (source[index] == '\n') { ++line; start = index + 1; }
    }
    size_t end = source.find('\n', start);
    if (end == std::string_view::npos) end = source.size();
    if (end > start && source[end - 1] == '\r') --end;
    size_t column = 1;
    size_t width = 0;
    size_t caret = 0;
    std::string excerpt;
    for (size_t index = start; index < end;) {
        const size_t begin = index;
        const auto first = static_cast<unsigned char>(source[index++]);
        uint32_t codepoint = first;
        if (first >= 0xc0) {
            const size_t length = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
            codepoint = first & (0x7f >> length);
            for (size_t byte = 1; byte < length && index < end; ++byte)
                codepoint = (codepoint << 6) | (static_cast<unsigned char>(source[index++]) & 0x3f);
        }
        if (begin < diagnostic.offset) ++column;
        size_t cells = 1;
        if (first == '\t') {
            cells = 4 - width % 4;
            excerpt.append(cells, ' ');
        } else {
            excerpt.append(source.substr(begin, index - begin));
            if ((codepoint >= 0x1100 && codepoint <= 0x115f) ||
                (codepoint >= 0x2e80 && codepoint <= 0xa4cf) ||
                (codepoint >= 0xac00 && codepoint <= 0xd7a3) ||
                (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
                (codepoint >= 0xfe10 && codepoint <= 0xfe6f) ||
                (codepoint >= 0xff01 && codepoint <= 0xff60) ||
                (codepoint >= 0x1f300 && codepoint <= 0x1faff) ||
                (codepoint >= 0x20000 && codepoint <= 0x3ffff)) cells = 2;
            if ((codepoint >= 0x0300 && codepoint <= 0x036f) ||
                (codepoint >= 0xfe00 && codepoint <= 0xfe0f) || codepoint == 0x200d) cells = 0;
        }
        width += cells;
        if (begin < diagnostic.offset) caret = width;
    }
    const std::string label = std::to_string(line);
    std::cerr << name.string() << ':' << line << ':' << column << ": error: "
              << prefix << error.message << '\n'
              << label << " | " << excerpt << '\n'
              << std::string(label.size(), ' ') << " | " << std::string(caret, ' ') << "^\n";
}

bool parse_version(std::string_view text, dao::SemanticVersion* version) {
    const size_t first = text.find('.');
    const size_t second = first == std::string_view::npos
                              ? std::string_view::npos
                              : text.find('.', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos)
        return false;
    const auto parse = [](std::string_view part, uint32_t* value) {
        if (part.empty())
            return false;
        const auto result = std::from_chars(part.data(), part.data() + part.size(), *value);
        return result.ec == std::errc{} && result.ptr == part.data() + part.size();
    };
    return parse(text.substr(0, first), &version->major) &&
           parse(text.substr(first + 1, second - first - 1), &version->minor) &&
           parse(text.substr(second + 1), &version->patch);
}

bool resolve_import(void* user_data, std::string_view module_path, std::string* source,
                    std::string* error) {
    const auto& root = static_cast<FileResolver*>(user_data)->root;
    std::filesystem::path logical{std::string(module_path)};
    if (logical.empty() || module_path.find('\0') != std::string_view::npos ||
        logical.is_absolute() || logical.has_root_name() || logical.has_root_directory()) {
        *error = "module path must be relative";
        return false;
    }
    for (const auto& component : logical) {
        if (component == "..") {
            *error = "module path must not contain '..'";
            return false;
        }
    }
    if (!logical.has_extension())
        logical += ".ku";
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(root / logical);
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) {
        *error = "module path escapes the module root";
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            *error = "module path escapes the module root";
            return false;
        }
    }
    std::ifstream input(candidate, std::ios::binary);
    if (!input) {
        *error = "cannot open " + candidate.string();
        return false;
    }
    source->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

std::string format_source(std::string_view source) {
    std::string output;
    int indent = 0;
    bool line_start = true;
    bool in_string = false;
    bool in_line_comment = false;
    bool escaped = false;
    bool pending_space = false;
    std::vector<bool> block_braces;
    char previous = 0;
    auto begin_line = [&]() {
        if (line_start) {
            output.append(static_cast<size_t>(indent) * 2, ' ');
            line_start = false;
        }
    };
    auto newline = [&]() {
        while (!output.empty() && output.back() == ' ') output.pop_back();
        if (output.empty() || output.back() != '\n') output.push_back('\n');
        line_start = true;
        pending_space = false;
    };
    for (size_t i = 0; i < source.size(); ++i) {
        const char ch = source[i];
        const char next = i + 1 < source.size() ? source[i + 1] : 0;
        if (in_line_comment) {
            begin_line(); output.push_back(ch);
            if (ch == '\n') { in_line_comment = false; line_start = true; }
            continue;
        }
        if (in_string) {
            begin_line(); output.push_back(ch);
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') in_string = false;
            previous = ch;
            continue;
        }
        if (ch == '/' && next == '/') {
            begin_line(); if (pending_space && !output.empty() && output.back() != ' ') output.push_back(' ');
            output += "//"; ++i; in_line_comment = true; pending_space = false; continue;
        }
        if (ch == ';' && next == ';') {
            begin_line(); if (pending_space && !output.empty() && output.back() != ' ') output.push_back(' ');
            output += ";;"; ++i; in_line_comment = true; pending_space = false; continue;
        }
        if (ch == '"') { begin_line(); if (pending_space && !output.empty() && output.back() != ' ') output.push_back(' '); output.push_back(ch); in_string = true; pending_space = false; continue; }
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' || ch == '=' ||
            ch == '!' || ch == '<' || ch == '>') {
            size_t length = 1;
            if (i + 1 < source.size() && source[i + 1] == '=') length = 2;
            const std::string_view op = source.substr(i, length);
            const bool unary = (ch == '+' || ch == '-') &&
                (previous == 0 || previous == '(' || previous == '[' || previous == '{' ||
                 previous == ',' || previous == ':' || previous == '=' || previous == '\n' ||
                 (!output.empty() && output.back() == '\n'));
            while (!output.empty() && output.back() == ' ') output.pop_back();
            begin_line();
            if (!unary && !output.empty() && output.back() != '\n') output.push_back(' ');
            output.append(op);
            if (!unary) output.push_back(' ');
            i += length - 1;
            pending_space = false;
            previous = op.back();
            continue;
        }
        if (ch == '\r' || ch == '\t' || ch == ' ') { pending_space = true; continue; }
        if (ch == '\n' || ch == ';') { newline(); previous = ch; continue; }
        if (ch == '{') {
            begin_line(); if (pending_space && !output.empty() && output.back() != ' ') output.push_back(' ');
            output.push_back('{'); pending_space = false;
            const size_t line_begin = output.find_last_of('\n');
            const std::string line = output.substr(line_begin == std::string::npos ? 0 : line_begin + 1);
            const size_t first_text = line.find_first_not_of(' ');
            const std::string_view code_line = first_text == std::string::npos
                ? std::string_view{}
                : std::string_view(line).substr(first_text);
            const bool block = previous == ')' || code_line.starts_with("thought ") || code_line.starts_with("思 ") ||
                               code_line.starts_with("型 ") || code_line.starts_with("func ") || code_line.starts_with("if ") || code_line.starts_with("若 ") ||
                               code_line.starts_with("while ") || code_line.starts_with("当 ") || code_line.starts_with("for ") ||
                               code_line.starts_with("遍 ") || code_line.starts_with("try ") || code_line.starts_with("试 ") ||
                               code_line.starts_with("catch ") || code_line.starts_with("捕 ") || code_line.starts_with("else ") ||
                               code_line.starts_with("否 ");
            block_braces.push_back(block);
            if (block) { ++indent; newline(); }
            continue;
        }
        if (ch == '}') {
            const bool block = !block_braces.empty() && block_braces.back();
            if (!block_braces.empty()) block_braces.pop_back();
            if (block && indent > 0) --indent;
            if (block && !line_start && previous != '{') newline();
            begin_line(); output.push_back('}'); pending_space = false;
            size_t lookahead = i + 1;
            while (lookahead < source.size() &&
                   (source[lookahead] == ' ' || source[lookahead] == '\t' || source[lookahead] == '\r' ||
                    source[lookahead] == '\n')) ++lookahead;
            const std::string_view continuation = source.substr(lookahead);
            const bool joins_clause = continuation.starts_with("catch") || continuation.starts_with("捕") ||
                                      continuation.starts_with("else") || continuation.starts_with("否");
            if (block && joins_clause) output.push_back(' ');
            else if (block && next != ';' && next != ',' && next != ']' && next != ')') newline();
            previous = ch; continue;
        }
        begin_line();
        if (pending_space && !output.empty() && output.back() != ' ' && output.back() != '\n' && ch != ',' && ch != ')' && ch != ']' && ch != '.') output.push_back(' ');
        if ((ch == ',' || ch == ':' || ch == ')' || ch == ']' || ch == '.') && !output.empty() && output.back() == ' ') output.pop_back();
        output.push_back(ch);
        pending_space = ch == ',';
        previous = ch;
    }
    while (!output.empty() && (output.back() == ' ' || output.back() == '\n')) output.pop_back();
    output.push_back('\n');
    return output;
}

bool read_compiler_source(const char* executable, std::string* source) {
    const std::filesystem::path candidates[] = {
        std::filesystem::absolute(executable).parent_path() / ".." / "share" / "dao" /
            "selfhost" / "compiler.ku",
        std::filesystem::path(DAO_SELFHOST_COMPILER_SOURCE),
    };
    for (const auto& candidate : candidates) {
        std::ifstream input(candidate, std::ios::binary);
        if (!input)
            continue;
        source->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }
    return false;
}

int run_tests(std::string_view source, const std::vector<uint8_t>& bytes) {
    std::vector<std::string> names;
    dao_error error{};
    if (!dao::km::declared_function_names(source, names, &error)) {
        std::cerr << "test discovery failed: " << error.message << '\n';
        return 2;
    }
    size_t passed = 0;
    size_t failed = 0;
    for (const std::string& name : names) {
        if (!name.starts_with("测试_") && !name.starts_with("test_")) continue;
        uint32_t symbol = 2166136261u;
        for (const unsigned char byte : name) symbol = (symbol ^ byte) * 16777619u;

        const dao_vm_config config = dao_vm_config_default();
        std::unique_ptr<dao_vm, decltype(&dao_vm_destroy)> vm(dao_vm_create(&config), dao_vm_destroy);
        if (!vm) {
            std::cerr << "cannot create test VM\n";
            return 2;
        }
        dao_module* raw_module = nullptr;
        error = {};
        dao_status status = dao::source_host::register_functions(vm.get());
        if (status == DAO_OK && bytes.size() >= 8 && bytes[4] == 3 && bytes[5] == 0 && bytes[6] == 11 && bytes[7] == 0)
            status = dao_vm_set_features(vm.get(), DAO_FEATURE_RECORDS);
        if (status == DAO_OK)
            status = dao_vm_load_module(vm.get(), {bytes.data(), bytes.size()}, &raw_module, &error);
        std::unique_ptr<dao_module, decltype(&dao_module_release)> module(raw_module, dao_module_release);
        dao_function function = 0;
        if (status == DAO_OK) status = dao_module_find_export(module.get(), symbol, &function);
        dao_value result{};
        if (status == DAO_OK)
            status = dao_vm_call(vm.get(), module.get(), function, nullptr, 0, &result, &error);

        if (status == DAO_OK && result.type == DAO_VALUE_TRIT && result.payload == 1) {
            ++passed;
            std::cout << "[PASS] " << name << '\n';
            continue;
        }
        ++failed;
        std::cout << "[FAIL] " << name << ": ";
        if (status != DAO_OK) {
            std::cout << dao_status_name(status);
            if (error.message[0] != '\0') std::cout << ": " << error.message;
        } else if (result.type != DAO_VALUE_TRIT) {
            std::cout << "expected 真 (Trit +1), received a non-Trit result";
        } else {
            std::cout << "expected 真 (Trit +1), received Trit " << result.payload;
        }
        std::cout << '\n';
    }
    if (passed + failed == 0) {
        std::cerr << "no tests found; use 测试_ or test_ function names\n";
        return 2;
    }
    std::cout << "tests: " << passed << " passed, " << failed << " failed, "
              << passed + failed << " total\n";
    return failed == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    bool recovery = false;
    bool identified = false;
    bool check_mode = false;
    bool fmt_mode = false;
    bool test_mode = false;
    bool store_mode = false;
    bool enable_records = false;
    std::filesystem::path store_root;
    bool has_parent = false;
    std::string parent_identity;
    dao::SemanticVersion parent_version{};
    std::string identity_name;
    dao::SemanticVersion identity_version{};
    int input_index = 1;
    while (input_index < argc && std::string_view(argv[input_index]).starts_with("--")) {
        const std::string_view option = argv[input_index++];
        if (option == "--test" && !test_mode && !check_mode && !fmt_mode) {
            test_mode = true;
            continue;
        }
        if (option == "--check" && !check_mode && !fmt_mode && !test_mode) {
            check_mode = true;
            continue;
        }
        if (option == "--fmt" && !fmt_mode && !check_mode && !test_mode) {
            fmt_mode = true;
            continue;
        }
        if (option == "--recovery" && !recovery) {
            recovery = true;
            continue;
        }
        if (option == "--enable-records" && !enable_records) {
            enable_records = true;
            continue;
        }
        if (option == "--store" && !store_mode && input_index < argc) {
            store_root = argv[input_index++];
            store_mode = !store_root.empty();
            if (store_mode) continue;
        }
        if (option == "--parent" && !has_parent && input_index + 1 < argc) {
            parent_identity = argv[input_index++];
            has_parent = !parent_identity.empty() && parse_version(argv[input_index++], &parent_version);
            if (has_parent) continue;
        }
        if (option == "--identity" && !identified && input_index + 1 < argc) {
            identity_name = argv[input_index++];
            identified = !identity_name.empty() &&
                         parse_version(argv[input_index++], &identity_version);
            if (identified)
                continue;
        }
        std::cerr << "invalid dao-ku option\n";
        if (test_mode || option == "--test") {
            std::cerr << "usage: dao-ku --test [--recovery] <input.ku>\n";
            return 2;
        }
        if (fmt_mode || option == "--fmt")
            std::cerr << "usage: dao-ku --fmt [--recovery] <input.ku>\n";
        return EXIT_FAILURE;
    }
    const int error_exit = test_mode ? 2 : EXIT_FAILURE;
    if (enable_records && !store_mode) {
        std::cerr << "--enable-records requires --store\n";
        return error_exit;
    }
    std::string_view source_mode = "--check";
    if (fmt_mode) source_mode = "--fmt";
    if (test_mode) source_mode = "--test";
    const int output_index = input_index + 1;
    if (check_mode || fmt_mode || test_mode) {
        if (input_index + 1 != argc) {
            std::cerr << "usage: dao-ku " << source_mode
                      << " [--recovery] <input.ku>\n";
            return error_exit;
        }
    } else if (store_mode) {
        if (!identified || input_index + 1 != argc) {
            std::cerr << "usage: dao-ku [--enable-records] --store DIR --identity NAME MAJOR.MINOR.PATCH <input.ku>\n";
            return EXIT_FAILURE;
        }
    } else if (input_index + 2 != argc) {
        std::cerr << "usage: dao-ku [--recovery] [--identity NAME MAJOR.MINOR.PATCH] "
                     "<input.ku> <output.dao>\n";
        return EXIT_FAILURE;
    }
    if ((check_mode || fmt_mode || test_mode) && (store_mode || identified || has_parent)) {
        std::cerr << source_mode
                  << " cannot be combined with --store, --parent, or --identity\n";
        std::cerr << "usage: dao-ku " << source_mode
                  << " [--recovery] <input.ku>\n";
        return error_exit;
    }
    std::ifstream input(argv[input_index], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open input: " << argv[input_index] << '\n';
        return error_exit;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    dao_error error{};
    std::filesystem::path module_root = std::filesystem::absolute(argv[input_index]).parent_path();
    if (module_root.filename() == "std")
        module_root = module_root.parent_path();
    FileResolver resolver{std::filesystem::weakly_canonical(module_root)};
    dao::CompilerDiagnostic diagnostic;
    std::vector<uint8_t> bytes;
    std::string compiler_source;
    if (recovery) {
        dao::ModuleBuilder builder;
        if (identified)
            builder.set_identity(identity_name, identity_version);
        dao::km::Options options{};
        options.import_resolver = resolve_import;
        options.import_user_data = &resolver;
        options.source_name = argv[input_index];
        options.diagnostic = &diagnostic;
        if (!dao::km::compile(source, builder, &error, options)) {
            print_compile_error("recovery compile failed: ", error, diagnostic, resolver,
                                argv[input_index]);
            return error_exit;
        }
        try {
            bytes = builder.encode();
        } catch (const std::exception& exception) {
            std::cerr << "recovery encode failed: " << exception.what() << '\n';
            return error_exit;
        }
    } else {
        if (!read_compiler_source(argv[0], &compiler_source)) {
            std::cerr << "cannot locate self-hosted compiler.ku\n";
            return error_exit;
        }
        dao::selfhost::Options options{};
        options.module_resolver = resolve_import;
        options.module_user_data = &resolver;
        options.has_identity = identified;
        options.identity_name = identity_name;
        options.identity_version = identity_version;
        options.source_name = argv[input_index];
        options.diagnostic = &diagnostic;
        if (!dao::selfhost::compile(compiler_source, source, &bytes, &error, options)) {
            print_compile_error("compile failed: ", error, diagnostic, resolver, argv[input_index]);
            return error_exit;
        }
    }
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    if (vm == nullptr) {
        std::cerr << "cannot create verifier VM\n";
        return error_exit;
    }
    dao_module* module = nullptr;
    if (bytes.size() >= 8 && bytes[4] == 3 && bytes[5] == 0 && bytes[6] == 11 && bytes[7] == 0)
        dao_vm_set_features(vm, DAO_FEATURE_RECORDS);
    const dao_status status = dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error);
    if (status != DAO_OK) {
        std::cerr << "verification failed: " << error.message << '\n';
        dao_vm_destroy(vm);
        return error_exit;
    }
    dao_module_release(module);
    dao_vm_destroy(vm);
    if (test_mode) return run_tests(source, bytes);
    if (check_mode) {
        std::cout << "checked " << argv[input_index] << "\n";
        return EXIT_SUCCESS;
    }
    if (fmt_mode) {
        std::cout << format_source(source);
        return EXIT_SUCCESS;
    }
    if (store_mode) {
        dao::store::ModuleStore store(store_root, enable_records ? DAO_FEATURE_RECORDS : 0);
        if (has_parent && recovery) {
            std::cerr << "--parent requires the self-hosted compiler path\n";
            return EXIT_FAILURE;
        }
        const dao_status store_status = has_parent
            ? store.save_derived_source(compiler_source, source, identity_name, identity_version,
                                        parent_identity, parent_version, &error)
            : store.save(bytes, source, &error);
        if (store_status != DAO_OK) {
            std::cerr << "cannot store module: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        std::cout << "stored " << bytes.size() << " bytes in " << store_root << '\n';
        return EXIT_SUCCESS;
    }
    std::ofstream output(argv[output_index], std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))) {
        std::cerr << "cannot write output: " << argv[output_index] << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "wrote " << bytes.size() << " bytes to " << argv[output_index] << '\n';
    return EXIT_SUCCESS;
}
