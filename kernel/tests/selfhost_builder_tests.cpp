#include "dao/selfhost.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

uint32_t symbol(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

const std::string imports =
    "import builder_new(0) import builder_begin(3) import builder_emit_raw(6) "
    "import builder_import(3) "
    "import builder_identity(5) import builder_module_import(7) "
    "import builder_patch(3) import builder_patch_marked(5) import builder_finish(3) "
    "import builder_encode(1) ";

std::string compiler(std::string_view body) {
    return imports + "thought compile(source) { " + std::string(body) + " }";
}

} // namespace

int main() {
    const uint32_t main_symbol = symbol("main");
    const std::string valid_build =
        "builder_begin(h, 0, 4096); builder_emit_raw(h, 1, 0, 0, 0, 42); "
        "builder_emit_raw(h, 15, 0, 0, 0, 0); builder_finish(h, " +
        std::to_string(main_symbol) + ", 1)";
    const std::string valid_tail = valid_build + "; builder_encode(h)";

    struct Rejected {
        const char* name;
        std::string body;
        dao_status status;
    };
    const std::vector<Rejected> rejected = {
        {"emit before begin", "h = builder_new(); builder_emit_raw(h, 1, 0, 0, 0, 0); " + valid_tail,
         DAO_INVALID_ARGUMENT},
        {"parameters exceed registers",
         "h = builder_new(); builder_begin(h, 2, 4096); builder_emit_raw(h, 15, 0, 0, 0, 0); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"zero register count",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 15, 0, 0, 0, 0); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 0); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"register count exceeds limit",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 15, 0, 0, 0, 0); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 4097); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"nested begin",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_begin(h, 0, 4096); "
         "builder_emit_raw(h, 15, 0, 0, 0, 0); builder_finish(h, " +
             std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"unknown opcode",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 42, 0, 0, 0, 0); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"truncated register",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 1, 4096, 0, 0, 0); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"patch non-control instruction",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 1, 0, 0, 0, 0); "
         "builder_patch(h, 0, 0); builder_emit_raw(h, 15, 0, 0, 0, 0); builder_finish(h, " +
             std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"invalid marked patch placeholder",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 13, 0, 0, 0, -3); "
         "builder_patch_marked(h, 0, 1, -3, 1); builder_emit_raw(h, 15, 0, 0, 0, 0); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"unresolved jump",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 13, 0, 0, 0, -1); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"encode active function", "h = builder_new(); builder_begin(h, 0, 4096); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"finish empty function",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_finish(h, " +
             std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"invalid import symbol", "h = builder_new(); builder_import(h, -1, 0); " + valid_tail,
         DAO_INVALID_ARGUMENT},
        {"import arity exceeds registers",
         "h = builder_new(); builder_import(h, 1, 4097); " + valid_tail,
         DAO_INVALID_ARGUMENT},
        {"module import requires identity",
         "h = builder_new(); builder_module_import(h, \"ku:test/provider\", 1, 0, 0, 1, 0); " +
             valid_tail,
         DAO_INVALID_ARGUMENT},
        {"identity version out of range",
         "h = builder_new(); builder_identity(h, \"ku:test/builder\", 4294967296, 0, 0); " +
             valid_tail,
         DAO_INVALID_ARGUMENT},
        {"duplicate identity",
         "h = builder_new(); builder_identity(h, \"ku:test/builder\", 1, 2, 3); "
         "builder_identity(h, \"ku:test/builder\", 1, 2, 3); " + valid_tail,
         DAO_INVALID_ARGUMENT},
        {"invalid export symbol",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 15, 0, 0, 0, 0); "
         "builder_finish(h, -1, 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"function without return",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 1, 0, 0, 0, 42); "
         "builder_finish(h, " + std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_INVALID_ARGUMENT},
        {"mutate encoded builder",
         "h = builder_new(); bytes = " + valid_tail + "; builder_begin(h, 0, 1); bytes",
         DAO_INVALID_ARGUMENT},
        {"verifier rejects operand",
         "h = builder_new(); builder_begin(h, 0, 4096); builder_emit_raw(h, 2, 0, 2, 0, 0); "
         "builder_emit_raw(h, 15, 0, 0, 0, 0); builder_finish(h, " +
             std::to_string(main_symbol) + ", 1); builder_encode(h)",
         DAO_VERIFY_ERROR},
    };

    for (const Rejected& test : rejected) {
        std::vector<uint8_t> output;
        dao_error error{};
        if (dao::selfhost::compile(compiler(test.body), "probe", &output, &error) ||
            error.code != test.status) {
            std::cerr << test.name << ": expected " << dao_status_name(test.status) << ", got "
                      << dao_status_name(error.code) << '\n';
            return EXIT_FAILURE;
        }
    }

    std::vector<uint8_t> output;
    dao_error error{};
    const std::string repeated_encode =
        "h = builder_new(); builder_identity(h, \"ku:test/builder\", 1, 2, 3); " + valid_build +
                                         "; first = builder_encode(h); ignored = builder_encode(h); first";
    if (!dao::selfhost::compile(compiler(repeated_encode), "probe", &output, &error)) {
        std::cerr << "valid builder rejected: " << error.message << '\n';
        return EXIT_FAILURE;
    }
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    dao_module* module = nullptr;
    if (vm == nullptr ||
        dao_vm_load_module(vm, {output.data(), output.size()}, &module, &error) != DAO_OK) {
        std::cerr << "valid module load failed: " << error.message << '\n';
        if (vm != nullptr)
            dao_vm_destroy(vm);
        return EXIT_FAILURE;
    }
    dao_function main_function = 0;
    dao_module_identity identity{};
    identity.struct_size = sizeof(identity);
    dao_value result{};
    const bool valid = dao_module_find_export(module, main_symbol, &main_function) == DAO_OK &&
                       dao_module_get_identity(module, &identity) == DAO_OK &&
                       std::string_view(reinterpret_cast<const char*>(identity.name.data),
                                        identity.name.size) == "ku:test/builder" &&
                       identity.version_major == 1 && identity.version_minor == 2 &&
                       identity.version_patch == 3 &&
                       dao_vm_call(vm, module, main_function, nullptr, 0, &result, &error) == DAO_OK &&
                       result.type == DAO_VALUE_I64 && result.payload == 42;
    dao_module_release(module);
    dao_vm_destroy(vm);
    if (!valid) {
        std::cerr << "valid builder result mismatch\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
