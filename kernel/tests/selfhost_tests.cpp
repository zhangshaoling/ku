#include "dao/ku_migration.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
uint32_t symbol(std::string_view name) { uint32_t h=2166136261u; for(unsigned char c:name){h^=c;h*=16777619u;} return h; }
struct State { dao::ModuleBuilder builder; dao::FunctionSpec function; std::vector<uint8_t> bytes; };
struct Context { std::vector<std::unique_ptr<State>> states; std::deque<std::string> decoded_strings; };
bool i64(const dao_value& v) { return v.type == DAO_VALUE_I64; }
State* state(Context* c, const dao_value& v) { if(!i64(v)||v.payload<=0||static_cast<size_t>(v.payload)>c->states.size()) return nullptr; return c->states[static_cast<size_t>(v.payload)-1].get(); }
dao_status source_len(void*,const dao_value*a,size_t n,dao_value*out){if(n!=1||a[0].type!=DAO_VALUE_STRING)return DAO_TYPE_ERROR;*out={DAO_VALUE_I64,0,a[0].reserved};return DAO_OK;}
dao_status source_byte(void*,const dao_value*a,size_t n,dao_value*out){if(n!=2||a[0].type!=DAO_VALUE_STRING||!i64(a[1]))return DAO_TYPE_ERROR;dao_bytes b{};if(dao_value_get_view(&a[0],&b)!=DAO_OK||a[1].payload<0||static_cast<uint64_t>(a[1].payload)>=b.size)return DAO_INVALID_ARGUMENT;*out={DAO_VALUE_I64,0,b.data[a[1].payload]};return DAO_OK;}
dao_status source_slice(void*,const dao_value*a,size_t n,dao_value*out){if(n!=3||a[0].type!=DAO_VALUE_STRING||!i64(a[1])||!i64(a[2]))return DAO_TYPE_ERROR;dao_bytes b{};if(dao_value_get_view(&a[0],&b)!=DAO_OK||a[1].payload<0||a[2].payload<0||static_cast<uint64_t>(a[1].payload+a[2].payload)>b.size)return DAO_INVALID_ARGUMENT;return dao_value_make_string_view({b.data+a[1].payload,static_cast<size_t>(a[2].payload)},out);}
dao_status source_hash(void*,const dao_value*a,size_t n,dao_value*out){if(n!=3||a[0].type!=DAO_VALUE_STRING||!i64(a[1])||!i64(a[2]))return DAO_TYPE_ERROR;dao_bytes b{};if(dao_value_get_view(&a[0],&b)!=DAO_OK||a[1].payload<0||a[2].payload<0||static_cast<uint64_t>(a[1].payload+a[2].payload)>b.size)return DAO_INVALID_ARGUMENT;uint32_t h=2166136261u;for(int64_t i=0;i<a[2].payload;++i){h^=b.data[a[1].payload+i];h*=16777619u;}*out={DAO_VALUE_I64,0,h};return DAO_OK;}
dao_status string_new(void* u,const dao_value*,size_t n,dao_value*out){if(n)return DAO_INVALID_ARGUMENT;auto*c=static_cast<Context*>(u);c->decoded_strings.emplace_back();*out={DAO_VALUE_I64,0,static_cast<int64_t>(c->decoded_strings.size())};return DAO_OK;}
std::string* string_value(Context*c,const dao_value&handle){if(!i64(handle)||handle.payload<=0||static_cast<size_t>(handle.payload)>c->decoded_strings.size())return nullptr;return &c->decoded_strings[static_cast<size_t>(handle.payload)-1];}
dao_status string_append(void*u,const dao_value*a,size_t n,dao_value*out){auto*value=n==2?string_value(static_cast<Context*>(u),a[0]):nullptr;if(!value||!i64(a[1])||a[1].payload<0||a[1].payload>255)return DAO_INVALID_ARGUMENT;value->push_back(static_cast<char>(a[1].payload));*out={DAO_VALUE_NULL,0,0};return DAO_OK;}
dao_status string_finish(void*u,const dao_value*a,size_t n,dao_value*out){auto*value=n==1?string_value(static_cast<Context*>(u),a[0]):nullptr;if(!value)return DAO_INVALID_ARGUMENT;return dao_value_make_string_view({reinterpret_cast<const uint8_t*>(value->data()),value->size()},out);}
dao_status builder_new(void* u,const dao_value*,size_t n,dao_value*out){if(n)return DAO_INVALID_ARGUMENT;auto*c=static_cast<Context*>(u);c->states.push_back(std::make_unique<State>());*out={DAO_VALUE_I64,0,static_cast<int64_t>(c->states.size())};return DAO_OK;}
dao_status builder_begin(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==3?state(static_cast<Context*>(u),a[0]):nullptr;if(!s||!i64(a[1])||!i64(a[2]))return DAO_INVALID_ARGUMENT;s->function={static_cast<uint16_t>(a[1].payload),static_cast<uint16_t>(a[2].payload),{}};*out={DAO_VALUE_NULL,0,0};return DAO_OK;}
dao_status builder_emit(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==6?state(static_cast<Context*>(u),a[0]):nullptr;if(!s)return DAO_INVALID_ARGUMENT;for(size_t i=1;i<6;++i)if(!i64(a[i]))return DAO_TYPE_ERROR;s->function.code.push_back({static_cast<dao::Opcode>(a[1].payload),0,static_cast<uint16_t>(a[2].payload),static_cast<uint16_t>(a[3].payload),static_cast<uint16_t>(a[4].payload),a[5].payload});*out={DAO_VALUE_NULL,0,0};return DAO_OK;}
dao_status builder_add_string(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==2?state(static_cast<Context*>(u),a[0]):nullptr;if(!s||a[1].type!=DAO_VALUE_STRING)return DAO_INVALID_ARGUMENT;dao_bytes b{};if(dao_value_get_view(&a[1],&b)!=DAO_OK)return DAO_TYPE_ERROR;try{*out={DAO_VALUE_I64,0,static_cast<int64_t>(s->builder.add_string(std::string_view(reinterpret_cast<const char*>(b.data),b.size)))};}catch(...){return DAO_RUNTIME_ERROR;}return DAO_OK;}
dao_status builder_import(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==3?state(static_cast<Context*>(u),a[0]):nullptr;if(!s||!i64(a[1])||!i64(a[2])||a[1].payload<0||a[1].payload>UINT32_MAX||a[2].payload<0||a[2].payload>UINT16_MAX)return DAO_INVALID_ARGUMENT;try{*out={DAO_VALUE_I64,0,static_cast<int64_t>(s->builder.add_import(static_cast<uint32_t>(a[1].payload),static_cast<uint16_t>(a[2].payload)))};}catch(...){return DAO_RUNTIME_ERROR;}return DAO_OK;}
dao_status builder_position(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==1?state(static_cast<Context*>(u),a[0]):nullptr;if(!s)return DAO_INVALID_ARGUMENT;*out={DAO_VALUE_I64,0,static_cast<int64_t>(s->function.code.size())};return DAO_OK;}
dao_status builder_patch(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==3?state(static_cast<Context*>(u),a[0]):nullptr;if(!s||!i64(a[1])||!i64(a[2])||a[1].payload<0||static_cast<size_t>(a[1].payload)>=s->function.code.size())return DAO_INVALID_ARGUMENT;s->function.code[static_cast<size_t>(a[1].payload)].immediate=a[2].payload;*out={DAO_VALUE_NULL,0,0};return DAO_OK;}
dao_status builder_patch_marked(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==5?state(static_cast<Context*>(u),a[0]):nullptr;if(!s||!i64(a[1])||!i64(a[2])||!i64(a[3])||!i64(a[4])||a[1].payload<0||a[2].payload<a[1].payload||static_cast<size_t>(a[2].payload)>s->function.code.size())return DAO_INVALID_ARGUMENT;for(int64_t i=a[1].payload;i<a[2].payload;++i){auto&instruction=s->function.code[static_cast<size_t>(i)];if(instruction.opcode==dao::Opcode::Jump&&instruction.immediate==a[3].payload)instruction.immediate=a[4].payload;}*out={DAO_VALUE_NULL,0,0};return DAO_OK;}
dao_status host_double(void*,const dao_value*a,size_t n,dao_value*out){if(n!=1||!i64(a[0]))return DAO_TYPE_ERROR;*out={DAO_VALUE_I64,0,a[0].payload*2};return DAO_OK;}
dao_status host_add(void*,const dao_value*a,size_t n,dao_value*out){if(n!=2||!i64(a[0])||!i64(a[1]))return DAO_TYPE_ERROR;*out={DAO_VALUE_I64,0,a[0].payload+a[1].payload};return DAO_OK;}
dao_status builder_finish(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==2?state(static_cast<Context*>(u),a[0]):nullptr;if(!s||!i64(a[1]))return DAO_INVALID_ARGUMENT;try{const uint32_t f=s->builder.add_function(std::move(s->function));s->builder.add_export(static_cast<uint32_t>(a[1].payload),f);}catch(...){return DAO_RUNTIME_ERROR;}*out={DAO_VALUE_NULL,0,0};return DAO_OK;}
dao_status builder_encode(void*u,const dao_value*a,size_t n,dao_value*out){auto*s=n==1?state(static_cast<Context*>(u),a[0]):nullptr;if(!s)return DAO_INVALID_ARGUMENT;try{s->bytes=s->builder.encode();}catch(...){return DAO_RUNTIME_ERROR;}return dao_value_make_bytes_view({s->bytes.data(),s->bytes.size()},out);}
}

int main(int argc,char**argv){
    if(argc!=2)return EXIT_FAILURE;std::ifstream in(argv[1],std::ios::binary);const std::string compiler((std::istreambuf_iterator<char>(in)),{});
    dao::ModuleBuilder bootstrap;dao_error error{};if(!dao::km::compile(compiler,bootstrap,&error)){std::cerr<<error.message<<'\n';return EXIT_FAILURE;}const auto compiler_bytes=bootstrap.encode();
    dao_vm_config config=dao_vm_config_default();config.max_instructions_per_call=100ULL*1000ULL*1000ULL;
    dao_vm*vm=dao_vm_create(&config);dao_module*module=nullptr;if(dao_vm_load_module(vm,{compiler_bytes.data(),compiler_bytes.size()},&module,&error)!=DAO_OK)return EXIT_FAILURE;
    Context context;struct Host{const char*name;uint32_t arity;dao_host_callback callback;};
    const Host hosts[]={{"source_len",1,source_len},{"source_byte",2,source_byte},{"source_slice",3,source_slice},{"source_hash",3,source_hash},{"string_new",0,string_new},{"string_append",2,string_append},{"string_finish",1,string_finish},{"builder_new",0,builder_new},{"builder_begin",3,builder_begin},{"builder_emit_raw",6,builder_emit},{"builder_add_string",2,builder_add_string},{"builder_import",3,builder_import},{"builder_position",1,builder_position},{"builder_patch",3,builder_patch},{"builder_patch_marked",5,builder_patch_marked},{"builder_encode",1,builder_encode},{"builder_finish",2,builder_finish},{"host_double",1,host_double},{"host_add",2,host_add}};
    for(const auto&h:hosts){dao_host_function f{sizeof(f),symbol(h.name),h.arity,0,h.callback,&context};if(dao_vm_register_host_function(vm,&f)!=DAO_OK)return EXIT_FAILURE;}
    dao_function function=0;if(dao_module_find_export(module,symbol("compile"),&function)!=DAO_OK)return EXIT_FAILURE;
    struct Case{const char*source;const char*name;int64_t expected;uint8_t type=DAO_VALUE_I64;uint8_t argument_count=0;const char*text=nullptr;};const Case cases[]={{"thought main() { 40 + 2 }","main",42},{"thought product() { 6 * 7 }","product",42},{"thought difference() { 50 - 8 }","difference",42},{"thought quotient() { 84 / 2 }","quotient",42},{"thought precedence() { 2 + 3 * 4 }","precedence",14},{"thought mixed() { 20 / 2 + 32 }","mixed",42},{"thought remainder() { 86 % 44 }","remainder",42},{"thought chain() { 100 - 8 * 7 - 2 }","chain",42},{"thought parens() { (2 + 3) * 8 + 2 }","parens",42},{"thought unary() { 50 + -8 }","unary",42},{"thought nested() { 2 * (10 + 11) }","nested",42},{"thought first() { 40 + 2 } thought second() { 6 * 7 }","first",42},{"thought first() { 40 + 2 } thought second() { 6 * 7 }","second",42},{"thought with_args(left, right) { left + right }","with_args",42,DAO_VALUE_I64,2},{"thought local() { value = 40; value + 2 }","local",42},{"thought reassigned() { value = 40; value = value + 2; value }","reassigned",42},{"thought independent() { index = 0; start = index; index = 7; start + 42 }","independent",42},{"thought double(value) { value * 2 } thought called() { double(21) }","called",42},{"thought add(left, right) { left + right } thought called() { add(20, 22) }","called",42},{"thought answer() { 42 } thought called() { answer() }","called",42},{"thought add(left, right) { left + right } thought with_args(left, right) { add(left, right) }","with_args",42,DAO_VALUE_I64,2},{"thought add(left, right) { left + right } thought calculated() { add(8 * 2, 30 - 4) }","calculated",42},{"thought equal() { 21 == 21 }","equal",1,DAO_VALUE_TRIT},{"thought unequal() { 21 != 22 }","unequal",1,DAO_VALUE_TRIT},{"thought ordered() { 20 < 22 }","ordered",1,DAO_VALUE_TRIT},{"thought ordered() { 22 <= 22 }","ordered",1,DAO_VALUE_TRIT},{"thought ordered() { 22 > 20 }","ordered",1,DAO_VALUE_TRIT},{"thought ordered() { 22 >= 22 }","ordered",1,DAO_VALUE_TRIT},{"thought false_ordered() { 22 < 20 }","false_ordered",-1,DAO_VALUE_TRIT},{"thought choose(value) { if value > 0 { return 42 } return 0 }","choose",42,DAO_VALUE_I64,1},{"thought choose() { if 20 > 22 { return 0 } return 42 }","choose",42},{"thought choose(value) { if value > 0 { return 42 } else { return 0 } }","choose",42,DAO_VALUE_I64,1},{"thought choose() { if 20 > 22 { return 0 } else { return 42 } }","choose",42},{"thought sum_to(n) { total = 0; while n > 0 { total = total + n; n = n - 1 } return total }","sum_to",820,DAO_VALUE_I64,1},{"thought sum_until(n) { total = 0; while n > 0 { n = n - 1; if n == 3 { continue } if n == 1 { break } total = total + n } return total }","sum_until",776,DAO_VALUE_I64,1},{"thought nested_loops(n) { total = 0; while n > 0 { inner = 0; while inner < 2 { inner = inner + 1; if inner == 1 { continue } total = total + 1 } n = n - 1 } return total }","nested_loops",40,DAO_VALUE_I64,1},{"thought greeting() { \"dao\" }","greeting",0,DAO_VALUE_STRING,0,"dao"},{"thought esc_nl() { \"a\\nb\" }","esc_nl",0,DAO_VALUE_STRING,0,"a\nb"},{"thought esc_tab() { \"x\\ty\" }","esc_tab",0,DAO_VALUE_STRING,0,"x\ty"},{"thought esc_bs() { \"a\\\\b\" }","esc_bs",0,DAO_VALUE_STRING,0,"a\\b"},{"thought esc_dq() { \"say \\\"hi\\\"\" }","esc_dq",0,DAO_VALUE_STRING,0,"say \"hi\""},{"thought esc_mix() { \"a\\nb\\t\\\\\\\"z\" }","esc_mix",0,DAO_VALUE_STRING,0,"a\nb\t\\\"z"},{"thought esc_cr() { \"a\\rb\" }","esc_cr",0,DAO_VALUE_STRING,0,"a\rb"},{"thought list_size() { items = [2, 3, 5]; len(items) }","list_size",3},{"thought nested_list_size() { len([1, [2, 3]]) }","nested_list_size",2},{"thought list_item() { items = [10, 42]; items[1] }","list_item",42},{"thought collection_total() { values = [2, 3, 5]; total = 0; for item in values { total = total + item } return total }","collection_total",10},{"thought map_value() { mapping = {\"bonus\": 42}; mapping[\"bonus\"] }","map_value",42}};
    const Case logic_cases[]={{"thought logic() { true and not false }","logic",1,DAO_VALUE_TRIT},{"thought alternate() { false or true }","alternate",1,DAO_VALUE_TRIT},{"thought empty() { null }","empty",0,DAO_VALUE_NULL}};
    const auto run_cases=[&](dao_module*compiler_module,dao_function compile_function){
      const auto run_group=[&](const Case*group,size_t count){
        for(size_t index=0;index<count;++index){
            const auto&test=group[index];
            const std::string source=test.source;dao_value arg{};
            if(dao_value_make_string_view({reinterpret_cast<const uint8_t*>(source.data()),source.size()},&arg)!=DAO_OK)return false;
            dao_value generated{};
            if(dao_vm_call(vm,compiler_module,compile_function,&arg,1,&generated,&error)!=DAO_OK)return false;
            dao_bytes output{};
            if(dao_value_get_view(&generated,&output)!=DAO_OK)return false;
            dao_module*result_module=nullptr;
            if(dao_vm_load_module(vm,output,&result_module,&error)!=DAO_OK)return false;
            dao_function generated_fn=0;
            if(dao_module_find_export(result_module,symbol(test.name),&generated_fn)!=DAO_OK){dao_module_release(result_module);return false;}
            const dao_value supplied[]={{DAO_VALUE_I64,0,40},{DAO_VALUE_I64,0,2}};dao_value result{};
            bool ok=dao_vm_call(vm,result_module,generated_fn,test.argument_count?supplied:nullptr,test.argument_count,&result,&error)==DAO_OK&&result.type==test.type;
            if(ok&&test.text){dao_bytes text{};ok=dao_value_get_view(&result,&text)==DAO_OK&&std::string_view(reinterpret_cast<const char*>(text.data),text.size)==test.text;}
            if(ok&&!test.text)ok=result.payload==test.expected;
            dao_module_release(result_module);
            if(!ok)return false;
        }
        return true;
      };
      return run_group(cases,std::size(cases))&&run_group(logic_cases,std::size(logic_cases));
    };
    if(!run_cases(module,function))return EXIT_FAILURE;
    const auto compile_module=[&](dao_module*compiler_module,dao_function compile_function,std::string_view source,dao_module**result_module){
        dao_value arg{};if(dao_value_make_string_view({reinterpret_cast<const uint8_t*>(source.data()),source.size()},&arg)!=DAO_OK)return false;
        dao_value generated{};if(dao_vm_call(vm,compiler_module,compile_function,&arg,1,&generated,&error)!=DAO_OK)return false;
        dao_bytes output{};return dao_value_get_view(&generated,&output)==DAO_OK&&dao_vm_load_module(vm,output,result_module,&error)==DAO_OK;
    };
    const auto run_extended=[&](dao_module*compiler_module,dao_function compile_function){
        dao_module*generated=nullptr;dao_function generated_function=0;dao_value result{};
        if(!compile_module(compiler_module,compile_function,"import host_add(2) thought add(left, right) { host_add(left, right) } thought call_host() { add(40, 2) }",&generated))return false;
        bool ok=dao_module_find_export(generated,symbol("call_host"),&generated_function)==DAO_OK&&dao_vm_call(vm,generated,generated_function,nullptr,0,&result,&error)==DAO_OK&&result.type==DAO_VALUE_I64&&result.payload==42;
        dao_module_release(generated);if(!ok)return false;
        generated=nullptr;
        if(!compile_module(compiler_module,compile_function,"thought mutate() { items = [2, 3]; items[1] = 42; mapping = {\"bonus\": 2}; mapping[\"bonus\"] = items[1]; mapping[\"bonus\"] }",&generated))return false;
        ok=dao_module_find_export(generated,symbol("mutate"),&generated_function)==DAO_OK&&dao_vm_call(vm,generated,generated_function,nullptr,0,&result,&error)==DAO_OK&&result.type==DAO_VALUE_I64&&result.payload==42;
        dao_module_release(generated);if(!ok)return false;
        generated=nullptr;
        if(!compile_module(compiler_module,compile_function,"thought recover() { try { throw \"handled\" } catch error { return error } }",&generated))return false;
        dao_bytes exception_text{};
        ok=dao_module_find_export(generated,symbol("recover"),&generated_function)==DAO_OK&&dao_vm_call(vm,generated,generated_function,nullptr,0,&result,&error)==DAO_OK&&result.type==DAO_VALUE_STRING&&dao_value_get_view(&result,&exception_text)==DAO_OK&&std::string_view(reinterpret_cast<const char*>(exception_text.data),exception_text.size)=="handled";
        dao_module_release(generated);return ok;
    };
    if(!run_extended(module,function))return EXIT_FAILURE;
    const char*invalid_sources[]={"thought bad() { \"unterminated }","thought bad() { missing }","thought bad() { [1, 2 }","thought bad() { {\"key\" 42} }","thought add(left, right) { left + right } thought bad() { add(1) }","import host_double(1) thought bad() { host_double(1, 2) }","thought bad() { absent() }","thought bad()","thought bad() { 42 ","thought bad() { @ }","thought bad() { [1, ","import host_double(x) thought bad() { 42 }","other bad() { 42 }","thought same() { 1 } thought same() { 2 }","import same(1) thought same() { 1 }","import same(1) import same(1) thought ok() { 1 }"};
    const std::string duplicate_parameter="thought bad(value, value) { value }";
    std::string register_overflow="thought bad() { ";
    for(int i=0;i<4100;++i)register_overflow+="1 + ";
    register_overflow+="1 }";
    const auto run_rejections=[&](dao_module*compiler_module,dao_function compile_function){
        for(const char*invalid_source:invalid_sources){
            const std::string_view invalid=invalid_source;dao_value arg{};dao_value generated{};
            if(dao_value_make_string_view({reinterpret_cast<const uint8_t*>(invalid.data()),invalid.size()},&arg)!=DAO_OK||dao_vm_call(vm,compiler_module,compile_function,&arg,1,&generated,&error)==DAO_OK)return false;
        }
        const std::string_view rejected[]={duplicate_parameter,register_overflow};
        for(const auto source:rejected){
            dao_value arg{};dao_value generated{};
            if(dao_value_make_string_view({reinterpret_cast<const uint8_t*>(source.data()),source.size()},&arg)!=DAO_OK||dao_vm_call(vm,compiler_module,compile_function,&arg,1,&generated,&error)==DAO_OK)return false;
        }
        return true;
    };
    if(!run_rejections(module,function))return EXIT_FAILURE;
    dao_value compiler_source_arg{};
    if(dao_value_make_string_view({reinterpret_cast<const uint8_t*>(compiler.data()),compiler.size()},&compiler_source_arg)!=DAO_OK)return EXIT_FAILURE;
    dao_value rebuilt_compiler_value{};
    if(dao_vm_call(vm,module,function,&compiler_source_arg,1,&rebuilt_compiler_value,&error)!=DAO_OK)return EXIT_FAILURE;
    dao_bytes rebuilt_compiler_bytes{};
    if(dao_value_get_view(&rebuilt_compiler_value,&rebuilt_compiler_bytes)!=DAO_OK)return EXIT_FAILURE;
    dao_module*rebuilt_compiler=nullptr;
    if(dao_vm_load_module(vm,rebuilt_compiler_bytes,&rebuilt_compiler,&error)!=DAO_OK)return EXIT_FAILURE;
    dao_function rebuilt_compile=0;
    if(dao_module_find_export(rebuilt_compiler,symbol("compile"),&rebuilt_compile)!=DAO_OK)return EXIT_FAILURE;
    if(!run_cases(rebuilt_compiler,rebuilt_compile)||!run_extended(rebuilt_compiler,rebuilt_compile)||!run_rejections(rebuilt_compiler,rebuilt_compile))return EXIT_FAILURE;
    dao_value second_rebuilt_compiler_value{};
    if(dao_vm_call(vm,rebuilt_compiler,rebuilt_compile,&compiler_source_arg,1,&second_rebuilt_compiler_value,&error)!=DAO_OK)return EXIT_FAILURE;
    dao_bytes second_rebuilt_compiler_bytes{};
    if(dao_value_get_view(&second_rebuilt_compiler_value,&second_rebuilt_compiler_bytes)!=DAO_OK||second_rebuilt_compiler_bytes.size!=rebuilt_compiler_bytes.size||std::memcmp(second_rebuilt_compiler_bytes.data,rebuilt_compiler_bytes.data,rebuilt_compiler_bytes.size)!=0)return EXIT_FAILURE;
    const std::string rebuilt_source="thought rebuilt() { 40 + 2 }";
    dao_value rebuilt_source_arg{};
    if(dao_value_make_string_view({reinterpret_cast<const uint8_t*>(rebuilt_source.data()),rebuilt_source.size()},&rebuilt_source_arg)!=DAO_OK)return EXIT_FAILURE;
    dao_value rebuilt_module_value{};
    if(dao_vm_call(vm,rebuilt_compiler,rebuilt_compile,&rebuilt_source_arg,1,&rebuilt_module_value,&error)!=DAO_OK)return EXIT_FAILURE;
    dao_bytes rebuilt_module_bytes{};
    if(dao_value_get_view(&rebuilt_module_value,&rebuilt_module_bytes)!=DAO_OK)return EXIT_FAILURE;
    dao_module*rebuilt_module=nullptr;
    if(dao_vm_load_module(vm,rebuilt_module_bytes,&rebuilt_module,&error)!=DAO_OK)return EXIT_FAILURE;
    dao_function rebuilt_function=0;
    if(dao_module_find_export(rebuilt_module,symbol("rebuilt"),&rebuilt_function)!=DAO_OK)return EXIT_FAILURE;
    dao_value rebuilt_result{};
    const bool rebuilt_ok=dao_vm_call(vm,rebuilt_module,rebuilt_function,nullptr,0,&rebuilt_result,&error)==DAO_OK&&rebuilt_result.type==DAO_VALUE_I64&&rebuilt_result.payload==42;
    dao_module_release(rebuilt_module);
    dao_module_release(rebuilt_compiler);
    if(!rebuilt_ok)return EXIT_FAILURE;
    dao_module_release(module);dao_vm_destroy(vm);std::cout<<"dao .ku self-host seed passed\n";return EXIT_SUCCESS;
}
