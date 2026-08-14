#include "dao/format.hpp"

#include <cstdlib>
#include <fstream>
#include <utility>

int main(int argc, char** argv) {
    if (argc != 2)
        return EXIT_FAILURE;

    dao::FunctionSpec function;
    function.register_count = 1;
    function.code = {
        {dao::Opcode::LoadI64, 0, 0, 0, 0, 0},
        {dao::Opcode::Return, 0, 0, 0, 0, 0},
    };

    dao::ModuleBuilder builder;
    const uint32_t function_index = builder.add_function(std::move(function));
    builder.add_export(1, function_index);
    const auto bytes = builder.encode();

    std::ofstream output(argv[1], std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output ? EXIT_SUCCESS : EXIT_FAILURE;
}
