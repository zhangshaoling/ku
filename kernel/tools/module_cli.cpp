#include "dao/module_store.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
uint32_t symbol(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) { hash ^= byte; hash *= 16777619u; }
    return hash;
}
bool version(std::string_view text, dao::SemanticVersion* out) {
    size_t a = text.find('.'), b = text.find('.', a == std::string_view::npos ? a : a + 1);
    if (a == std::string_view::npos || b == std::string_view::npos) return false;
    const auto parse = [](std::string_view part, uint32_t* value) {
        if (part.empty()) return false;
        const auto result = std::from_chars(part.data(), part.data() + part.size(), *value);
        return result.ec == std::errc{} && result.ptr == part.data() + part.size();
    };
    return parse(text.substr(0, a), &out->major) &&
           parse(text.substr(a + 1, b - a - 1), &out->minor) &&
           parse(text.substr(b + 1), &out->patch);
}
}
int main(int argc, char** argv) {
    int first = 1;
    const bool enable_records = argc > first && std::string_view(argv[first]) == "--enable-records";
    if (enable_records) ++first;
    if (argc < first + 4) { std::cerr << "usage: dao-module [--enable-records] <verify|recall|run> STORE ID VERSION [EXPORT]\n"; return EXIT_FAILURE; }
    const std::string command = argv[first];
    dao::SemanticVersion v{};
    if (!version(argv[first + 3], &v)) { std::cerr << "invalid version\n"; return EXIT_FAILURE; }
    const uint64_t features = enable_records ? DAO_FEATURE_RECORDS : 0;
    dao::store::ModuleStore store(argv[first + 1], features);
    dao_error error{};
    std::vector<uint8_t> bytes;
    if (store.load(argv[first + 2], v, &bytes, &error) != DAO_OK) { std::cerr << error.message << '\n'; return EXIT_FAILURE; }
    if (command == "verify") { std::cout << "verified " << bytes.size() << " bytes\n"; return EXIT_SUCCESS; }
    if (argc < first + 5) { std::cerr << "export name required\n"; return EXIT_FAILURE; }
    dao_vm_config config = dao_vm_config_default();
    std::unique_ptr<dao_vm, decltype(&dao_vm_destroy)> vm(dao_vm_create(&config), dao_vm_destroy);
    if (!vm || dao_vm_set_features(vm.get(), features) != DAO_OK) return EXIT_FAILURE;
    dao_module* module = nullptr;
    if (dao_vm_load_module(vm.get(), {bytes.data(), bytes.size()}, &module, &error) != DAO_OK) return EXIT_FAILURE;
    std::unique_ptr<dao_module, decltype(&dao_module_release)> owner(module, dao_module_release);
    dao_function fn{}; if (dao_module_find_export(module, symbol(argv[first + 4]), &fn) != DAO_OK) return EXIT_FAILURE;
    dao_value result{}; if (dao_vm_call(vm.get(), module, fn, nullptr, 0, &result, &error) != DAO_OK) { std::cerr << error.message << '\n'; return EXIT_FAILURE; }
    if (result.type == DAO_VALUE_RECORD || (enable_records &&
        (result.type == DAO_VALUE_LIST || result.type == DAO_VALUE_MAP))) {
        std::cerr << "Record and container results require the C ABI query interface\n";
        return EXIT_FAILURE;
    }
    std::cout << result.payload << '\n'; return EXIT_SUCCESS;
}
