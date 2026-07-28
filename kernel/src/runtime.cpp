#include "dao/dao.h"
#include "dao/format.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct FunctionRecord {
    uint32_t code_offset;
    uint32_t code_count;
    uint16_t register_count;
    uint16_t parameter_count;
    const dao::Instruction* instructions = nullptr;
};

struct SectionRecord {
    uint32_t type;
    uint32_t offset;
    uint32_t size;
    uint32_t count;
};

struct ImportRecord {
    uint32_t symbol_id;
    uint16_t parameter_count;
};

struct ModuleImportRecord {
    std::string module_name;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t symbol_id;
    uint16_t parameter_count;
};

struct ModuleKey {
    std::string name;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;

    bool operator==(const ModuleKey& other) const {
        return name == other.name && version_major == other.version_major &&
               version_minor == other.version_minor && version_patch == other.version_patch;
    }
};

struct ModuleKeyHash {
    size_t operator()(const ModuleKey& key) const {
        size_t hash = std::hash<std::string>{}(key.name);
        hash ^= static_cast<size_t>(key.version_major) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        hash ^= static_cast<size_t>(key.version_minor) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        hash ^= static_cast<size_t>(key.version_patch) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct HostFunction {
    uint32_t parameter_count;
    dao_host_callback callback;
    void* user_data;
};

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read_u32(const uint8_t* data) {
    uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(*data++) << shift;
    }
    return value;
}

int64_t read_i64(const uint8_t* data) {
    uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<uint64_t>(*data++) << shift;
    }
    return static_cast<int64_t>(value);
}

uint64_t fingerprint64(const uint8_t* data, size_t size) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void clear_error(dao_error* error) {
    if (error == nullptr)
        return;
    std::memset(error, 0, sizeof(*error));
    error->function_index = std::numeric_limits<uint32_t>::max();
    error->instruction_index = std::numeric_limits<uint32_t>::max();
}

dao_status fail(dao_error* error, dao_status status, const char* message,
                uint32_t function_index = std::numeric_limits<uint32_t>::max(),
                uint32_t instruction_index = std::numeric_limits<uint32_t>::max()) {
    if (error != nullptr) {
        clear_error(error);
        error->code = status;
        error->function_index = function_index;
        error->instruction_index = instruction_index;
        if (message != nullptr) {
            std::strncpy(error->message, message, sizeof(error->message) - 1);
            error->message[sizeof(error->message) - 1] = '\0';
        }
    }
    return status;
}

bool checked_add(int64_t left, int64_t right, int64_t& out) {
    if ((right > 0 && left > std::numeric_limits<int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<int64_t>::min() - right)) {
        return false;
    }
    out = left + right;
    return true;
}

bool checked_sub(int64_t left, int64_t right, int64_t& out) {
    if ((right < 0 && left > std::numeric_limits<int64_t>::max() + right) ||
        (right > 0 && left < std::numeric_limits<int64_t>::min() + right)) {
        return false;
    }
    out = left - right;
    return true;
}

bool checked_mul(int64_t left, int64_t right, int64_t& out) {
    if (left == 0 || right == 0) {
        out = 0;
        return true;
    }
    if ((left == -1 && right == std::numeric_limits<int64_t>::min()) ||
        (right == -1 && left == std::numeric_limits<int64_t>::min())) {
        return false;
    }
    if (left > 0) {
        if (right > 0 && left > std::numeric_limits<int64_t>::max() / right)
            return false;
        if (right < 0 && right < std::numeric_limits<int64_t>::min() / left)
            return false;
    } else {
        if (right > 0 && left < std::numeric_limits<int64_t>::min() / right)
            return false;
        if (right < 0 && left < std::numeric_limits<int64_t>::max() / right)
            return false;
    }
    out = left * right;
    return true;
}

dao_value null_value() { return dao_value{DAO_VALUE_NULL, 0, 0}; }

const uint8_t* view_data(const dao_value& value) {
    return reinterpret_cast<const uint8_t*>(static_cast<intptr_t>(value.payload));
}

bool is_valid_utf8(const uint8_t* data, size_t size) {
    size_t index = 0;
    while (index < size) {
        const uint8_t first = data[index++];
        if (first <= 0x7f)
            continue;

        size_t continuation_count = 0;
        uint8_t second_min = 0x80;
        uint8_t second_max = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation_count = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation_count = 2;
            if (first == 0xe0)
                second_min = 0xa0;
            if (first == 0xed)
                second_max = 0x9f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation_count = 3;
            if (first == 0xf0)
                second_min = 0x90;
            if (first == 0xf4)
                second_max = 0x8f;
        } else {
            return false;
        }

        if (continuation_count > size - index)
            return false;
        const uint8_t second = data[index++];
        if (second < second_min || second > second_max)
            return false;
        for (size_t continuation = 1; continuation < continuation_count; ++continuation) {
            const uint8_t byte = data[index++];
            if (byte < 0x80 || byte > 0xbf)
                return false;
        }
    }
    return true;
}

bool is_trit(const dao_value& value) {
    return value.type == DAO_VALUE_TRIT && value.payload >= -1 && value.payload <= 1;
}

bool is_valid_value(const dao_value& value) {
    if (value.type == DAO_VALUE_NULL)
        return value.reserved == 0 && value.payload == 0;
    if (value.type == DAO_VALUE_I64)
        return value.reserved == 0;
    if (value.type == DAO_VALUE_TRIT)
        return value.reserved == 0 && is_trit(value);
    if (value.type == DAO_VALUE_LIST)
        return value.reserved == 0 && value.payload != 0;
    if (value.type == DAO_VALUE_MAP)
        return value.reserved == 0 && value.payload != 0;
    if (value.type == DAO_VALUE_FUNCTION)
        return value.reserved == 0 && value.payload >= 0;
    if (value.type == DAO_VALUE_CLOSURE)
        return value.reserved == 0 && value.payload != 0;
    if (value.type != DAO_VALUE_BYTES && value.type != DAO_VALUE_STRING)
        return false;

    const uint8_t* data = view_data(value);
    const size_t size = value.reserved;
    if (size != 0 && data == nullptr)
        return false;
    if (value.type == DAO_VALUE_BYTES || size == 0)
        return true;
    return is_valid_utf8(data, size);
}

dao_status make_view(dao_bytes bytes, uint32_t type, dao_value* out_value) {
    if (out_value == nullptr || (bytes.size != 0 && bytes.data == nullptr) ||
        bytes.size > std::numeric_limits<uint32_t>::max()) {
        return DAO_INVALID_ARGUMENT;
    }
    if (type == DAO_VALUE_STRING && bytes.size != 0 && !is_valid_utf8(bytes.data, bytes.size))
        return DAO_TYPE_ERROR;
    *out_value = dao_value{type, static_cast<uint32_t>(bytes.size),
                           static_cast<int64_t>(reinterpret_cast<intptr_t>(bytes.data))};
    return DAO_OK;
}

} // namespace

struct dao_vm {
    struct List { std::vector<dao_value> values; };
    struct Map { std::unordered_map<std::string, dao_value> values; };
    struct Closure { uint32_t function_index; std::vector<dao_value> captured; };
    dao_vm_config config;
    std::unordered_map<uint32_t, HostFunction> host_functions;
    std::vector<std::unique_ptr<List>> lists;
    std::vector<std::unique_ptr<Map>> maps;
    std::vector<std::unique_ptr<Closure>> closures;
    mutable std::mutex cache_mutex;
    std::unordered_map<uint64_t, std::vector<dao_module*>> module_cache;
    std::unordered_map<ModuleKey, dao_module*, ModuleKeyHash> linked_modules;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    uint32_t max_cached_modules = 64;
    uint32_t container_generation = 1;
    uint32_t host_callback_depth = 0;
};

struct dao_module {
    std::atomic<uint32_t> references{1};
    std::vector<FunctionRecord> functions;
    std::vector<dao::Instruction> code;
    std::vector<ImportRecord> imports;
    std::vector<ModuleImportRecord> module_imports;
    std::unordered_map<uint32_t, uint32_t> exports;
    std::vector<std::string> strings;
    uint64_t fingerprint = 0;
    std::vector<uint8_t> source_bytes;
    bool has_identity = false;
    ModuleKey identity{};
};

namespace {

int64_t container_handle(uint32_t generation, size_t index) {
    return static_cast<int64_t>((static_cast<uint64_t>(generation) << 32) |
                                static_cast<uint32_t>(index + 1));
}

template <typename T>
const T* resolve_container(const std::vector<std::unique_ptr<T>>& values, uint32_t generation,
                           const dao_value& value) {
    const uint64_t handle = static_cast<uint64_t>(value.payload);
    if (static_cast<uint32_t>(handle >> 32) != generation) return nullptr;
    const uint32_t encoded_index = static_cast<uint32_t>(handle);
    if (encoded_index == 0 || encoded_index > values.size()) return nullptr;
    return values[encoded_index - 1].get();
}

template <typename T>
T* resolve_container(std::vector<std::unique_ptr<T>>& values, uint32_t generation,
                     const dao_value& value) {
    const uint64_t handle = static_cast<uint64_t>(value.payload);
    if (static_cast<uint32_t>(handle >> 32) != generation) return nullptr;
    const uint32_t encoded_index = static_cast<uint32_t>(handle);
    if (encoded_index == 0 || encoded_index > values.size()) return nullptr;
    return values[encoded_index - 1].get();
}

bool is_valid_vm_value(const dao_vm* vm, const dao_value& value) {
    if (!is_valid_value(value)) return false;
    if (value.type == DAO_VALUE_LIST)
        return resolve_container(vm->lists, vm->container_generation, value) != nullptr;
    if (value.type == DAO_VALUE_MAP)
        return resolve_container(vm->maps, vm->container_generation, value) != nullptr;
    if (value.type == DAO_VALUE_CLOSURE)
        return resolve_container(vm->closures, vm->container_generation, value) != nullptr;
    return true;
}

bool is_host_storable_value(const dao_vm* vm, const dao_value& value) {
    if (!is_valid_vm_value(vm, value)) return false;
    return value.type != DAO_VALUE_FUNCTION && value.type != DAO_VALUE_CLOSURE;
}

bool is_module_boundary_value(const dao_vm* vm, const dao_value& value, uint32_t depth = 0) {
    if (!is_valid_vm_value(vm, value) || value.type == DAO_VALUE_FUNCTION ||
        value.type == DAO_VALUE_CLOSURE || depth > 64)
        return false;
    if (value.type == DAO_VALUE_LIST) {
        const auto* list = resolve_container(vm->lists, vm->container_generation, value);
        return list != nullptr &&
               std::all_of(list->values.begin(), list->values.end(), [&](const dao_value& item) {
                   return is_module_boundary_value(vm, item, depth + 1);
               });
    }
    if (value.type == DAO_VALUE_MAP) {
        const auto* map = resolve_container(vm->maps, vm->container_generation, value);
        return map != nullptr &&
               std::all_of(map->values.begin(), map->values.end(), [&](const auto& item) {
                   return is_module_boundary_value(vm, item.second, depth + 1);
               });
    }
    return true;
}

const SectionRecord* find_section(const std::vector<SectionRecord>& sections,
                                  dao::SectionType type) {
    const uint32_t wanted = static_cast<uint32_t>(type);
    const auto found =
        std::find_if(sections.begin(), sections.end(),
                     [wanted](const SectionRecord& section) { return section.type == wanted; });
    return found == sections.end() ? nullptr : &*found;
}

dao_status verify_instruction(const dao_module& module, const FunctionRecord& function,
                              const dao::Instruction& instruction, uint32_t function_index,
                              uint32_t pc, dao_error* error) {
    if (instruction.flags != 0) {
        return fail(error, DAO_VERIFY_ERROR, "instruction flags must be zero",
                    function_index, pc);
    }

    const auto valid_register = [&function](uint16_t index) {
        return index < function.register_count;
    };
    const auto valid_target = [&function](int64_t target) {
        return target >= 0 && static_cast<uint64_t>(target) < function.code_count;
    };

    using dao::Opcode;
    switch (instruction.opcode) {
    case Opcode::Nop:
        return DAO_OK;
    case Opcode::LoadI64:
    case Opcode::LoadTrit:
        if (valid_register(instruction.dst))
            if (instruction.opcode != Opcode::LoadTrit ||
                (instruction.immediate >= -1 && instruction.immediate <= 1)) return DAO_OK;
        break;
    case Opcode::LoadString:
        if (valid_register(instruction.dst) && instruction.immediate >= 0 &&
            static_cast<uint64_t>(instruction.immediate) < module.strings.size()) return DAO_OK;
        break;
    case Opcode::LoadFunction:
        if (valid_register(instruction.dst) && instruction.immediate >= 0 &&
            static_cast<uint64_t>(instruction.immediate) < module.functions.size()) return DAO_OK;
        break;
    case Opcode::MakeClosure: {
        const uint64_t end = static_cast<uint64_t>(instruction.a) + instruction.b;
        if (valid_register(instruction.dst) && end <= function.register_count &&
            instruction.immediate >= 0 &&
            static_cast<uint64_t>(instruction.immediate) < module.functions.size()) return DAO_OK;
        break;
    }
    case Opcode::LoadNull:
        if (valid_register(instruction.dst)) return DAO_OK;
        break;
    case Opcode::MakeList: {
        const uint32_t end = static_cast<uint32_t>(instruction.a) + instruction.b;
        if (valid_register(instruction.dst) && end <= function.register_count) return DAO_OK;
        break;
    }
    case Opcode::MakeMap: {
        const uint32_t end = static_cast<uint32_t>(instruction.a) + static_cast<uint32_t>(instruction.b) * 2u;
        if (valid_register(instruction.dst) && end <= function.register_count) return DAO_OK;
        break;
    }
    case Opcode::ListLength:
        if (valid_register(instruction.dst) && valid_register(instruction.a)) return DAO_OK;
        break;
    case Opcode::ListAppend:
        if (valid_register(instruction.dst) && valid_register(instruction.a)) return DAO_OK;
        break;
    case Opcode::ListGet:
    case Opcode::IndexGet:
    case Opcode::IndexSet:
        if (valid_register(instruction.dst) && valid_register(instruction.a) && valid_register(instruction.b)) return DAO_OK;
        break;
    case Opcode::TryBegin:
        if (valid_target(instruction.immediate)) return DAO_OK;
        break;
    case Opcode::TryEnd:
        return DAO_OK;
    case Opcode::Throw:
        if (valid_register(instruction.a)) return DAO_OK;
        break;
    case Opcode::Catch:
        if (valid_register(instruction.dst)) return DAO_OK;
        break;
    case Opcode::Move:
    case Opcode::TritNot:
        if (valid_register(instruction.dst) && valid_register(instruction.a))
            return DAO_OK;
        break;
    case Opcode::AddI64:
    case Opcode::SubI64:
    case Opcode::MulI64:
    case Opcode::DivI64:
    case Opcode::RemI64:
    case Opcode::CompareEqI64:
    case Opcode::CompareNeI64:
    case Opcode::CompareLtI64:
    case Opcode::CompareLeI64:
    case Opcode::CompareGtI64:
    case Opcode::CompareGeI64:
    case Opcode::TritAnd:
    case Opcode::TritOr:
        if (valid_register(instruction.dst) && valid_register(instruction.a) &&
            valid_register(instruction.b)) {
            return DAO_OK;
        }
        break;
    case Opcode::BranchTritNegative:
    case Opcode::BranchTritZero:
    case Opcode::BranchTritPositive:
        if (valid_register(instruction.a) && valid_target(instruction.immediate))
            return DAO_OK;
        break;
    case Opcode::Jump:
        if (valid_target(instruction.immediate))
            return DAO_OK;
        break;
    case Opcode::Call: {
        if (!valid_register(instruction.dst) || instruction.immediate < 0 ||
            static_cast<uint64_t>(instruction.immediate) >= module.functions.size()) {
            break;
        }
        const uint32_t end =
            static_cast<uint32_t>(instruction.a) + static_cast<uint32_t>(instruction.b);
        const auto& callee = module.functions[static_cast<size_t>(instruction.immediate)];
        if (end <= function.register_count && instruction.b == callee.parameter_count)
            return DAO_OK;
        break;
    }
    case Opcode::CallValue: {
        if (!valid_register(instruction.dst) || !valid_register(instruction.a) ||
            instruction.immediate < 0) break;
        const uint64_t end = static_cast<uint64_t>(instruction.b) +
                             static_cast<uint64_t>(instruction.immediate);
        if (end <= function.register_count) return DAO_OK;
        break;
    }
    case Opcode::CallHost: {
        if (!valid_register(instruction.dst) || instruction.immediate < 0 ||
            static_cast<uint64_t>(instruction.immediate) >= module.imports.size()) {
            break;
        }
        const uint32_t end =
            static_cast<uint32_t>(instruction.a) + static_cast<uint32_t>(instruction.b);
        const auto& import = module.imports[static_cast<size_t>(instruction.immediate)];
        if (end <= function.register_count && instruction.b == import.parameter_count)
            return DAO_OK;
        break;
    }
    case Opcode::CallModule: {
        if (!valid_register(instruction.dst) || instruction.immediate < 0 ||
            static_cast<uint64_t>(instruction.immediate) >= module.module_imports.size()) {
            break;
        }
        const uint32_t end =
            static_cast<uint32_t>(instruction.a) + static_cast<uint32_t>(instruction.b);
        const auto& import =
            module.module_imports[static_cast<size_t>(instruction.immediate)];
        if (end <= function.register_count && instruction.b == import.parameter_count)
            return DAO_OK;
        break;
    }
    case Opcode::Return:
        if (valid_register(instruction.a))
            return DAO_OK;
        break;
    default:
        return fail(error, DAO_VERIFY_ERROR, "unknown opcode", function_index, pc);
    }
    return fail(error, DAO_VERIFY_ERROR, "invalid instruction operand", function_index, pc);
}

dao_status execute_function(dao_vm* vm, const dao_module* module, uint32_t function_index,
                            const dao_value* args, size_t arg_count, uint32_t depth,
                            uint64_t& budget, dao_value* out, dao_value* thrown, dao_error* error) {
    if (depth >= vm->config.max_call_depth) {
        return fail(error, DAO_CALL_DEPTH_EXCEEDED, "maximum call depth exceeded", function_index);
    }
    if (function_index >= module->functions.size()) {
        return fail(error, DAO_RUNTIME_ERROR, "function index out of range", function_index);
    }

    const auto& function = module->functions[function_index];
    if (arg_count != function.parameter_count) {
        return fail(error, DAO_INVALID_ARGUMENT, "argument count does not match function signature",
                    function_index);
    }

    std::vector<dao_value> registers(function.register_count, null_value());
    for (size_t index = 0; index < arg_count; ++index)
        registers[index] = args[index];

    uint32_t pc = 0;
    std::vector<uint32_t> handlers;
    while (pc < function.code_count) {
        if (budget == 0) {
            return fail(error, DAO_INSTRUCTION_LIMIT_EXCEEDED, "instruction budget exhausted",
                        function_index, pc);
        }
        --budget;

        const auto& instruction = function.instructions[pc];
        const auto require_i64 = [&](uint16_t index) {
            return registers[index].type == DAO_VALUE_I64;
        };

        using dao::Opcode;
        switch (instruction.opcode) {
        case Opcode::Nop:
            ++pc;
            break;
        case Opcode::LoadI64:
            registers[instruction.dst] = dao_value{DAO_VALUE_I64, 0, instruction.immediate};
            ++pc;
            break;
        case Opcode::LoadTrit:
            registers[instruction.dst] = dao_value{DAO_VALUE_TRIT, 0, instruction.immediate};
            ++pc;
            break;
        case Opcode::LoadString: {
            const auto& value = module->strings[static_cast<size_t>(instruction.immediate)];
            registers[instruction.dst] = dao_value{DAO_VALUE_STRING, static_cast<uint32_t>(value.size()),
                static_cast<int64_t>(reinterpret_cast<intptr_t>(value.data()))};
            ++pc; break;
        }
        case Opcode::LoadFunction:
            registers[instruction.dst] = dao_value{DAO_VALUE_FUNCTION, 0, instruction.immediate};
            ++pc;
            break;
        case Opcode::MakeClosure: {
            auto closure = std::make_unique<dao_vm::Closure>();
            closure->function_index = static_cast<uint32_t>(instruction.immediate);
            closure->captured.assign(registers.begin() + instruction.a,
                                     registers.begin() + instruction.a + instruction.b);
            vm->closures.push_back(std::move(closure));
            registers[instruction.dst] = dao_value{DAO_VALUE_CLOSURE, 0,
                container_handle(vm->container_generation, vm->closures.size() - 1)};
            ++pc;
            break;
        }
        case Opcode::LoadNull:
            registers[instruction.dst] = null_value(); ++pc; break;
        case Opcode::MakeList: {
            auto list = std::make_unique<dao_vm::List>();
            list->values.assign(registers.begin() + instruction.a, registers.begin() + instruction.a + instruction.b);
            vm->lists.push_back(std::move(list));
            registers[instruction.dst] = dao_value{DAO_VALUE_LIST, 0, container_handle(vm->container_generation, vm->lists.size() - 1)};
            ++pc; break;
        }
        case Opcode::MakeMap: {
            auto map = std::make_unique<dao_vm::Map>();
            for (uint16_t i = 0; i < instruction.b; ++i) {
                const dao_value& key = registers[instruction.a + i * 2]; const dao_value& value = registers[instruction.a + i * 2 + 1];
                if (key.type != DAO_VALUE_STRING) return fail(error, DAO_TYPE_ERROR, "map keys must be strings", function_index, pc);
                map->values[std::string(reinterpret_cast<const char*>(view_data(key)), key.reserved)] = value;
            }
            vm->maps.push_back(std::move(map));
            registers[instruction.dst] = dao_value{DAO_VALUE_MAP, 0, container_handle(vm->container_generation, vm->maps.size() - 1)}; ++pc; break;
        }
        case Opcode::ListLength: {
            if (registers[instruction.a].type != DAO_VALUE_LIST) return fail(error, DAO_TYPE_ERROR, "LIST_LEN requires a list", function_index, pc);
            const auto* list = resolve_container(vm->lists, vm->container_generation, registers[instruction.a]);
            if (list == nullptr) return fail(error, DAO_RUNTIME_ERROR, "stale list handle", function_index, pc);
            registers[instruction.dst] = dao_value{DAO_VALUE_I64, 0, static_cast<int64_t>(list->values.size())}; ++pc; break;
        }
        case Opcode::ListAppend: {
            if (registers[instruction.dst].type != DAO_VALUE_LIST)
                return fail(error, DAO_TYPE_ERROR, "LIST_APPEND requires a list", function_index, pc);
            auto* list = const_cast<dao_vm::List*>(
                resolve_container(vm->lists, vm->container_generation, registers[instruction.dst]));
            if (list == nullptr)
                return fail(error, DAO_RUNTIME_ERROR, "stale list handle", function_index, pc);
            list->values.push_back(registers[instruction.a]);
            ++pc;
            break;
        }
        case Opcode::ListGet: {
            if (registers[instruction.a].type != DAO_VALUE_LIST || !require_i64(instruction.b)) return fail(error, DAO_TYPE_ERROR, "LIST_GET requires list and i64 index", function_index, pc);
            const auto* list = resolve_container(vm->lists, vm->container_generation, registers[instruction.a]);
            if (list == nullptr) return fail(error, DAO_RUNTIME_ERROR, "stale list handle", function_index, pc);
            const int64_t index = registers[instruction.b].payload;
            if (index < 0 || static_cast<uint64_t>(index) >= list->values.size()) return fail(error, DAO_RUNTIME_ERROR, "list index out of range", function_index, pc);
            registers[instruction.dst] = list->values[static_cast<size_t>(index)]; ++pc; break;
        }
        case Opcode::IndexGet: {
            const dao_value& object = registers[instruction.a]; const dao_value& key = registers[instruction.b];
            if (object.type == DAO_VALUE_LIST) {
                if (key.type != DAO_VALUE_I64) return fail(error, DAO_TYPE_ERROR, "list index must be i64", function_index, pc);
                const auto* list = resolve_container(vm->lists, vm->container_generation, object); if (list == nullptr) return fail(error, DAO_RUNTIME_ERROR, "stale list handle", function_index, pc); const int64_t index = key.payload;
                if (index < 0 || static_cast<uint64_t>(index) >= list->values.size()) return fail(error, DAO_RUNTIME_ERROR, "list index out of range", function_index, pc);
                registers[instruction.dst] = list->values[static_cast<size_t>(index)];
            } else if (object.type == DAO_VALUE_MAP) {
                if (key.type != DAO_VALUE_STRING) return fail(error, DAO_TYPE_ERROR, "map key must be string", function_index, pc);
                const auto* map = resolve_container(vm->maps, vm->container_generation, object); if (map == nullptr) return fail(error, DAO_RUNTIME_ERROR, "stale map handle", function_index, pc);
                const auto found = map->values.find(std::string(reinterpret_cast<const char*>(view_data(key)), key.reserved));
                registers[instruction.dst] = found == map->values.end() ? null_value() : found->second;
            } else return fail(error, DAO_TYPE_ERROR, "indexing requires list or map", function_index, pc);
            ++pc; break;
        }
        case Opcode::IndexSet: {
            dao_value& object = registers[instruction.dst]; const dao_value& key = registers[instruction.a]; const dao_value& value = registers[instruction.b];
            if (object.type == DAO_VALUE_LIST) {
                if (key.type != DAO_VALUE_I64) return fail(error, DAO_TYPE_ERROR, "list index must be i64", function_index, pc);
                auto* list = const_cast<dao_vm::List*>(resolve_container(vm->lists, vm->container_generation, object)); const int64_t index = key.payload;
                if (list == nullptr) return fail(error, DAO_RUNTIME_ERROR, "stale list handle", function_index, pc);
                if (index < 0 || static_cast<uint64_t>(index) >= list->values.size()) return fail(error, DAO_RUNTIME_ERROR, "list index out of range", function_index, pc);
                list->values[static_cast<size_t>(index)] = value;
            } else if (object.type == DAO_VALUE_MAP) {
                if (key.type != DAO_VALUE_STRING) return fail(error, DAO_TYPE_ERROR, "map key must be string", function_index, pc);
                auto* map = const_cast<dao_vm::Map*>(resolve_container(vm->maps, vm->container_generation, object));
                if (map == nullptr) return fail(error, DAO_RUNTIME_ERROR, "stale map handle", function_index, pc);
                map->values[std::string(reinterpret_cast<const char*>(view_data(key)), key.reserved)] = value;
            } else return fail(error, DAO_TYPE_ERROR, "index assignment requires list or map", function_index, pc);
            ++pc; break;
        }
        case Opcode::TryBegin:
            handlers.push_back(static_cast<uint32_t>(instruction.immediate)); ++pc; break;
        case Opcode::TryEnd:
            if (handlers.empty()) return fail(error, DAO_RUNTIME_ERROR, "TRY_END without handler", function_index, pc);
            handlers.pop_back(); ++pc; break;
        case Opcode::Throw:
            *thrown = registers[instruction.a];
            if (handlers.empty()) return DAO_RUNTIME_ERROR;
            pc = handlers.back(); handlers.pop_back(); break;
        case Opcode::Catch:
            registers[instruction.dst] = *thrown; *thrown = null_value(); ++pc; break;
        case Opcode::Move:
            registers[instruction.dst] = registers[instruction.a];
            ++pc;
            break;
        case Opcode::AddI64:
        case Opcode::SubI64:
        case Opcode::MulI64:
        case Opcode::DivI64:
        case Opcode::RemI64:
        case Opcode::CompareEqI64:
        case Opcode::CompareNeI64:
        case Opcode::CompareLtI64:
        case Opcode::CompareLeI64:
        case Opcode::CompareGtI64:
        case Opcode::CompareGeI64: {
            const bool equality = instruction.opcode == Opcode::CompareEqI64 ||
                                  instruction.opcode == Opcode::CompareNeI64;
            const dao_value& left_value = registers[instruction.a];
            const dao_value& right_value = registers[instruction.b];
            const bool left_null = registers[instruction.a].type == DAO_VALUE_NULL;
            const bool right_null = registers[instruction.b].type == DAO_VALUE_NULL;
            if (equality && (left_null || right_null)) {
                const bool equal = left_null && right_null;
                const bool truth = instruction.opcode == Opcode::CompareEqI64 ? equal : !equal;
                registers[instruction.dst] = dao_value{DAO_VALUE_TRIT, 0, truth ? 1 : -1};
                ++pc;
                break;
            }
            if (equality && left_value.type != right_value.type) {
                const bool truth = instruction.opcode == Opcode::CompareNeI64;
                registers[instruction.dst] = dao_value{DAO_VALUE_TRIT, 0, truth ? 1 : -1};
                ++pc;
                break;
            }
            if (equality && (left_value.type == DAO_VALUE_STRING ||
                             left_value.type == DAO_VALUE_BYTES)) {
                const bool equal = left_value.reserved == right_value.reserved &&
                    std::memcmp(view_data(left_value), view_data(right_value), left_value.reserved) == 0;
                const bool truth = instruction.opcode == Opcode::CompareEqI64 ? equal : !equal;
                registers[instruction.dst] = dao_value{DAO_VALUE_TRIT, 0, truth ? 1 : -1};
                ++pc;
                break;
            }
            if (equality && left_value.type == DAO_VALUE_TRIT) {
                const bool equal = left_value.payload == right_value.payload;
                const bool truth = instruction.opcode == Opcode::CompareEqI64 ? equal : !equal;
                registers[instruction.dst] = dao_value{DAO_VALUE_TRIT, 0, truth ? 1 : -1};
                ++pc;
                break;
            }
            if (!require_i64(instruction.a) || !require_i64(instruction.b)) {
                return fail(error, DAO_TYPE_ERROR, "integer opcode requires i64 operands",
                            function_index, pc);
            }
            const int64_t left = registers[instruction.a].payload;
            const int64_t right = registers[instruction.b].payload;
            int64_t result = 0;
            bool valid = true;
            if (instruction.opcode == Opcode::AddI64)
                valid = checked_add(left, right, result);
            if (instruction.opcode == Opcode::SubI64)
                valid = checked_sub(left, right, result);
            if (instruction.opcode == Opcode::MulI64)
                valid = checked_mul(left, right, result);
            if (instruction.opcode == Opcode::DivI64) {
                if (right == 0) {
                    return fail(error, DAO_DIVIDE_BY_ZERO, "integer division by zero",
                                function_index, pc);
                }
                if (left == std::numeric_limits<int64_t>::min() && right == -1)
                    valid = false;
                else
                    result = left / right;
            }
            if (instruction.opcode == Opcode::RemI64) {
                if (right == 0) return fail(error, DAO_DIVIDE_BY_ZERO, "integer remainder by zero", function_index, pc);
                result = left == std::numeric_limits<int64_t>::min() && right == -1 ? 0 : left % right;
            }
            const bool comparison = instruction.opcode >= Opcode::CompareEqI64 &&
                                    instruction.opcode <= Opcode::CompareGeI64;
            if (comparison) {
                bool truth = false;
                if (instruction.opcode == Opcode::CompareEqI64) truth = left == right;
                if (instruction.opcode == Opcode::CompareNeI64) truth = left != right;
                if (instruction.opcode == Opcode::CompareLtI64) truth = left < right;
                if (instruction.opcode == Opcode::CompareLeI64) truth = left <= right;
                if (instruction.opcode == Opcode::CompareGtI64) truth = left > right;
                if (instruction.opcode == Opcode::CompareGeI64) truth = left >= right;
                registers[instruction.dst] = dao_value{DAO_VALUE_TRIT, 0, truth ? 1 : -1};
                ++pc;
                break;
            }
            if (!valid) {
                return fail(error, DAO_INTEGER_OVERFLOW, "integer overflow", function_index, pc);
            }
            registers[instruction.dst] = dao_value{DAO_VALUE_I64, 0, result};
            ++pc;
            break;
        }
        case Opcode::TritNot:
            if (!is_trit(registers[instruction.a])) {
                return fail(error, DAO_TYPE_ERROR, "TRIT_NOT requires a trit", function_index, pc);
            }
            registers[instruction.dst] =
                dao_value{DAO_VALUE_TRIT, 0, -registers[instruction.a].payload};
            ++pc;
            break;
        case Opcode::TritAnd:
        case Opcode::TritOr:
            if (!is_trit(registers[instruction.a]) || !is_trit(registers[instruction.b])) {
                return fail(error, DAO_TYPE_ERROR, "trit opcode requires trit operands",
                            function_index, pc);
            }
            registers[instruction.dst] = dao_value{
                DAO_VALUE_TRIT, 0,
                instruction.opcode == Opcode::TritAnd
                    ? std::min(registers[instruction.a].payload, registers[instruction.b].payload)
                    : std::max(registers[instruction.a].payload, registers[instruction.b].payload)};
            ++pc;
            break;
        case Opcode::BranchTritNegative:
        case Opcode::BranchTritZero:
        case Opcode::BranchTritPositive: {
            if (!is_trit(registers[instruction.a])) {
                return fail(error, DAO_TYPE_ERROR, "trit branch requires a trit", function_index,
                            pc);
            }
            const int64_t value = registers[instruction.a].payload;
            const bool take = (instruction.opcode == Opcode::BranchTritNegative && value < 0) ||
                              (instruction.opcode == Opcode::BranchTritZero && value == 0) ||
                              (instruction.opcode == Opcode::BranchTritPositive && value > 0);
            pc = take ? static_cast<uint32_t>(instruction.immediate) : pc + 1;
            break;
        }
        case Opcode::Jump:
            pc = static_cast<uint32_t>(instruction.immediate);
            break;
        case Opcode::Call: {
            dao_value result = null_value();
            const dao_status status =
                execute_function(vm, module, static_cast<uint32_t>(instruction.immediate),
                                 instruction.b == 0 ? nullptr : registers.data() + instruction.a,
                                 instruction.b, depth + 1, budget, &result, thrown, error);
            if (status == DAO_RUNTIME_ERROR && thrown->type != DAO_VALUE_NULL && !handlers.empty()) { pc = handlers.back(); handlers.pop_back(); break; }
            if (status != DAO_OK)
                return status;
            registers[instruction.dst] = result;
            ++pc;
            break;
        }
        case Opcode::CallModule: {
            const auto& import =
                module->module_imports[static_cast<size_t>(instruction.immediate)];
            const ModuleKey key{import.module_name, import.version_major, import.version_minor,
                                import.version_patch};
            const auto linked = vm->linked_modules.find(key);
            if (linked == vm->linked_modules.end())
                return fail(error, DAO_IMPORT_NOT_FOUND, "module import is not linked",
                            function_index, pc);
            const dao_module* target_module = linked->second;
            const auto target_export = target_module->exports.find(import.symbol_id);
            if (target_export == target_module->exports.end())
                return fail(error, DAO_EXPORT_NOT_FOUND, "module export is not available",
                            function_index, pc);
            const auto& target_function = target_module->functions[target_export->second];
            if (target_function.parameter_count != import.parameter_count)
                return fail(error, DAO_MODULE_VERSION_MISMATCH,
                            "module import signature does not match linked export",
                            function_index, pc);
            const dao_value* arguments =
                instruction.b == 0 ? nullptr : registers.data() + instruction.a;
            for (uint16_t index = 0; index < instruction.b; ++index) {
                if (!is_module_boundary_value(vm, arguments[index]))
                    return fail(error, DAO_TYPE_ERROR,
                                "module arguments contain module-local values", function_index,
                                pc);
            }
            dao_value result = null_value();
            const dao_status status = execute_function(
                vm, target_module, target_export->second, arguments, instruction.b, depth + 1,
                budget, &result, thrown, error);
            if (status == DAO_RUNTIME_ERROR && thrown->type != DAO_VALUE_NULL &&
                !handlers.empty()) {
                pc = handlers.back();
                handlers.pop_back();
                break;
            }
            if (status != DAO_OK)
                return status;
            if (!is_module_boundary_value(vm, result))
                return fail(error, DAO_TYPE_ERROR,
                            "module result contains a module-local value", function_index, pc);
            registers[instruction.dst] = result;
            ++pc;
            break;
        }
        case Opcode::CallValue: {
            const dao_value& target = registers[instruction.a];
            uint32_t target_index = 0;
            std::vector<dao_value> arguments;
            if (target.type == DAO_VALUE_FUNCTION && target.payload >= 0 &&
                static_cast<uint64_t>(target.payload) < module->functions.size()) {
                target_index = static_cast<uint32_t>(target.payload);
            } else if (target.type == DAO_VALUE_CLOSURE) {
                const auto* closure = resolve_container(vm->closures, vm->container_generation, target);
                if (closure == nullptr) return fail(error, DAO_RUNTIME_ERROR, "stale closure handle", function_index, pc);
                target_index = closure->function_index;
                arguments = closure->captured;
            } else {
                return fail(error, DAO_TYPE_ERROR, "CALL_VALUE requires a local function reference",
                            function_index, pc);
            }
            arguments.insert(arguments.end(), registers.begin() + instruction.b,
                             registers.begin() + instruction.b + instruction.immediate);
            dao_value result = null_value();
            const dao_status status = execute_function(
                vm, module, target_index, arguments.empty() ? nullptr : arguments.data(),
                arguments.size(), depth + 1, budget, &result, thrown, error);
            if (status == DAO_RUNTIME_ERROR && thrown->type != DAO_VALUE_NULL && !handlers.empty()) {
                pc = handlers.back(); handlers.pop_back(); break;
            }
            if (status != DAO_OK) return status;
            registers[instruction.dst] = result;
            ++pc;
            break;
        }
        case Opcode::CallHost: {
            const auto& import = module->imports[static_cast<size_t>(instruction.immediate)];
            const auto found = vm->host_functions.find(import.symbol_id);
            if (found == vm->host_functions.end()) {
                return fail(error, DAO_IMPORT_NOT_FOUND, "host import is not registered",
                            function_index, pc);
            }
            const HostFunction& host = found->second;
            if (host.parameter_count != import.parameter_count) {
                return fail(error, DAO_TYPE_ERROR, "host import signature does not match module",
                            function_index, pc);
            }

            dao_value result = null_value();
            dao_status status = DAO_RUNTIME_ERROR;
            ++vm->host_callback_depth;
            try {
                status = host.callback(
                    host.user_data, instruction.b == 0 ? nullptr : registers.data() + instruction.a,
                    instruction.b, &result);
            } catch (...) {
                --vm->host_callback_depth;
                return fail(error, DAO_RUNTIME_ERROR, "host callback threw an exception",
                            function_index, pc);
            }
            --vm->host_callback_depth;
            if (status != DAO_OK) {
                if (status < DAO_INVALID_ARGUMENT || status > DAO_IMPORT_NOT_FOUND)
                    status = DAO_RUNTIME_ERROR;
                return fail(error, status, "host callback returned an error", function_index, pc);
            }
            if (!is_valid_value(result)) {
                return fail(error, DAO_TYPE_ERROR, "host callback returned an invalid value",
                            function_index, pc);
            }
            if ((result.type == DAO_VALUE_LIST || result.type == DAO_VALUE_MAP) &&
                !is_valid_vm_value(vm, result)) {
                return fail(error, DAO_TYPE_ERROR, "host callback returned a foreign container",
                            function_index, pc);
            }
            if (result.type == DAO_VALUE_FUNCTION || result.type == DAO_VALUE_CLOSURE) {
                return fail(error, DAO_TYPE_ERROR, "host callbacks cannot manufacture callable values",
                            function_index, pc);
            }
            registers[instruction.dst] = result;
            ++pc;
            break;
        }
        case Opcode::Return:
            *out = registers[instruction.a];
            return DAO_OK;
        default:
            return fail(error, DAO_RUNTIME_ERROR, "unknown opcode reached execution",
                        function_index, pc);
        }
    }

    return fail(error, DAO_RUNTIME_ERROR, "function ended without RETURN", function_index, pc);
}

ModuleKey module_key(const ModuleImportRecord& import) {
    return ModuleKey{import.module_name, import.version_major, import.version_minor,
                     import.version_patch};
}

dao_status validate_resolved_module_imports(const dao_vm* vm, dao_error* error) {
    for (const auto& linked : vm->linked_modules) {
        for (const ModuleImportRecord& import : linked.second->module_imports) {
            const auto target = vm->linked_modules.find(module_key(import));
            if (target == vm->linked_modules.end())
                continue;
            const auto exported = target->second->exports.find(import.symbol_id);
            if (exported == target->second->exports.end())
                return fail(error, DAO_EXPORT_NOT_FOUND,
                            "linked module does not provide an imported symbol");
            if (target->second->functions[exported->second].parameter_count !=
                import.parameter_count)
                return fail(error, DAO_MODULE_VERSION_MISMATCH,
                            "linked module export signature does not match import");
        }
    }
    return DAO_OK;
}

bool visit_module(const dao_vm* vm, const ModuleKey& key,
                  std::unordered_map<ModuleKey, uint8_t, ModuleKeyHash>& colors) {
    uint8_t& color = colors[key];
    if (color == 1)
        return false;
    if (color == 2)
        return true;
    color = 1;
    const auto linked = vm->linked_modules.find(key);
    if (linked != vm->linked_modules.end()) {
        for (const ModuleImportRecord& import : linked->second->module_imports) {
            const ModuleKey dependency = module_key(import);
            if (vm->linked_modules.contains(dependency) && !visit_module(vm, dependency, colors))
                return false;
        }
    }
    color = 2;
    return true;
}

bool linked_module_graph_is_acyclic(const dao_vm* vm) {
    std::unordered_map<ModuleKey, uint8_t, ModuleKeyHash> colors;
    colors.reserve(vm->linked_modules.size());
    for (const auto& linked : vm->linked_modules) {
        if (!visit_module(vm, linked.first, colors))
            return false;
    }
    return true;
}

} // namespace

extern "C" {

dao_status dao_value_make_bytes_view(dao_bytes bytes, dao_value* out_value) {
    return make_view(bytes, DAO_VALUE_BYTES, out_value);
}

dao_status dao_value_make_string_view(dao_bytes utf8, dao_value* out_value) {
    return make_view(utf8, DAO_VALUE_STRING, out_value);
}

dao_status dao_value_get_view(const dao_value* value, dao_bytes* out_bytes) {
    if (value == nullptr || out_bytes == nullptr)
        return DAO_INVALID_ARGUMENT;
    if ((value->type != DAO_VALUE_BYTES && value->type != DAO_VALUE_STRING) ||
        !is_valid_value(*value)) {
        return DAO_TYPE_ERROR;
    }
    *out_bytes = dao_bytes{view_data(*value), value->reserved};
    return DAO_OK;
}

dao_status dao_value_list_size(const dao_vm* vm, const dao_value* value, size_t* out_size) {
    if (vm == nullptr || value == nullptr || out_size == nullptr || value->type != DAO_VALUE_LIST) return DAO_INVALID_ARGUMENT;
    const auto* list = resolve_container(vm->lists, vm->container_generation, *value);
    if (list == nullptr) return DAO_RUNTIME_ERROR;
    *out_size = list->values.size(); return DAO_OK;
}

dao_status dao_value_list_get(const dao_vm* vm, const dao_value* value, size_t index,
                              dao_value* out_value) {
    if (vm == nullptr || value == nullptr || out_value == nullptr || value->type != DAO_VALUE_LIST) return DAO_INVALID_ARGUMENT;
    const auto* list = resolve_container(vm->lists, vm->container_generation, *value);
    if (list == nullptr) return DAO_RUNTIME_ERROR;
    if (index >= list->values.size()) return DAO_INVALID_ARGUMENT;
    *out_value = list->values[index]; return DAO_OK;
}

dao_status dao_value_map_get(const dao_vm* vm, const dao_value* value, dao_bytes utf8_key,
                             dao_value* out_value) {
    if (vm == nullptr || value == nullptr || out_value == nullptr || value->type != DAO_VALUE_MAP ||
        (utf8_key.size != 0 && utf8_key.data == nullptr) || !is_valid_utf8(utf8_key.data, utf8_key.size)) return DAO_INVALID_ARGUMENT;
    const auto* map = resolve_container(vm->maps, vm->container_generation, *value);
    if (map == nullptr) return DAO_RUNTIME_ERROR;
    const char* key_data = utf8_key.data == nullptr ? "" : reinterpret_cast<const char*>(utf8_key.data);
    const auto found = map->values.find(std::string(key_data, utf8_key.size));
    if (found == map->values.end()) return DAO_EXPORT_NOT_FOUND;
    *out_value = found->second; return DAO_OK;
}

dao_status dao_vm_make_list(dao_vm* vm, dao_value* out_value) {
    if (vm == nullptr || out_value == nullptr || vm->host_callback_depth == 0)
        return DAO_INVALID_ARGUMENT;
    try {
        vm->lists.push_back(std::make_unique<dao_vm::List>());
        *out_value = {DAO_VALUE_LIST, 0,
                      container_handle(vm->container_generation, vm->lists.size() - 1)};
        return DAO_OK;
    } catch (const std::bad_alloc&) {
        return DAO_OUT_OF_MEMORY;
    }
}

dao_status dao_value_list_append(dao_vm* vm, dao_value* list, const dao_value* value) {
    if (vm == nullptr || list == nullptr || value == nullptr || vm->host_callback_depth == 0 ||
        list->type != DAO_VALUE_LIST || !is_host_storable_value(vm, *value))
        return DAO_INVALID_ARGUMENT;
    auto* resolved = resolve_container(vm->lists, vm->container_generation, *list);
    if (resolved == nullptr) return DAO_RUNTIME_ERROR;
    try {
        resolved->values.push_back(*value);
        return DAO_OK;
    } catch (const std::bad_alloc&) {
        return DAO_OUT_OF_MEMORY;
    }
}

dao_status dao_vm_make_map(dao_vm* vm, dao_value* out_value) {
    if (vm == nullptr || out_value == nullptr || vm->host_callback_depth == 0)
        return DAO_INVALID_ARGUMENT;
    try {
        vm->maps.push_back(std::make_unique<dao_vm::Map>());
        *out_value = {DAO_VALUE_MAP, 0,
                      container_handle(vm->container_generation, vm->maps.size() - 1)};
        return DAO_OK;
    } catch (const std::bad_alloc&) {
        return DAO_OUT_OF_MEMORY;
    }
}

dao_status dao_value_map_set(dao_vm* vm, dao_value* map, dao_bytes utf8_key,
                             const dao_value* value) {
    if (vm == nullptr || map == nullptr || value == nullptr || vm->host_callback_depth == 0 ||
        map->type != DAO_VALUE_MAP || (utf8_key.size != 0 && utf8_key.data == nullptr) ||
        !is_valid_utf8(utf8_key.data, utf8_key.size) || !is_host_storable_value(vm, *value))
        return DAO_INVALID_ARGUMENT;
    auto* resolved = resolve_container(vm->maps, vm->container_generation, *map);
    if (resolved == nullptr) return DAO_RUNTIME_ERROR;
    const char* key_data = utf8_key.data == nullptr ? "" :
        reinterpret_cast<const char*>(utf8_key.data);
    try {
        resolved->values[std::string(key_data, utf8_key.size)] = *value;
        return DAO_OK;
    } catch (const std::bad_alloc&) {
        return DAO_OUT_OF_MEMORY;
    }
}

dao_vm_config dao_vm_config_default(void) {
    dao_vm_config config{};
    config.struct_size = sizeof(dao_vm_config);
    config.max_registers = 4096;
    config.max_call_depth = 1024;
    config.max_module_bytes = 64ULL * 1024ULL * 1024ULL;
    config.max_instructions_per_call = 10ULL * 1000ULL * 1000ULL;
    return config;
}

dao_vm* dao_vm_create(const dao_vm_config* requested) {
    dao_vm_config config = dao_vm_config_default();
    if (requested != nullptr) {
        if (requested->struct_size != sizeof(dao_vm_config))
            return nullptr;
        config = *requested;
    }
    if (config.max_registers == 0 || config.max_call_depth == 0 || config.reserved != 0 || config.max_module_bytes == 0 ||
        config.max_instructions_per_call == 0) {
        return nullptr;
    }
    dao_vm* vm = new (std::nothrow) dao_vm();
    if (vm != nullptr) vm->config = config;
    return vm;
}

void release_module(dao_module* module) {
    if (module != nullptr && module->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete module;
}

void dao_vm_destroy(dao_vm* vm) {
    if (vm == nullptr) return;
    for (auto& bucket : vm->module_cache) for (dao_module* module : bucket.second) release_module(module);
    for (auto& linked : vm->linked_modules) release_module(linked.second);
    delete vm;
}

dao_status dao_vm_get_cache_stats(const dao_vm* vm, dao_cache_stats* out_stats) {
    if (vm == nullptr || out_stats == nullptr || out_stats->struct_size != sizeof(*out_stats)) return DAO_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(vm->cache_mutex);
    uint64_t count = 0; for (const auto& bucket : vm->module_cache) count += bucket.second.size();
    out_stats->module_count = static_cast<uint32_t>(count); out_stats->hits = vm->cache_hits; out_stats->misses = vm->cache_misses; return DAO_OK;
}

dao_status dao_vm_clear_module_cache(dao_vm* vm) {
    if (vm == nullptr) return DAO_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(vm->cache_mutex);
    for (auto& bucket : vm->module_cache) for (dao_module* module : bucket.second) release_module(module);
    vm->module_cache.clear(); return DAO_OK;
}

dao_status dao_vm_set_module_cache_capacity(dao_vm* vm, uint32_t capacity) {
    if (vm == nullptr) return DAO_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(vm->cache_mutex);
    uint64_t count = 0; for (const auto& bucket : vm->module_cache) count += bucket.second.size();
    if (count > capacity) return DAO_INVALID_ARGUMENT;
    vm->max_cached_modules = capacity; return DAO_OK;
}

dao_status dao_vm_register_host_function(dao_vm* vm, const dao_host_function* function) {
    if (vm == nullptr || function == nullptr || function->struct_size != sizeof(*function) ||
        function->reserved != 0 || function->callback == nullptr ||
        function->parameter_count > std::numeric_limits<uint16_t>::max() ||
        function->parameter_count > vm->config.max_registers) {
        return DAO_INVALID_ARGUMENT;
    }
    try {
        const auto [unused, inserted] = vm->host_functions.emplace(
            function->symbol_id,
            HostFunction{function->parameter_count, function->callback, function->user_data});
        (void)unused;
        return inserted ? DAO_OK : DAO_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        return DAO_OUT_OF_MEMORY;
    }
}

dao_status dao_vm_unregister_host_function(dao_vm* vm, uint32_t symbol_id) {
    if (vm == nullptr)
        return DAO_INVALID_ARGUMENT;
    return vm->host_functions.erase(symbol_id) == 1 ? DAO_OK : DAO_IMPORT_NOT_FOUND;
}

dao_status dao_vm_load_module(dao_vm* vm, dao_bytes bytes, dao_module** out_module,
                              dao_error* error) {
    clear_error(error);
    if (vm == nullptr || out_module == nullptr || bytes.data == nullptr) {
        return fail(error, DAO_INVALID_ARGUMENT, "vm, bytes, and out_module are required");
    }
    *out_module = nullptr;
    if (bytes.size < dao::kHeaderSize || bytes.size > vm->config.max_module_bytes) {
        return fail(error, DAO_BAD_MODULE, "module size is invalid");
    }
    const uint64_t requested_fingerprint = fingerprint64(bytes.data, bytes.size);
    if (vm->max_cached_modules != 0) {
        std::lock_guard<std::mutex> lock(vm->cache_mutex);
        const auto bucket = vm->module_cache.find(requested_fingerprint);
        if (bucket != vm->module_cache.end()) {
            for (dao_module* cached : bucket->second) {
                if (cached->source_bytes.size() == bytes.size && std::memcmp(cached->source_bytes.data(), bytes.data, bytes.size) == 0) {
                    cached->references.fetch_add(1, std::memory_order_relaxed); ++vm->cache_hits; *out_module = cached; return DAO_OK;
                }
            }
        }
        ++vm->cache_misses;
    }
    if (std::memcmp(bytes.data, "DAO\0", 4) != 0) {
        return fail(error, DAO_BAD_MODULE, "invalid module magic");
    }
    const uint16_t format_version = read_u16(bytes.data + 4);
    const uint16_t vm_abi_version = read_u16(bytes.data + 6);
    const bool legacy_module = format_version == dao::kLegacyFormatVersion &&
                               vm_abi_version == dao::kLegacyVmAbiVersion;
    const bool identified_module = format_version == dao::kFormatVersion &&
                                   vm_abi_version == dao::kVmAbiVersion;
    if (!legacy_module && !identified_module) {
        return fail(error, DAO_BAD_MODULE, "unsupported format or VM ABI version");
    }
    if (read_u32(bytes.data + 8) != 0) {
        return fail(error, DAO_BAD_MODULE, "unsupported module flags");
    }

    const uint32_t section_count = read_u32(bytes.data + 12);
    if (section_count == 0 || section_count > 32) {
        return fail(error, DAO_BAD_MODULE, "invalid section count");
    }
    const uint64_t table_end = static_cast<uint64_t>(dao::kHeaderSize) +
                               static_cast<uint64_t>(section_count) * dao::kSectionEntrySize;
    if (table_end > bytes.size)
        return fail(error, DAO_BAD_MODULE, "truncated section table");

    std::vector<SectionRecord> sections;
    sections.reserve(section_count);
    for (uint32_t index = 0; index < section_count; ++index) {
        const uint8_t* entry =
            bytes.data + dao::kHeaderSize + static_cast<size_t>(index) * dao::kSectionEntrySize;
        SectionRecord section{read_u32(entry), read_u32(entry + 4), read_u32(entry + 8),
                              read_u32(entry + 12)};
        const uint32_t maximum_section = static_cast<uint32_t>(
            identified_module ? dao::SectionType::ModuleImports : dao::SectionType::Data);
        if (section.type < static_cast<uint32_t>(dao::SectionType::Functions) ||
            section.type > maximum_section) {
            return fail(error, DAO_BAD_MODULE, "unknown section type");
        }
        const uint64_t end = static_cast<uint64_t>(section.offset) + section.size;
        if (section.offset < table_end || end > bytes.size) {
            return fail(error, DAO_BAD_MODULE, "section lies outside module bounds");
        }
        if (find_section(sections, static_cast<dao::SectionType>(section.type)) != nullptr) {
            return fail(error, DAO_BAD_MODULE, "duplicate section type");
        }
        sections.push_back(section);
    }

    auto ranges = sections;
    std::sort(ranges.begin(), ranges.end(),
              [](const SectionRecord& left, const SectionRecord& right) {
                  return left.offset < right.offset;
              });
    for (size_t index = 1; index < ranges.size(); ++index) {
        const uint64_t previous_end =
            static_cast<uint64_t>(ranges[index - 1].offset) + ranges[index - 1].size;
        if (ranges[index - 1].size != 0 && ranges[index].size != 0 &&
            previous_end > ranges[index].offset) {
            return fail(error, DAO_BAD_MODULE, "module sections overlap");
        }
    }

    const SectionRecord* functions_section = find_section(sections, dao::SectionType::Functions);
    const SectionRecord* code_section = find_section(sections, dao::SectionType::Code);
    const SectionRecord* exports_section = find_section(sections, dao::SectionType::Exports);
    const SectionRecord* imports_section = find_section(sections, dao::SectionType::Imports);
    const SectionRecord* data_section = find_section(sections, dao::SectionType::Data);
    const SectionRecord* metadata_section = find_section(sections, dao::SectionType::Metadata);
    const SectionRecord* module_imports_section =
        find_section(sections, dao::SectionType::ModuleImports);
    if (functions_section == nullptr || code_section == nullptr || exports_section == nullptr ||
        imports_section == nullptr || data_section == nullptr ||
        (identified_module && (metadata_section == nullptr || module_imports_section == nullptr))) {
        return fail(error, DAO_BAD_MODULE, "required section is missing");
    }
    if (functions_section->size !=
            static_cast<uint64_t>(functions_section->count) * dao::kFunctionRecordSize ||
        code_section->size != static_cast<uint64_t>(code_section->count) * dao::kInstructionSize ||
        exports_section->size !=
            static_cast<uint64_t>(exports_section->count) * dao::kExportRecordSize ||
        imports_section->size !=
            static_cast<uint64_t>(imports_section->count) * dao::kImportRecordSize ||
        (identified_module &&
         (metadata_section->count != 1 ||
          metadata_section->size != dao::kModuleMetadataRecordSize ||
          module_imports_section->size !=
              static_cast<uint64_t>(module_imports_section->count) *
                  dao::kModuleImportRecordSize))) {
        return fail(error, DAO_BAD_MODULE, "section size does not match record count");
    }

    dao_module* module = new (std::nothrow) dao_module();
    if (module == nullptr)
        return fail(error, DAO_OUT_OF_MEMORY, "module allocation failed");
    module->fingerprint = requested_fingerprint;

    try {
        module->functions.reserve(functions_section->count);
        for (uint32_t index = 0; index < functions_section->count; ++index) {
            const uint8_t* record = bytes.data + functions_section->offset +
                                    static_cast<size_t>(index) * dao::kFunctionRecordSize;
            FunctionRecord function{read_u32(record), read_u32(record + 4), read_u16(record + 8),
                                    read_u16(record + 10)};
            const uint32_t reserved = read_u32(record + 12);
            const uint64_t code_end =
                static_cast<uint64_t>(function.code_offset) + function.code_count;
            if (function.parameter_count > function.register_count ||
                function.register_count > vm->config.max_registers ||
                code_end > code_section->count || reserved != 0) {
                delete module;
                return fail(error, DAO_VERIFY_ERROR, "invalid function record", index);
            }
            module->functions.push_back(function);
        }

        module->code.reserve(code_section->count);
        for (uint32_t index = 0; index < code_section->count; ++index) {
            const uint8_t* record = bytes.data + code_section->offset +
                                    static_cast<size_t>(index) * dao::kInstructionSize;
            module->code.push_back(dao::Instruction{static_cast<dao::Opcode>(record[0]), record[1],
                                                    read_u16(record + 2), read_u16(record + 4),
                                                    read_u16(record + 6), read_i64(record + 8)});
        }
        for (auto& function : module->functions) function.instructions = module->code.data() + function.code_offset;
        module->source_bytes.assign(bytes.data, bytes.data + bytes.size);

        module->imports.reserve(imports_section->count);
        std::unordered_map<uint32_t, bool> import_symbols;
        import_symbols.reserve(imports_section->count);
        for (uint32_t index = 0; index < imports_section->count; ++index) {
            const uint8_t* record = bytes.data + imports_section->offset +
                                    static_cast<size_t>(index) * dao::kImportRecordSize;
            const ImportRecord import{read_u32(record), read_u16(record + 4)};
            if (read_u16(record + 6) != 0 || import.parameter_count > vm->config.max_registers ||
                !import_symbols.emplace(import.symbol_id, true).second) {
                delete module;
                return fail(error, DAO_VERIFY_ERROR, "invalid or duplicate import");
            }
            module->imports.push_back(import);
        }

        const uint64_t data_records_size = static_cast<uint64_t>(data_section->count) * dao::kDataRecordSize;
        if (data_records_size > data_section->size) {
            delete module; return fail(error, DAO_BAD_MODULE, "truncated data records");
        }
        module->strings.reserve(data_section->count);
        for (uint32_t index = 0; index < data_section->count; ++index) {
            const uint8_t* record = bytes.data + data_section->offset + static_cast<size_t>(index) * dao::kDataRecordSize;
            const uint32_t offset = read_u32(record); const uint32_t length = read_u32(record + 4);
            const uint64_t end = static_cast<uint64_t>(offset) + length;
            if (offset < data_records_size || end > data_section->size ||
                !is_valid_utf8(bytes.data + data_section->offset + offset, length)) {
                delete module; return fail(error, DAO_VERIFY_ERROR, "invalid string constant");
            }
            module->strings.emplace_back(reinterpret_cast<const char*>(bytes.data + data_section->offset + offset), length);
        }

        if (identified_module) {
            const uint8_t* metadata = bytes.data + metadata_section->offset;
            const uint32_t identity_index = read_u32(metadata);
            if (identity_index >= module->strings.size() || module->strings[identity_index].empty() ||
                module->strings[identity_index].size() > 1024 || read_u32(metadata + 16) != 0 ||
                read_u32(metadata + 20) != 0) {
                delete module;
                return fail(error, DAO_VERIFY_ERROR, "invalid module identity metadata");
            }
            module->has_identity = true;
            module->identity = ModuleKey{module->strings[identity_index], read_u32(metadata + 4),
                                         read_u32(metadata + 8), read_u32(metadata + 12)};

            module->module_imports.reserve(module_imports_section->count);
            for (uint32_t index = 0; index < module_imports_section->count; ++index) {
                const uint8_t* record =
                    bytes.data + module_imports_section->offset +
                    static_cast<size_t>(index) * dao::kModuleImportRecordSize;
                const uint32_t name_index = read_u32(record);
                const uint16_t parameter_count = read_u16(record + 20);
                if (name_index >= module->strings.size() || module->strings[name_index].empty() ||
                    module->strings[name_index].size() > 1024 || read_u16(record + 22) != 0 ||
                    parameter_count > vm->config.max_registers) {
                    delete module;
                    return fail(error, DAO_VERIFY_ERROR, "invalid module import record");
                }
                module->module_imports.push_back(
                    {module->strings[name_index], read_u32(record + 4), read_u32(record + 8),
                     read_u32(record + 12), read_u32(record + 16), parameter_count});
            }
        }

        for (uint32_t function_index = 0; function_index < module->functions.size();
             ++function_index) {
            const auto& function = module->functions[function_index];
            for (uint32_t pc = 0; pc < function.code_count; ++pc) {
                const dao_status status =
                    verify_instruction(*module, function, module->code[function.code_offset + pc],
                                       function_index, pc, error);
                if (status != DAO_OK) {
                    delete module;
                    return status;
                }
            }
        }

        module->exports.reserve(exports_section->count);
        for (uint32_t index = 0; index < exports_section->count; ++index) {
            const uint8_t* record = bytes.data + exports_section->offset +
                                    static_cast<size_t>(index) * dao::kExportRecordSize;
            const uint32_t symbol = read_u32(record);
            const uint32_t function = read_u32(record + 4);
            if (function >= module->functions.size() ||
                !module->exports.emplace(symbol, function).second) {
                delete module;
                return fail(error, DAO_VERIFY_ERROR, "invalid or duplicate export");
            }
        }
    } catch (const std::bad_alloc&) {
        delete module;
        return fail(error, DAO_OUT_OF_MEMORY, "module decode allocation failed");
    }

    if (vm->max_cached_modules != 0) {
        std::lock_guard<std::mutex> lock(vm->cache_mutex);
        uint64_t count = 0; for (const auto& bucket : vm->module_cache) count += bucket.second.size();
        if (count < vm->max_cached_modules) { module->references.fetch_add(1, std::memory_order_relaxed); vm->module_cache[module->fingerprint].push_back(module); }
    }
    *out_module = module;
    return DAO_OK;
}

void dao_module_retain(dao_module* module) {
    if (module != nullptr)
        module->references.fetch_add(1, std::memory_order_relaxed);
}

void dao_module_release(dao_module* module) { release_module(module); }

uint64_t dao_module_fingerprint(const dao_module* module) {
    return module == nullptr ? 0 : module->fingerprint;
}

dao_status dao_module_get_identity(const dao_module* module,
                                   dao_module_identity* out_identity) {
    if (module == nullptr || out_identity == nullptr ||
        out_identity->struct_size != sizeof(*out_identity))
        return DAO_INVALID_ARGUMENT;
    if (!module->has_identity)
        return DAO_MODULE_IDENTITY_MISSING;
    out_identity->version_major = module->identity.version_major;
    out_identity->version_minor = module->identity.version_minor;
    out_identity->version_patch = module->identity.version_patch;
    out_identity->name = {
        reinterpret_cast<const uint8_t*>(module->identity.name.data()),
        module->identity.name.size()};
    return DAO_OK;
}

dao_status dao_vm_link_module(dao_vm* vm, dao_module* module, dao_error* error) {
    clear_error(error);
    if (vm == nullptr || module == nullptr)
        return fail(error, DAO_INVALID_ARGUMENT, "vm and module are required");
    if (!module->has_identity)
        return fail(error, DAO_MODULE_IDENTITY_MISSING, "module has no stable identity");
    const auto existing = vm->linked_modules.find(module->identity);
    if (existing != vm->linked_modules.end()) {
        const dao_module* current = existing->second;
        if (current->source_bytes.size() == module->source_bytes.size() &&
            std::memcmp(current->source_bytes.data(), module->source_bytes.data(),
                        module->source_bytes.size()) == 0)
            return DAO_OK;
        return fail(error, DAO_MODULE_CONFLICT,
                    "module identity and version are already linked with different bytes");
    }
    try {
        module->references.fetch_add(1, std::memory_order_relaxed);
        vm->linked_modules.emplace(module->identity, module);
    } catch (const std::bad_alloc&) {
        release_module(module);
        return fail(error, DAO_OUT_OF_MEMORY, "module link allocation failed");
    }
    dao_status status = validate_resolved_module_imports(vm, error);
    if (status == DAO_OK && !linked_module_graph_is_acyclic(vm))
        status = fail(error, DAO_MODULE_CYCLE, "linked module imports form a cycle");
    if (status != DAO_OK) {
        vm->linked_modules.erase(module->identity);
        release_module(module);
    }
    return status;
}

dao_status dao_vm_find_module(dao_vm* vm, dao_bytes identity_name, uint32_t version_major,
                              uint32_t version_minor, uint32_t version_patch,
                              dao_module** out_module) {
    if (vm == nullptr || out_module == nullptr || identity_name.data == nullptr ||
        identity_name.size == 0 || identity_name.size > 1024 ||
        !is_valid_utf8(identity_name.data, identity_name.size))
        return DAO_INVALID_ARGUMENT;
    *out_module = nullptr;
    try {
        const ModuleKey key{std::string(reinterpret_cast<const char*>(identity_name.data),
                                        identity_name.size),
                            version_major, version_minor, version_patch};
        const auto linked = vm->linked_modules.find(key);
        if (linked == vm->linked_modules.end())
            return DAO_IMPORT_NOT_FOUND;
        linked->second->references.fetch_add(1, std::memory_order_relaxed);
        *out_module = linked->second;
        return DAO_OK;
    } catch (const std::bad_alloc&) {
        return DAO_OUT_OF_MEMORY;
    }
}

dao_status dao_module_find_export(const dao_module* module, uint32_t symbol_id,
                                  dao_function* out_function) {
    if (module == nullptr || out_function == nullptr)
        return DAO_INVALID_ARGUMENT;
    const auto found = module->exports.find(symbol_id);
    if (found == module->exports.end())
        return DAO_EXPORT_NOT_FOUND;
    *out_function = found->second;
    return DAO_OK;
}

dao_status dao_vm_call(dao_vm* vm, const dao_module* module, dao_function function,
                       const dao_value* args, size_t arg_count, dao_value* out_value,
                       dao_error* error) {
    clear_error(error);
    if (vm == nullptr || module == nullptr || out_value == nullptr ||
        (arg_count != 0 && args == nullptr)) {
        return fail(error, DAO_INVALID_ARGUMENT, "vm, module, arguments, and output must be valid");
    }
    for (size_t index = 0; index < arg_count; ++index) {
        if (!is_valid_value(args[index])) {
            return fail(error, DAO_TYPE_ERROR, "argument contains an invalid value");
        }
        if (args[index].type == DAO_VALUE_LIST || args[index].type == DAO_VALUE_MAP ||
            args[index].type == DAO_VALUE_FUNCTION || args[index].type == DAO_VALUE_CLOSURE) {
            return fail(error, DAO_INVALID_ARGUMENT,
                        "VM-owned and module-local values cannot be reused across top-level calls");
        }
    }
    vm->lists.clear(); vm->maps.clear(); vm->closures.clear();
    if (++vm->container_generation == 0) ++vm->container_generation;
    uint64_t budget = vm->config.max_instructions_per_call;
    dao_value thrown = null_value();
    const dao_status status = execute_function(vm, module, function, args, arg_count, 0, budget, out_value, &thrown, error);
    if (status == DAO_RUNTIME_ERROR && thrown.type != DAO_VALUE_NULL) {
        if (thrown.type == DAO_VALUE_STRING) {
            const std::string message(reinterpret_cast<const char*>(view_data(thrown)), thrown.reserved);
            return fail(error, DAO_RUNTIME_ERROR, message.c_str());
        }
        return fail(error, DAO_RUNTIME_ERROR, "uncaught exception");
    }
    return status;
}

const char* dao_status_name(dao_status status) {
    switch (status) {
    case DAO_OK:
        return "ok";
    case DAO_INVALID_ARGUMENT:
        return "invalid_argument";
    case DAO_OUT_OF_MEMORY:
        return "out_of_memory";
    case DAO_BAD_MODULE:
        return "bad_module";
    case DAO_VERIFY_ERROR:
        return "verify_error";
    case DAO_EXPORT_NOT_FOUND:
        return "export_not_found";
    case DAO_TYPE_ERROR:
        return "type_error";
    case DAO_DIVIDE_BY_ZERO:
        return "divide_by_zero";
    case DAO_INTEGER_OVERFLOW:
        return "integer_overflow";
    case DAO_CALL_DEPTH_EXCEEDED:
        return "call_depth_exceeded";
    case DAO_INSTRUCTION_LIMIT_EXCEEDED:
        return "instruction_limit_exceeded";
    case DAO_RUNTIME_ERROR:
        return "runtime_error";
    case DAO_IMPORT_NOT_FOUND:
        return "import_not_found";
    case DAO_MODULE_IDENTITY_MISSING:
        return "module_identity_missing";
    case DAO_MODULE_CONFLICT:
        return "module_conflict";
    case DAO_MODULE_CYCLE:
        return "module_cycle";
    case DAO_MODULE_VERSION_MISMATCH:
        return "module_version_mismatch";
    }
    return "unknown";
}

} // extern "C"
