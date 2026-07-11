#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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
    if (!dao::km::compile(source, builder, &error)) {
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
