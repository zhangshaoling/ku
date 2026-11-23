#include "dao/source_host.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#ifndef DAO_CONFIG_MODULE
#error "DAO_CONFIG_MODULE must name the compiled configuration validator"
#endif

namespace {
bool read(const std::filesystem::path& path, std::string* contents) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    contents->assign(std::istreambuf_iterator<char>(input), {});
    return !input.bad();
}

dao_value field(dao_vm* vm, const dao_value& map, std::string_view key) {
    dao_value value{};
    dao_value_map_get(vm, &map, {reinterpret_cast<const uint8_t*>(key.data()), key.size()}, &value);
    return value;
}

std::string_view text(const dao_value& value) {
    dao_bytes bytes{};
    if (dao_value_get_view(&value, &bytes) != DAO_OK) return {};
    return {reinterpret_cast<const char*>(bytes.data), bytes.size};
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "usage: ku-config <config-file>\n"
                     "Required: name, port (1..65535). Optional: workers (1..256).\n";
        return 0;
    }
    if (argc != 2 || std::string_view(argv[1]).starts_with("--")) {
        std::cerr << "usage: ku-config <config-file>\n";
        return 2;
    }
    std::string source;
    if (!read(argv[1], &source)) {
        std::cerr << "cannot read configuration: " << argv[1] << '\n';
        return 2;
    }
    dao_value argument{};
    if (dao_value_make_string_view(
            {reinterpret_cast<const uint8_t*>(source.data()), source.size()}, &argument) != DAO_OK) {
        std::cerr << argv[1] << ": configuration must be UTF-8\n";
        return 2;
    }
    const auto installed = std::filesystem::absolute(argv[0]).parent_path() /
                           ".." / "share" / "dao" / "examples" / "config_validator.dao";
    const auto module_path = std::filesystem::exists(installed)
                                 ? installed : std::filesystem::path(DAO_CONFIG_MODULE);
    std::string bytes;
    if (!read(module_path, &bytes)) {
        std::cerr << "cannot read configuration validator module\n";
        return 2;
    }
    std::unique_ptr<dao_vm, decltype(&dao_vm_destroy)> vm(dao_vm_create(nullptr), dao_vm_destroy);
    if (!vm || dao_vm_set_features(vm.get(), DAO_FEATURE_RECORDS) != DAO_OK ||
        dao::source_host::register_functions(vm.get()) != DAO_OK) {
        std::cerr << "cannot create configuration VM\n";
        return 2;
    }
    dao_error error{};
    dao_module* raw_module = nullptr;
    dao_status status = dao_vm_load_module(
        vm.get(), {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, &raw_module, &error);
    std::unique_ptr<dao_module, decltype(&dao_module_release)> module(raw_module, dao_module_release);
    dao_function function = 0;
    if (status == DAO_OK)
        status = dao_module_find_export(module.get(), dao::source_host::symbol("校验"), &function);
    dao_value result{};
    if (status == DAO_OK)
        status = dao_vm_call(vm.get(), module.get(), function, &argument, 1, &result, &error);
    const dao_value ok = field(vm.get(), result, "ok");
    if (status != DAO_OK || ok.type != DAO_VALUE_TRIT) {
        std::cerr << "configuration validator failed: " << dao_status_name(status)
                  << ' ' << error.message << '\n';
        return 2;
    }
    if (ok.payload == 1) {
        std::cout << "valid " << argv[1] << '\n';
        return 0;
    }
    const dao_value errors = field(vm.get(), result, "errors");
    size_t count = 0;
    if (dao_value_list_size(vm.get(), &errors, &count) != DAO_OK || count == 0) {
        std::cerr << "configuration validator returned invalid errors\n";
        return 2;
    }
    for (size_t index = 0; index < count; ++index) {
        dao_value item{};
        dao_value_list_get(vm.get(), &errors, index, &item);
        const dao_value line = field(vm.get(), item, "line");
        const std::string_view key = text(field(vm.get(), item, "key"));
        std::cerr << argv[1];
        if (line.payload > 0) std::cerr << ':' << line.payload;
        std::cerr << ": error: " << text(field(vm.get(), item, "message"));
        if (!key.empty()) std::cerr << " [" << key << ']';
        std::cerr << '\n';
    }
    return 1;
}
