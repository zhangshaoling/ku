#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct FileResolver {
    std::filesystem::path root;
};

bool resolve_import(void* user_data, std::string_view module_path, std::string* source,
                    std::string* error) {
    const auto& root = static_cast<FileResolver*>(user_data)->root;
    std::filesystem::path logical{std::string(module_path)};
    if (logical.empty() || logical.is_absolute()) {
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
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: dao-ku <input.ku> <output.dao>\n";
        return EXIT_FAILURE;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open input: " << argv[1] << '\n';
        return EXIT_FAILURE;
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    dao::ModuleBuilder builder;
    dao_error error{};
    std::filesystem::path module_root = std::filesystem::absolute(argv[1]).parent_path();
    if (module_root.filename() == "std")
        module_root = module_root.parent_path();
    FileResolver resolver{std::filesystem::weakly_canonical(module_root)};
    dao::km::Options options{};
    options.import_resolver = resolve_import;
    options.import_user_data = &resolver;
    if (!dao::km::compile(source, builder, &error, options)) {
        std::cerr << "compile failed: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    std::vector<uint8_t> bytes;
    try {
        bytes = builder.encode();
    } catch (const std::exception& exception) {
        std::cerr << "encode failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    dao_module* module = nullptr;
    const dao_status status = dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error);
    if (status != DAO_OK) {
        std::cerr << "verification failed: " << error.message << '\n';
        dao_vm_destroy(vm);
        return EXIT_FAILURE;
    }
    dao_module_release(module);
    dao_vm_destroy(vm);
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))) {
        std::cerr << "cannot write output: " << argv[2] << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "wrote " << bytes.size() << " bytes to " << argv[2] << '\n';
    return EXIT_SUCCESS;
}
