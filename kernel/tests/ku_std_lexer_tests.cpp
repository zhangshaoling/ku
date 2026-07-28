#include "dao/ku_migration.hpp"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const char* name) { if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; } }
uint32_t symbol_id(std::string_view name) { uint32_t hash=2166136261u; for(const unsigned char byte:name){hash^=byte;hash*=16777619u;} return hash; }

struct Sources { std::string lexer; std::string string; };
struct StringHost { std::deque<std::string> values; };

bool string_arg(const dao_value& value, std::string* out) {
    dao_bytes bytes{}; if(dao_value_get_view(&value,&bytes)!=DAO_OK)return false;
    out->assign(reinterpret_cast<const char*>(bytes.data),bytes.size); return true;
}
dao_status return_string(StringHost* host,std::string value,dao_value* out){host->values.push_back(std::move(value));const auto&stored=host->values.back();return dao_value_make_string_view({reinterpret_cast<const uint8_t*>(stored.data()),stored.size()},out);}
dao_status string_length(void*,const dao_value*args,size_t count,dao_value*out){dao_bytes bytes{};if(count!=1||dao_value_get_view(&args[0],&bytes)!=DAO_OK)return DAO_TYPE_ERROR;*out={DAO_VALUE_I64,0,static_cast<int64_t>(bytes.size)};return DAO_OK;}
dao_status string_char_at(void*user_data,const dao_value*args,size_t count,dao_value*out){std::string value;if(count!=2||!string_arg(args[0],&value)||args[1].type!=DAO_VALUE_I64||args[1].payload<0||static_cast<uint64_t>(args[1].payload)>=value.size())return DAO_TYPE_ERROR;return return_string(static_cast<StringHost*>(user_data),value.substr(static_cast<size_t>(args[1].payload),1),out);}
dao_status string_substring(void*user_data,const dao_value*args,size_t count,dao_value*out){std::string value;if(count!=3||!string_arg(args[0],&value)||args[1].type!=DAO_VALUE_I64||args[2].type!=DAO_VALUE_I64||args[1].payload<0||args[2].payload<args[1].payload||static_cast<uint64_t>(args[2].payload)>value.size())return DAO_TYPE_ERROR;return return_string(static_cast<StringHost*>(user_data),value.substr(static_cast<size_t>(args[1].payload),static_cast<size_t>(args[2].payload-args[1].payload)),out);}
dao_status string_concat(void*user_data,const dao_value*args,size_t count,dao_value*out){std::string left,right;if(count!=2||!string_arg(args[0],&left)||!string_arg(args[1],&right))return DAO_TYPE_ERROR;return return_string(static_cast<StringHost*>(user_data),left+right,out);}
dao_status string_ord(void*,const dao_value*args,size_t count,dao_value*out){std::string value;if(count!=1||!string_arg(args[0],&value)||value.empty())return DAO_TYPE_ERROR;*out={DAO_VALUE_I64,0,static_cast<unsigned char>(value[0])};return DAO_OK;}
dao_status string_chr(void*user_data,const dao_value*args,size_t count,dao_value*out){if(count!=1||args[0].type!=DAO_VALUE_I64||args[0].payload<0||args[0].payload>127)return DAO_TYPE_ERROR;return return_string(static_cast<StringHost*>(user_data),std::string(1,static_cast<char>(args[0].payload)),out);}

bool resolve(void*user_data,std::string_view path,std::string*source,std::string*error){const auto*s=static_cast<Sources*>(user_data);if(path=="std/lexer")*source=s->lexer;else if(path=="string")*source=s->string;else{*error="unknown module";return false;}return true;}
bool map_string(dao_vm*vm,const dao_value&map,std::string_view key,std::string*value){dao_value item{};if(dao_value_map_get(vm,&map,{reinterpret_cast<const uint8_t*>(key.data()),key.size()},&item)!=DAO_OK)return false;return string_arg(item,value);}
} // namespace

int main(int argc,char**argv){
    if(argc!=3)return EXIT_FAILURE;Sources sources{};std::string*targets[]={&sources.lexer,&sources.string};
    for(int i=0;i<2;++i){std::ifstream input(argv[i+1],std::ios::binary);targets[i]->assign(std::istreambuf_iterator<char>(input),{});if(!input&&targets[i]->empty())return EXIT_FAILURE;}
    const char*program=R"(
import "std/lexer" as lexer
thought lex_case() { lexer_lex("thought answer = 42\n") }
)";
    dao::km::Options options{};options.import_resolver=resolve;options.import_user_data=&sources;dao::ModuleBuilder builder;builder.set_identity("ku:test/lexer-suite",{1,0,0});dao_error error{};
    check(dao::km::compile(program,builder,&error,options),"compile migrated lexer");const auto bytes=builder.encode();dao::ModuleBuilder string_builder;string_builder.set_identity("ku:std/string",{1,0,0});check(dao::km::compile(sources.string,string_builder,&error),"compile identified string");const auto string_bytes=string_builder.encode();dao_vm*vm=dao_vm_create(nullptr);dao_module*module=nullptr;dao_module*string_module=nullptr;check(dao_vm_load_module(vm,{bytes.data(),bytes.size()},&module,&error)==DAO_OK,"load migrated lexer");check(dao_vm_load_module(vm,{string_bytes.data(),string_bytes.size()},&string_module,&error)==DAO_OK,"load identified string");
    StringHost host{};struct Host{const char*name;uint32_t arity;dao_host_callback callback;};const Host hosts[]={{"host_string_length",1,string_length},{"host_string_char_at",2,string_char_at},{"host_string_substring",3,string_substring},{"host_string_concat",2,string_concat},{"host_string_ord",1,string_ord},{"host_string_chr",1,string_chr}};
    for(const auto&item:hosts){dao_host_function function{sizeof(dao_host_function),symbol_id(item.name),item.arity,0,item.callback,&host};check(dao_vm_register_host_function(vm,&function)==DAO_OK,"register lexer host");}
    check(module&&dao_vm_link_module(vm,module,&error)==DAO_OK,"link lexer before string");
    if(module&&string_module){dao_function function{};dao_value result{};size_t size=0;check(dao_module_find_export(module,symbol_id("lex_case"),&function)==DAO_OK&&dao_vm_call(vm,module,function,nullptr,0,&result,&error)==DAO_IMPORT_NOT_FOUND,"lexer string dependency unresolved");check(dao_vm_link_module(vm,string_module,&error)==DAO_OK,"link identified string");check(dao_vm_call(vm,module,function,nullptr,0,&result,&error)==DAO_OK&&dao_value_list_size(vm,&result,&size)==DAO_OK&&size==6,"execute migrated lexer");
        const char*types[]={"keyword","name","op","number","newline","eof"};const char*values[]={"thought","answer","=","42","\n",""};
        for(size_t i=0;i<size&&i<6;++i){dao_value token{};std::string type,value;check(dao_value_list_get(vm,&result,i,&token)==DAO_OK&&map_string(vm,token,"type",&type)&&map_string(vm,token,"value",&value)&&type==types[i]&&value==values[i],"inspect lexer token");}
    }
    if(module)dao_module_release(module);if(string_module)dao_module_release(string_module);
    dao_vm_destroy(vm);if(failures)return EXIT_FAILURE;std::cout<<"dao migrated lexer tests passed\n";return EXIT_SUCCESS;
}
