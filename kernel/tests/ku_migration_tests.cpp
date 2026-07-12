#include "dao/assemble.hpp"
#include "dao/disassemble.hpp"
#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char* message) { if (!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; } }
uint32_t symbol_id(std::string_view name) { uint32_t hash = 2166136261u; for (const unsigned char c : name) { hash ^= c; hash *= 16777619u; } return hash; }
dao_status host_double(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 1 || args[0].type != DAO_VALUE_I64) return DAO_TYPE_ERROR;
    *out = {DAO_VALUE_I64, 0, args[0].payload * 2}; return DAO_OK;
}
struct ImportSources { std::unordered_map<std::string, std::string> values; };
bool resolve_import(void* user_data, std::string_view path, std::string* source,
                    std::string* error) {
    const auto& values = static_cast<ImportSources*>(user_data)->values;
    const auto found = values.find(std::string(path));
    if (found == values.end()) { *error = "not found"; return false; }
    *source = found->second;
    return true;
}
}
#define CHECK(name, expression) check((expression), (name))

int main() {
    const char* source = R"(
;; legacy comment punctuation . and Unicode 注释
import host_double(1)
thought add(a, b) { return a + b }
thought use_host(x) { host_double(x) }
thought literal() { "Dao道" }
thought calculate(x) {
  doubled = add(x, x)
  (doubled * 3) % 100
}
thought compare(x) { x >= 7 }
thought prefix_compare(x) { >= (x, 7) }
thought prefix_logic(x) { and(>= (x, 7), < (x, 10)) }
thought variadic_prefix_logic(x) { and(>= (x, 7), < (x, 10), != (x, 8)) }
thought logic() { true and not false }
thought nothing() { null }
thought missing_is_null() { {}["missing"] == null }
thought null_differs_from_number() { null != 0 }
thought string_equal() { "dao" == "dao" }
thought string_not_equal() { "dao" != "ku" }
thought trit_equal() { true == true }
thought multiline_map() {
  {
    "answer": 42,
    "other": 7
  }["answer"]
}
thought bare_alternate(x) { if x > 0 { return 10 } { return 20 } }
thought conditional_expression(x) {
  value = if (x > 0) { 40 } { 2 }
  value + 2
}
thought choose(x) {
  if x > 0 { return 10 } else { return 20 }
}
thought sum_to(n) {
  total = 0
  while n > 0 {
    total = total + n
    n = n - 1
  }
  return total
}
thought loop_control(n) {
  total = 0
  while n > 0 {
    n = n - 1
    if n == 3 { continue }
    if n == 1 { break }
    total = total + n
  }
  return total
}
thought list_sum() {
  values = [1, 2, 3, 4]
  total = values[0]
  for value in values {
    if value == 2 { continue }
    total = total + value
  }
  return total
}
thought map_read() {
  values = {"answer": 42, "other": 7}
  return values["answer"]
}
thought return_list() { [4, 5, 6] }
thought return_map() { {"answer": 42} }
thought mutate() {
  values = [1, 2]
  values[0] = 40
  mapping = {}
  mapping["answer"] = values[0] + values[1]
  return mapping["answer"]
}
thought appended() {
  values = []
  values = values + [40]
  values = values + [2]
  values
}
thought pushed() {
  values = []
  push(values, 42)
  values[0]
}
thought raises() { throw "bad" }
thought catches() {
  try { raises() } catch err { return err }
}
)";
    dao::ModuleBuilder builder;
    dao_error error{};
    CHECK("compile source", dao::km::compile(source, builder, &error));
    const auto bytes = builder.encode();

    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config); dao_module* module = nullptr;
    CHECK("load generated module", dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error) == DAO_OK);
    dao_function fn{}; CHECK("find calculate", dao_module_find_export(module, symbol_id("calculate"), &fn) == DAO_OK);
    const dao_value args[] = {{DAO_VALUE_I64, 0, 7}}; dao_value result{};
    dao_host_function host{sizeof(dao_host_function), symbol_id("host_double"), 1, 0, host_double, nullptr};
    CHECK("register compiled host import", dao_vm_register_host_function(vm, &host) == DAO_OK);
    CHECK("find host wrapper", dao_module_find_export(module, symbol_id("use_host"), &fn) == DAO_OK);
    CHECK("execute host wrapper", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("host wrapper result", result.type == DAO_VALUE_I64 && result.payload == 14);
    CHECK("find string literal", dao_module_find_export(module, symbol_id("literal"), &fn) == DAO_OK);
    CHECK("execute string literal", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    dao_bytes string_view{};
    CHECK("read string literal", dao_value_get_view(&result, &string_view) == DAO_OK);
    CHECK("string literal bytes", string_view.size == 6 && std::memcmp(string_view.data, "Dao\xe9\x81\x93", 6) == 0);
    CHECK("refind calculate", dao_module_find_export(module, symbol_id("calculate"), &fn) == DAO_OK);
    CHECK("execute calculate", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("calculate result", result.type == DAO_VALUE_I64 && result.payload == 42);
    CHECK("find compare", dao_module_find_export(module, symbol_id("compare"), &fn) == DAO_OK);
    CHECK("execute compare", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("compare result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find prefix compare", dao_module_find_export(module, symbol_id("prefix_compare"), &fn) == DAO_OK);
    CHECK("execute prefix compare", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("prefix compare result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find prefix logic", dao_module_find_export(module, symbol_id("prefix_logic"), &fn) == DAO_OK);
    CHECK("execute prefix logic", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("prefix logic result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find variadic prefix logic", dao_module_find_export(module, symbol_id("variadic_prefix_logic"), &fn) == DAO_OK);
    CHECK("execute variadic prefix logic", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("variadic prefix logic result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find logic", dao_module_find_export(module, symbol_id("logic"), &fn) == DAO_OK);
    CHECK("execute logic", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("logic result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find null", dao_module_find_export(module, symbol_id("nothing"), &fn) == DAO_OK);
    CHECK("execute null", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("null result", result.type == DAO_VALUE_NULL);
    CHECK("find missing null", dao_module_find_export(module, symbol_id("missing_is_null"), &fn) == DAO_OK);
    CHECK("execute missing null", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("missing key is null", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find null inequality", dao_module_find_export(module, symbol_id("null_differs_from_number"), &fn) == DAO_OK);
    CHECK("execute null inequality", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("null differs from number", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find string equal", dao_module_find_export(module, symbol_id("string_equal"), &fn) == DAO_OK);
    CHECK("execute string equal", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("string equal result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find string not equal", dao_module_find_export(module, symbol_id("string_not_equal"), &fn) == DAO_OK);
    CHECK("execute string not equal", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("string not equal result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find trit equal", dao_module_find_export(module, symbol_id("trit_equal"), &fn) == DAO_OK);
    CHECK("execute trit equal", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("trit equal result", result.type == DAO_VALUE_TRIT && result.payload == 1);
    CHECK("find multiline map", dao_module_find_export(module, symbol_id("multiline_map"), &fn) == DAO_OK);
    CHECK("execute multiline map", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("multiline map result", result.type == DAO_VALUE_I64 && result.payload == 42);
    const dao_value negative[] = {{DAO_VALUE_I64, 0, -1}};
    CHECK("find bare alternate", dao_module_find_export(module, symbol_id("bare_alternate"), &fn) == DAO_OK);
    CHECK("execute bare alternate", dao_vm_call(vm, module, fn, negative, 1, &result, &error) == DAO_OK);
    CHECK("bare alternate result", result.type == DAO_VALUE_I64 && result.payload == 20);
    CHECK("find conditional expression", dao_module_find_export(module, symbol_id("conditional_expression"), &fn) == DAO_OK);
    CHECK("execute conditional expression", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("conditional expression result", result.type == DAO_VALUE_I64 && result.payload == 42);
    CHECK("find choose", dao_module_find_export(module, symbol_id("choose"), &fn) == DAO_OK);
    CHECK("execute true branch", dao_vm_call(vm, module, fn, args, 1, &result, &error) == DAO_OK);
    CHECK("true branch result", result.type == DAO_VALUE_I64 && result.payload == 10);
    CHECK("execute false branch", dao_vm_call(vm, module, fn, negative, 1, &result, &error) == DAO_OK);
    CHECK("false branch result", result.type == DAO_VALUE_I64 && result.payload == 20);
    CHECK("find sum_to", dao_module_find_export(module, symbol_id("sum_to"), &fn) == DAO_OK);
    const dao_value five[] = {{DAO_VALUE_I64, 0, 5}};
    CHECK("execute while", dao_vm_call(vm, module, fn, five, 1, &result, &error) == DAO_OK);
    CHECK("while result", result.type == DAO_VALUE_I64 && result.payload == 15);
    CHECK("find loop_control", dao_module_find_export(module, symbol_id("loop_control"), &fn) == DAO_OK);
    CHECK("execute break continue", dao_vm_call(vm, module, fn, five, 1, &result, &error) == DAO_OK);
    CHECK("break continue result", result.type == DAO_VALUE_I64 && result.payload == 6);
    CHECK("find list_sum", dao_module_find_export(module, symbol_id("list_sum"), &fn) == DAO_OK);
    CHECK("execute list for", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("list for result", result.type == DAO_VALUE_I64 && result.payload == 9);
    CHECK("find map_read", dao_module_find_export(module, symbol_id("map_read"), &fn) == DAO_OK);
    CHECK("execute map read", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("map read result", result.type == DAO_VALUE_I64 && result.payload == 42);
    CHECK("find return_list", dao_module_find_export(module, symbol_id("return_list"), &fn) == DAO_OK);
    CHECK("execute return_list", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    const dao_value old_list = result; size_t list_size = 0; dao_value item{};
    CHECK("inspect list size", dao_value_list_size(vm, &result, &list_size) == DAO_OK && list_size == 3);
    CHECK("inspect list item", dao_value_list_get(vm, &result, 1, &item) == DAO_OK && item.type == DAO_VALUE_I64 && item.payload == 5);
    CHECK("find return_map", dao_module_find_export(module, symbol_id("return_map"), &fn) == DAO_OK);
    CHECK("execute return_map", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("stale list rejected", dao_value_list_size(vm, &old_list, &list_size) == DAO_RUNTIME_ERROR);
    const uint8_t answer_key[] = {'a','n','s','w','e','r'};
    CHECK("inspect map item", dao_value_map_get(vm, &result, {answer_key, sizeof(answer_key)}, &item) == DAO_OK && item.type == DAO_VALUE_I64 && item.payload == 42);
    CHECK("find mutate", dao_module_find_export(module, symbol_id("mutate"), &fn) == DAO_OK);
    CHECK("execute index mutation", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK && result.type == DAO_VALUE_I64 && result.payload == 42);
    CHECK("find appended", dao_module_find_export(module, symbol_id("appended"), &fn) == DAO_OK);
    CHECK("execute list append", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK && result.type == DAO_VALUE_LIST);
    CHECK("inspect appended size", dao_value_list_size(vm, &result, &list_size) == DAO_OK && list_size == 2);
    CHECK("inspect appended item", dao_value_list_get(vm, &result, 0, &item) == DAO_OK && item.type == DAO_VALUE_I64 && item.payload == 40);
    CHECK("find pushed", dao_module_find_export(module, symbol_id("pushed"), &fn) == DAO_OK);
    CHECK("execute push builtin", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK && result.type == DAO_VALUE_I64 && result.payload == 42);
    CHECK("find catches", dao_module_find_export(module, symbol_id("catches"), &fn) == DAO_OK);
    CHECK("execute cross-call catch", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_OK);
    CHECK("read caught value", dao_value_get_view(&result, &string_view) == DAO_OK);
    CHECK("caught value", string_view.size == 3 && std::memcmp(string_view.data, "bad", 3) == 0);
    CHECK("find raises", dao_module_find_export(module, symbol_id("raises"), &fn) == DAO_OK);
    CHECK("uncaught throw", dao_vm_call(vm, module, fn, nullptr, 0, &result, &error) == DAO_RUNTIME_ERROR);

    dao::DisassembledModule disassembled{};
    CHECK("disassemble", dao::disassemble({bytes.data(), bytes.size()}, &disassembled, &error) == DAO_OK);
    std::vector<uint8_t> round_trip;
    const bool assembled = dao::assemble_text(dao::to_text(disassembled), &round_trip, &error);
    if (!assembled) std::cerr << "assemble error: " << error.message << '\n';
    CHECK("assemble text", assembled);
    CHECK("round trip identical", round_trip == bytes);

    ImportSources import_sources{{
        {"math", "thought add(a, b) { a + b } thought double(value) { add(value, value) }"},
    }};
    dao::km::Options import_options{};
    import_options.import_resolver = resolve_import;
    import_options.import_user_data = &import_sources;
    dao::ModuleBuilder imported_builder;
    const std::string imported_source =
        "\xE5\xBC\x95 \"math\" \xE5\x88\xAB m\nthought imported_answer() { m_double(21) }";
    CHECK("compile legacy module import",
          dao::km::compile(imported_source, imported_builder, &error, import_options));
    const auto imported_bytes = imported_builder.encode();
    dao_module* imported_module = nullptr;
    CHECK("load legacy module import",
          dao_vm_load_module(vm, {imported_bytes.data(), imported_bytes.size()}, &imported_module,
                             &error) == DAO_OK);
    CHECK("find legacy import caller",
          dao_module_find_export(imported_module, symbol_id("imported_answer"), &fn) == DAO_OK);
    CHECK("execute legacy import caller",
          dao_vm_call(vm, imported_module, fn, nullptr, 0, &result, &error) == DAO_OK &&
              result.type == DAO_VALUE_I64 && result.payload == 42);
    dao_module_release(imported_module);

    dao::ModuleBuilder missing_resolver;
    CHECK("legacy import requires resolver",
          !dao::km::compile("import \"math\" as m\nthought bad() { m_double(1) }",
                            missing_resolver, &error));
    import_sources.values["cycle"] =
        "import \"cycle\" as nested\nthought value() { 1 }";
    dao::ModuleBuilder cyclic_import;
    CHECK("reject cyclic legacy import",
          !dao::km::compile("import \"cycle\" as cycle\nthought bad() { cycle_value() }",
                            cyclic_import, &error, import_options));

    dao::ModuleBuilder invalid;
    CHECK("reject break outside loop", !dao::km::compile("thought bad() { break }", invalid, &error));
    dao::ModuleBuilder bad_arity;
    CHECK("reject internal arity mismatch", !dao::km::compile("thought f(x) { x } thought g() { f() }", bad_arity, &error));

    dao_module_release(module); dao_vm_destroy(vm);
    if (failures) return EXIT_FAILURE;
    std::cout << "dao ku migration tests passed\n";
    return EXIT_SUCCESS;
}
