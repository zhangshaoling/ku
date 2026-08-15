#include "dao/selfhost.hpp"
#include "dao/ku_migration.hpp"
#include "dao/module_store.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef DAO_SELFHOST_COMPILER_SOURCE
#error "DAO_SELFHOST_COMPILER_SOURCE must name kernel/selfhost/compiler.ku"
#endif

namespace {
struct FileResolver {
    std::filesystem::path root;
};

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

bool read_compiler_source(const char* executable, std::string* source) {
    const std::filesystem::path candidates[] = {
        std::filesystem::path(DAO_SELFHOST_COMPILER_SOURCE),
        std::filesystem::absolute(executable).parent_path() / ".." / "share" / "dao" /
            "selfhost" / "compiler.ku",
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
} // namespace

int main(int argc, char** argv) {
    bool recovery = false;
    bool identified = false;
    bool check_mode = false;
    bool store_mode = false;
    std::filesystem::path store_root;
    bool has_parent = false;
    std::string parent_identity;
    dao::SemanticVersion parent_version{};
    std::string identity_name;
    dao::SemanticVersion identity_version{};
    int input_index = 1;
    while (input_index < argc && std::string_view(argv[input_index]).starts_with("--")) {
        const std::string_view option = argv[input_index++];
        if (option == "--check" && !check_mode) {
            check_mode = true;
            continue;
        }
        if (option == "--recovery" && !recovery) {
            recovery = true;
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
        return EXIT_FAILURE;
    }
    const int output_index = input_index + 1;
    if (check_mode) {
        if (input_index + 1 != argc) {
            std::cerr << "usage: dao-ku --check [--recovery] <input.ku>\n";
            return EXIT_FAILURE;
        }
    } else if (store_mode) {
        if (!identified || input_index + 1 != argc) {
            std::cerr << "usage: dao-ku --store DIR --identity NAME MAJOR.MINOR.PATCH <input.ku>\n";
            return EXIT_FAILURE;
        }
    } else if (input_index + 2 != argc) {
        std::cerr << "usage: dao-ku [--recovery] [--identity NAME MAJOR.MINOR.PATCH] "
                     "<input.ku> <output.dao>\n";
        return EXIT_FAILURE;
    }
    if (check_mode && (store_mode || identified || has_parent)) {
        std::cerr << "--check cannot be combined with --store, --parent, or --identity\n";
        return EXIT_FAILURE;
    }
    std::ifstream input(argv[input_index], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open input: " << argv[input_index] << '\n';
        return EXIT_FAILURE;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    dao_error error{};
    std::filesystem::path module_root = std::filesystem::absolute(argv[input_index]).parent_path();
    if (module_root.filename() == "std")
        module_root = module_root.parent_path();
    FileResolver resolver{std::filesystem::weakly_canonical(module_root)};
    std::vector<uint8_t> bytes;
    std::string compiler_source;
    if (recovery) {
        dao::ModuleBuilder builder;
        if (identified)
            builder.set_identity(identity_name, identity_version);
        dao::km::Options options{};
        options.import_resolver = resolve_import;
        options.import_user_data = &resolver;
        if (!dao::km::compile(source, builder, &error, options)) {
            std::cerr << "recovery compile failed: " << error.message << '\n';
            return EXIT_FAILURE;
        }
        try {
            bytes = builder.encode();
        } catch (const std::exception& exception) {
            std::cerr << "recovery encode failed: " << exception.what() << '\n';
            return EXIT_FAILURE;
        }
    } else {
        if (!read_compiler_source(argv[0], &compiler_source)) {
            std::cerr << "cannot locate self-hosted compiler.ku\n";
            return EXIT_FAILURE;
        }
        dao::selfhost::Options options{};
        options.module_resolver = resolve_import;
        options.module_user_data = &resolver;
        options.has_identity = identified;
        options.identity_name = identity_name;
        options.identity_version = identity_version;
        if (!dao::selfhost::compile(compiler_source, source, &bytes, &error, options)) {
            std::cerr << "compile failed: " << error.message << '\n';
            return EXIT_FAILURE;
        }
    }
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    if (vm == nullptr) {
        std::cerr << "cannot create verifier VM\n";
        return EXIT_FAILURE;
    }
    dao_module* module = nullptr;
    const dao_status status = dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error);
    if (status != DAO_OK) {
        std::cerr << "verification failed: " << error.message << '\n';
        dao_vm_destroy(vm);
        return EXIT_FAILURE;
    }
    dao_module_release(module);
    dao_vm_destroy(vm);
    if (check_mode) {
        std::cout << "checked " << argv[input_index] << "\n";
        return EXIT_SUCCESS;
    }
    if (store_mode) {
        dao::store::ModuleStore store(store_root);
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
