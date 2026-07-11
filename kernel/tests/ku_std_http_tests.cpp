#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << name << '\n';
    }
}

uint32_t symbol_id(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

struct HttpHost {
    dao_vm* vm = nullptr;
    std::deque<std::string> responses;
    std::string last_url;
    std::string last_body;
    std::string last_content_type;
};

bool string_arg(const dao_value& value, std::string* out) {
    dao_bytes bytes{};
    if (dao_value_get_view(&value, &bytes) != DAO_OK) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data), bytes.size);
    return true;
}

dao_status response(HttpHost* host, std::string value, dao_value* out) {
    host->responses.push_back(std::move(value));
    const auto& stored = host->responses.back();
    return dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(stored.data()), stored.size()}, out);
}

dao_status http_get(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* host = static_cast<HttpHost*>(user_data);
    if (count != 1 || !string_arg(args[0], &host->last_url)) return DAO_TYPE_ERROR;
    return response(host, "get-ok", out);
}

dao_status http_post(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* host = static_cast<HttpHost*>(user_data);
    if (count != 3 || !string_arg(args[0], &host->last_url) ||
        !string_arg(args[1], &host->last_body) || args[2].type != DAO_VALUE_MAP) {
        return DAO_TYPE_ERROR;
    }
    const uint8_t key[] = {'C','o','n','t','e','n','t','-','T','y','p','e'};
    dao_value content_type{};
    if (dao_value_map_get(host->vm, &args[2], {key, sizeof(key)}, &content_type) != DAO_OK ||
        !string_arg(content_type, &host->last_content_type)) {
        return DAO_TYPE_ERROR;
    }
    return response(host, "post-ok", out);
}

bool resolve_http(void* user_data, std::string_view path, std::string* source,
                  std::string* error) {
    if (path != "std/http") {
        *error = "unknown module";
        return false;
    }
    *source = *static_cast<std::string*>(user_data);
    return true;
}

bool call(dao_vm* vm, dao_module* module, const char* name, dao_value* result,
          dao_error* error) {
    dao_function function{};
    return dao_module_find_export(module, symbol_id(name), &function) == DAO_OK &&
           dao_vm_call(vm, module, function, nullptr, 0, result, error) == DAO_OK;
}

bool equals_string(const dao_value& value, std::string_view expected) {
    dao_bytes bytes{};
    return dao_value_get_view(&value, &bytes) == DAO_OK &&
           std::string_view(reinterpret_cast<const char*>(bytes.data), bytes.size) == expected;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    std::ifstream input(argv[1], std::ios::binary);
    std::string http_source(std::istreambuf_iterator<char>(input), {});
    if (!input && http_source.empty()) return EXIT_FAILURE;

    const char* program = R"(
import "std/http" as http
thought get_case() { http_get("https://example.test/status") }
thought post_case() {
  http_post("https://example.test/items", "payload", {"Content-Type": "text/plain"})
}
thought ok_case() { http_is_ok({"ok": true}) }
thought error_case() { http_is_error({"ok": false}) }
)";
    dao::km::Options options{};
    options.import_resolver = resolve_http;
    options.import_user_data = &http_source;
    dao::ModuleBuilder builder;
    dao_error error{};
    check(dao::km::compile(program, builder, &error, options), "compile migrated http");
    const auto bytes = builder.encode();
    dao_vm* vm = dao_vm_create(nullptr);
    dao_module* module = nullptr;
    check(dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK,
          "load migrated http");

    HttpHost host{vm, {}, {}, {}, {}};
    const dao_host_function get_function{sizeof(dao_host_function), symbol_id("host_http_get"),
                                         1, 0, http_get, &host};
    const dao_host_function post_function{sizeof(dao_host_function), symbol_id("host_http_post"),
                                          3, 0, http_post, &host};
    check(dao_vm_register_host_function(vm, &get_function) == DAO_OK, "register HTTP GET");
    check(dao_vm_register_host_function(vm, &post_function) == DAO_OK, "register HTTP POST");

    if (module != nullptr) {
        dao_value result{};
        check(call(vm, module, "get_case", &result, &error) && equals_string(result, "get-ok") &&
                  host.last_url == "https://example.test/status",
              "http get");
        check(call(vm, module, "post_case", &result, &error) && equals_string(result, "post-ok") &&
                  host.last_url == "https://example.test/items" && host.last_body == "payload" &&
                  host.last_content_type == "text/plain",
              "http post");
        check(call(vm, module, "ok_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "http response ok");
        check(call(vm, module, "error_case", &result, &error) &&
                  result.type == DAO_VALUE_TRIT && result.payload == 1,
              "http response error");
        dao_module_release(module);
    }
    dao_vm_destroy(vm);
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "dao migrated http tests passed\n";
    return EXIT_SUCCESS;
}
