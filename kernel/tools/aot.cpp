#include "dao/disassemble.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
bool supported(dao::Opcode op) {
    using dao::Opcode;
    return op == Opcode::Nop || op == Opcode::LoadI64 || op == Opcode::LoadTrit ||
           op == Opcode::LoadNull || op == Opcode::Move || op == Opcode::AddI64 ||
           op == Opcode::SubI64 || op == Opcode::MulI64 || op == Opcode::DivI64 ||
           op == Opcode::RemI64 || (op >= Opcode::CompareEqI64 && op <= Opcode::CompareGeI64) ||
           op == Opcode::TritNot || op == Opcode::TritAnd || op == Opcode::TritOr ||
           op == Opcode::BranchTritNegative || op == Opcode::BranchTritZero ||
           op == Opcode::BranchTritPositive || op == Opcode::Jump || op == Opcode::Call ||
           op == Opcode::Return || op == Opcode::TryBegin || op == Opcode::TryEnd ||
           op == Opcode::Throw || op == Opcode::Catch;
}
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: dao-aot <input.dao> <output.c>\n"; return EXIT_FAILURE; }
    std::ifstream input(argv[1], std::ios::binary); if (!input) return EXIT_FAILURE;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    dao::DisassembledModule module{}; dao_error error{};
    if (dao::disassemble({bytes.data(), bytes.size()}, &module, &error) != DAO_OK) {
        std::cerr << "invalid module: " << error.message << '\n'; return EXIT_FAILURE;
    }
    for (const auto& function : module.functions) for (const auto& instruction : function.instructions)
        if (!supported(instruction.opcode)) { std::cerr << "function " << function.index << " uses unsupported AOT opcode\n"; return EXIT_FAILURE; }
    std::ofstream out(argv[2], std::ios::trunc); if (!out) return EXIT_FAILURE;
    out << "#include <stddef.h>\n#include <stdint.h>\n#include <limits.h>\n"
           "#ifdef _WIN32\n#define DAO_AOT_EXPORT __declspec(dllexport)\n#else\n#define DAO_AOT_EXPORT __attribute__((visibility(\"default\")))\n#endif\n"
           "enum { DAO_AOT_OK=0, DAO_AOT_BAD_ARGS=1, DAO_AOT_DIV_ZERO=7, DAO_AOT_OVERFLOW=8, DAO_AOT_THROW=9 };\n"
           "static int add64(int64_t a,int64_t b,int64_t*o){if((b>0&&a>INT64_MAX-b)||(b<0&&a<INT64_MIN-b))return 0;*o=a+b;return 1;}\n"
           "static int sub64(int64_t a,int64_t b,int64_t*o){if((b<0&&a>INT64_MAX+b)||(b>0&&a<INT64_MIN+b))return 0;*o=a-b;return 1;}\n"
           "static int mul64(int64_t a,int64_t b,int64_t*o){if(a==0||b==0){*o=0;return 1;}if((a==-1&&b==INT64_MIN)||(b==-1&&a==INT64_MIN))return 0;if(a>0?b>0?a>INT64_MAX/b:b<INT64_MIN/a:b>0?a<INT64_MIN/b:b<INT64_MAX/a)return 0;*o=a*b;return 1;}\n";
    out << "DAO_AOT_EXPORT int dao_aot_native_add_baseline(const int64_t*args,size_t argc,int64_t*out){if(!out||argc!=2||!args)return DAO_AOT_BAD_ARGS;return add64(args[0],args[1],out)?DAO_AOT_OK:DAO_AOT_OVERFLOW;}\n";
    out << "DAO_AOT_EXPORT int dao_aot_native_sum_baseline(const int64_t*args,size_t argc,int64_t*out){if(!out||argc!=1||!args)return DAO_AOT_BAD_ARGS;int64_t n=args[0],total=0,next;while(n>0){if(!add64(total,n,&next))return DAO_AOT_OVERFLOW;total=next;if(!sub64(n,1,&next))return DAO_AOT_OVERFLOW;n=next;}*out=total;return DAO_AOT_OK;}\n";
    for (const auto& fn : module.functions) {
        out << "static int dao_aot_impl_" << fn.index
            << "(const int64_t*,size_t,int64_t*,int64_t*);\n";
        out << "DAO_AOT_EXPORT int dao_aot_fn_" << fn.index << "(const int64_t*,size_t,int64_t*);\n";
    }
    for (const auto& fn : module.functions) {
        const size_t handler_capacity = fn.instructions.empty() ? 1 : fn.instructions.size();
        out << "static int dao_aot_impl_" << fn.index
            << "(const int64_t*args,size_t argc,int64_t*out,int64_t*thrown){\n"
            << "if(!out||!thrown||argc!=" << fn.parameter_count << "||(" << fn.parameter_count
            << "&& !args))return DAO_AOT_BAD_ARGS;\n"
            << "int64_t r[" << (fn.register_count == 0 ? 1 : fn.register_count)
            << "]={0};size_t handlers[" << handler_capacity
            << "]={0},hs=0,handler_target=0;for(size_t i=0;i<argc;i++)r[i]=args[i];\n";
        for (size_t pc = 0; pc < fn.instructions.size(); ++pc) {
            const auto& i = fn.instructions[pc];
            out << "L" << pc << ":;\n";
            using dao::Opcode;
            switch (i.opcode) {
            case Opcode::Nop: break;
            case Opcode::LoadNull: case Opcode::LoadI64: case Opcode::LoadTrit: out << "r[" << i.dst << "]=" << i.immediate << ";\n"; break;
            case Opcode::Move: out << "r[" << i.dst << "]=r[" << i.a << "];\n"; break;
            case Opcode::AddI64: out << "if(!add64(r["<<i.a<<"],r["<<i.b<<"],&r["<<i.dst<<"]))return DAO_AOT_OVERFLOW;\n"; break;
            case Opcode::SubI64: out << "if(!sub64(r["<<i.a<<"],r["<<i.b<<"],&r["<<i.dst<<"]))return DAO_AOT_OVERFLOW;\n"; break;
            case Opcode::MulI64: out << "if(!mul64(r["<<i.a<<"],r["<<i.b<<"],&r["<<i.dst<<"]))return DAO_AOT_OVERFLOW;\n"; break;
            case Opcode::DivI64: out << "if(!r["<<i.b<<"])return DAO_AOT_DIV_ZERO;if(r["<<i.a<<"]==INT64_MIN&&r["<<i.b<<"]==-1)return DAO_AOT_OVERFLOW;r["<<i.dst<<"]=r["<<i.a<<"]/r["<<i.b<<"];\n"; break;
            case Opcode::RemI64: out << "if(!r["<<i.b<<"])return DAO_AOT_DIV_ZERO;r["<<i.dst<<"]=(r["<<i.a<<"]==INT64_MIN&&r["<<i.b<<"]==-1)?0:r["<<i.a<<"]%r["<<i.b<<"];\n"; break;
            case Opcode::CompareEqI64: out << "r["<<i.dst<<"]=(r["<<i.a<<"]==r["<<i.b<<"])?1:-1;\n"; break;
            case Opcode::CompareNeI64: out << "r["<<i.dst<<"]=(r["<<i.a<<"]!=r["<<i.b<<"])?1:-1;\n"; break;
            case Opcode::CompareLtI64: out << "r["<<i.dst<<"]=(r["<<i.a<<"]<r["<<i.b<<"])?1:-1;\n"; break;
            case Opcode::CompareLeI64: out << "r["<<i.dst<<"]=(r["<<i.a<<"]<=r["<<i.b<<"])?1:-1;\n"; break;
            case Opcode::CompareGtI64: out << "r["<<i.dst<<"]=(r["<<i.a<<"]>r["<<i.b<<"])?1:-1;\n"; break;
            case Opcode::CompareGeI64: out << "r["<<i.dst<<"]=(r["<<i.a<<"]>=r["<<i.b<<"])?1:-1;\n"; break;
            case Opcode::TritNot: out << "r["<<i.dst<<"]=-r["<<i.a<<"];\n"; break;
            case Opcode::TritAnd: out << "r["<<i.dst<<"]=r["<<i.a<<"]<r["<<i.b<<"]?r["<<i.a<<"]:r["<<i.b<<"];\n"; break;
            case Opcode::TritOr: out << "r["<<i.dst<<"]=r["<<i.a<<"]>r["<<i.b<<"]?r["<<i.a<<"]:r["<<i.b<<"];\n"; break;
            case Opcode::BranchTritNegative: out << "if(r["<<i.a<<"]<0)goto L"<<i.immediate<<";\n"; break;
            case Opcode::BranchTritZero: out << "if(r["<<i.a<<"]==0)goto L"<<i.immediate<<";\n"; break;
            case Opcode::BranchTritPositive: out << "if(r["<<i.a<<"]>0)goto L"<<i.immediate<<";\n"; break;
            case Opcode::Jump: out << "goto L"<<i.immediate<<";\n"; break;
            case Opcode::Call:
                out << "{int64_t ca[" << (i.b == 0 ? 1 : i.b) << "];";
                for (uint16_t argument = 0; argument < i.b; ++argument) out << "ca["<<argument<<"]=r["<<(i.a + argument)<<"];";
                out << "int st=dao_aot_impl_"<<i.immediate<<"(ca,"<<i.b<<",&r["<<i.dst<<"],thrown);"
                       "if(st==DAO_AOT_THROW&&hs){handler_target=handlers[--hs];goto DAO_AOT_DISPATCH;}if(st)return st;}\n"; break;
            case Opcode::Return: out << "*out=r["<<i.a<<"];return DAO_AOT_OK;\n"; break;
            case Opcode::TryBegin:
                out << "if(hs>=" << handler_capacity << ")return DAO_AOT_BAD_ARGS;handlers[hs++]="
                    << i.immediate << ";\n"; break;
            case Opcode::TryEnd: out << "if(!hs)return DAO_AOT_BAD_ARGS;--hs;\n"; break;
            case Opcode::Throw:
                out << "*thrown=r[" << i.a
                    << "];if(!hs)return DAO_AOT_THROW;handler_target=handlers[--hs];goto DAO_AOT_DISPATCH;\n";
                break;
            case Opcode::Catch: out << "r[" << i.dst << "]=*thrown;*thrown=0;\n"; break;
            default: break;
            }
        }
        out << "goto DAO_AOT_FALLTHROUGH;DAO_AOT_DISPATCH:switch(handler_target){";
        for (const auto& instruction : fn.instructions) {
            if (instruction.opcode == dao::Opcode::TryBegin)
                out << "case " << instruction.immediate << ":goto L" << instruction.immediate << ";";
        }
        out << "default:return DAO_AOT_BAD_ARGS;}DAO_AOT_FALLTHROUGH:return DAO_AOT_BAD_ARGS;}\n"
            << "DAO_AOT_EXPORT int dao_aot_fn_" << fn.index
            << "(const int64_t*args,size_t argc,int64_t*out){int64_t thrown=0;return dao_aot_impl_"
            << fn.index << "(args,argc,out,&thrown);}\n";
    }
    return out ? EXIT_SUCCESS : EXIT_FAILURE;
}
