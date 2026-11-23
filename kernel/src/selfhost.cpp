#include "dao/selfhost.hpp"

#include "dao/format.hpp"
#include "dao/ku_migration.hpp"
#include "dao/source_host.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace dao::selfhost {
namespace {

uint32_t symbol(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

void set_error(dao_error* error, dao_status status, std::string_view message) {
    if (error == nullptr)
        return;
    *error = {};
    error->code = status;
    std::snprintf(error->message, sizeof(error->message), "%.*s",
                  static_cast<int>(message.size()), message.data());
}

struct State {
    ModuleBuilder builder;
    FunctionSpec function;
    std::vector<uint8_t> bytes;
    uint32_t function_count = 0;
    bool function_active = false;
    bool encoded = false;
    bool identity_set = false;
};

struct Context {
    explicit Context(const Options& value) : options(value) {}
    Options options;
    std::vector<std::unique_ptr<State>> states;
    std::deque<std::string> strings;
    std::deque<std::string> imported_sources;
    std::string resolver_error;
    struct SourceFile { std::string name; std::string_view text; };
    struct Span { size_t start; size_t length; size_t file; size_t offset; };
    std::vector<SourceFile> source_files;
    std::deque<std::vector<Span>> source_maps;
    std::string_view active_source;
    size_t active_offset = 0;
};

bool is_i64(const dao_value& value) { return value.type == DAO_VALUE_I64; }

bool view_offset(std::string_view buffer, std::string_view view, size_t* offset) {
    const auto begin = reinterpret_cast<uintptr_t>(buffer.data());
    const auto address = reinterpret_cast<uintptr_t>(view.data());
    if (address < begin || address - begin > buffer.size() ||
        view.size() > buffer.size() - (address - begin)) return false;
    *offset = address - begin;
    return true;
}

// Flatten mappings while source fragments are copied, including nested imports.
std::vector<Context::Span> source_spans(Context* context, std::string_view source) {
    if (context->options.diagnostic == nullptr) return {};
    size_t offset = 0;
    for (size_t file = 0; file < context->source_files.size(); ++file) {
        if (view_offset(context->source_files[file].text, source, &offset))
            return {{0, source.size(), file, offset}};
    }
    for (size_t index = 0; index < context->strings.size(); ++index) {
        if (!view_offset(context->strings[index], source, &offset)) continue;
        std::vector<Context::Span> result;
        for (const auto& span : context->source_maps[index]) {
            const size_t begin = std::max(offset, span.start);
            const size_t end = std::min(offset + source.size(), span.start + span.length);
            if (begin < end)
                result.push_back({begin - offset, end - begin, span.file,
                                  span.offset + begin - span.start});
        }
        return result;
    }
    return {};
}

void capture_source_error(Context* context, size_t offset) {
    const auto spans = source_spans(context, context->active_source);
    if (offset == std::string::npos || spans.empty()) return;
    for (const auto& span : spans) {
        if (offset >= span.start + span.length) continue;
        const auto& file = context->source_files[span.file];
        capture_diagnostic(context->options.diagnostic, file.name, file.text,
                           span.offset + (offset > span.start ? offset - span.start : 0));
        return;
    }
    const auto& last = spans.back();
    const auto& file = context->source_files[last.file];
    capture_diagnostic(context->options.diagnostic, file.name, file.text,
                       last.offset + last.length);
}

dao_status source_context(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    if (count != 2 || args[0].type != DAO_VALUE_STRING || !is_i64(args[1]) ||
        args[1].payload < 0) return DAO_TYPE_ERROR;
    dao_bytes bytes{};
    if (dao_value_get_view(&args[0], &bytes) != DAO_OK ||
        static_cast<uint64_t>(args[1].payload) > bytes.size) return DAO_INVALID_ARGUMENT;
    auto* context = static_cast<Context*>(user_data);
    context->active_source = {reinterpret_cast<const char*>(bytes.data), bytes.size};
    context->active_offset = static_cast<size_t>(args[1].payload);
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

constexpr int64_t kSelfhostRegisterLimit = 4096;
constexpr size_t kSelfhostBuilderLimit = 1024;

bool patchable(Opcode opcode) {
    return opcode == Opcode::BranchTritNegative || opcode == Opcode::BranchTritZero ||
           opcode == Opcode::BranchTritPositive || opcode == Opcode::Jump ||
           opcode == Opcode::TryBegin;
}

bool finished_targets_are_valid(const FunctionSpec& function) {
    for (const Instruction& instruction : function.code) {
        if (patchable(instruction.opcode) &&
            (instruction.immediate < 0 ||
             static_cast<uint64_t>(instruction.immediate) >= function.code.size()))
            return false;
    }
    return true;
}

dao_status verify_encoded_module(const std::vector<uint8_t>& bytes) {
    dao_vm_config config = dao_vm_config_default();
    dao_vm* vm = dao_vm_create(&config);
    if (vm == nullptr)
        return DAO_OUT_OF_MEMORY;
    if (bytes.size() >= 8 && bytes[4] == dao::kRecordFormatVersion && bytes[5] == 0 &&
        bytes[6] == dao::kRecordVmAbiVersion && bytes[7] == 0)
        dao_vm_set_features(vm, DAO_FEATURE_RECORDS);
    dao_module* module = nullptr;
    dao_error error{};
    const dao_status status =
        dao_vm_load_module(vm, {bytes.data(), bytes.size()}, &module, &error);
    if (module != nullptr)
        dao_module_release(module);
    dao_vm_destroy(vm);
    return status;
}

State* state(Context* context, const dao_value& handle) {
    if (!is_i64(handle) || handle.payload <= 0 ||
        static_cast<size_t>(handle.payload) > context->states.size())
        return nullptr;
    return context->states[static_cast<size_t>(handle.payload) - 1].get();
}

std::string* string_value(Context* context, const dao_value& handle) {
    if (!is_i64(handle) || handle.payload <= 0 ||
        static_cast<size_t>(handle.payload) > context->strings.size())
        return nullptr;
    return &context->strings[static_cast<size_t>(handle.payload) - 1];
}

using source_host::source_len;
using source_host::source_byte;
using source_host::source_slice;

dao_status source_hash(void*, const dao_value* args, size_t count, dao_value* out) {
    if (count != 3 || args[0].type != DAO_VALUE_STRING || !is_i64(args[1]) ||
        !is_i64(args[2]))
        return DAO_TYPE_ERROR;
    dao_bytes bytes{};
    if (dao_value_get_view(&args[0], &bytes) != DAO_OK || args[1].payload < 0 ||
        args[2].payload < 0 ||
        static_cast<uint64_t>(args[1].payload + args[2].payload) > bytes.size)
        return DAO_INVALID_ARGUMENT;
    uint32_t hash = 2166136261u;
    for (int64_t index = 0; index < args[2].payload; ++index) {
        hash ^= bytes.data[args[1].payload + index];
        hash *= 16777619u;
    }
    *out = {DAO_VALUE_I64, 0, hash};
    return DAO_OK;
}

dao_status module_source(void* user_data, const dao_value* args, size_t count,
                         dao_value* out) {
    auto* context = static_cast<Context*>(user_data);
    if (count != 1 || args[0].type != DAO_VALUE_STRING)
        return DAO_TYPE_ERROR;
    if (context->options.module_resolver == nullptr)
        return DAO_IMPORT_NOT_FOUND;
    dao_bytes path_bytes{};
    if (dao_value_get_view(&args[0], &path_bytes) != DAO_OK)
        return DAO_TYPE_ERROR;
    const std::string_view path(reinterpret_cast<const char*>(path_bytes.data), path_bytes.size);
    std::string source;
    std::string resolver_error;
    if (!context->options.module_resolver(context->options.module_user_data, path, &source,
                                          &resolver_error)) {
        context->resolver_error = std::move(resolver_error);
        return DAO_IMPORT_NOT_FOUND;
    }
    context->imported_sources.push_back(std::move(source));
    const std::string& imported = context->imported_sources.back();
    if (context->options.diagnostic)
        context->source_files.push_back({std::string(path), imported});
    const dao_status status = dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(imported.data()), imported.size()}, out);
    if (status != DAO_OK)
        context->resolver_error = "imported module is not valid UTF-8";
    return status;
}

dao_status source_append(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* context = static_cast<Context*>(user_data);
    auto* output = count == 4 ? string_value(context, args[0]) : nullptr;
    if (output == nullptr || args[1].type != DAO_VALUE_STRING || !is_i64(args[2]) ||
        !is_i64(args[3]) || args[2].payload < 0 || args[3].payload < 0)
        return DAO_INVALID_ARGUMENT;
    dao_bytes bytes{};
    if (dao_value_get_view(&args[1], &bytes) != DAO_OK ||
        static_cast<uint64_t>(args[2].payload) > bytes.size ||
        static_cast<uint64_t>(args[3].payload) > bytes.size - args[2].payload)
        return DAO_INVALID_ARGUMENT;
    const std::string_view fragment(reinterpret_cast<const char*>(bytes.data) + args[2].payload,
                                    static_cast<size_t>(args[3].payload));
    const auto spans = source_spans(context, fragment);
    const size_t start = output->size();
    output->append(fragment);
    auto& mapping = context->source_maps[static_cast<size_t>(args[0].payload) - 1];
    for (auto span : spans) {
        span.start += start;
        if (!mapping.empty()) {
            auto& last = mapping.back();
            if (last.file == span.file && last.start + last.length == span.start &&
                last.offset + last.length == span.offset) {
                last.length += span.length;
                continue;
            }
        }
        mapping.push_back(span);
    }
    *out = args[0];
    return DAO_OK;
}

dao_status string_new(void* user_data, const dao_value*, size_t count, dao_value* out) {
    if (count != 0)
        return DAO_INVALID_ARGUMENT;
    auto* context = static_cast<Context*>(user_data);
    if (context->states.size() >= kSelfhostBuilderLimit)
        return DAO_INVALID_ARGUMENT;
    context->strings.emplace_back();
    context->source_maps.emplace_back();
    *out = {DAO_VALUE_I64, 0, static_cast<int64_t>(context->strings.size())};
    return DAO_OK;
}

dao_status string_append(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* value = count == 2 ? string_value(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (value == nullptr || !is_i64(args[1]) || args[1].payload < 0 || args[1].payload > 255)
        return DAO_INVALID_ARGUMENT;
    value->push_back(static_cast<char>(args[1].payload));
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status string_finish(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* value = count == 1 ? string_value(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (value == nullptr)
        return DAO_INVALID_ARGUMENT;
    return dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(value->data()), value->size()}, out);
}

dao_status builder_new(void* user_data, const dao_value*, size_t count, dao_value* out) {
    if (count != 0)
        return DAO_INVALID_ARGUMENT;
    auto* context = static_cast<Context*>(user_data);
    try {
        context->states.push_back(std::make_unique<State>());
    } catch (...) {
        return DAO_OUT_OF_MEMORY;
    }
    *out = {DAO_VALUE_I64, 0, static_cast<int64_t>(context->states.size())};
    return DAO_OK;
}

dao_status builder_begin(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 3 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || current->function_active || current->encoded ||
        !is_i64(args[1]) || !is_i64(args[2]) || args[1].payload < 0 ||
        args[2].payload <= 0 || args[2].payload > kSelfhostRegisterLimit ||
        args[1].payload > args[2].payload)
        return DAO_INVALID_ARGUMENT;
    current->function = {static_cast<uint16_t>(args[1].payload),
                         static_cast<uint16_t>(args[2].payload), {}};
    current->function_active = true;
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status builder_record_type(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 2 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (!current || current->function_active || current->encoded || args[1].type != DAO_VALUE_STRING) return DAO_INVALID_ARGUMENT;
    dao_bytes name{};
    if (dao_value_get_view(&args[1], &name) != DAO_OK) return DAO_TYPE_ERROR;
    try {
        *out = {DAO_VALUE_I64, 0, current->builder.add_record_type({reinterpret_cast<const char*>(name.data), name.size})};
    } catch (...) { return DAO_INVALID_ARGUMENT; }
    return DAO_OK;
}

dao_status builder_record_field(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 4 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (!current || current->function_active || current->encoded || !is_i64(args[1]) || !is_i64(args[3]) ||
        args[1].payload < 0 || args[1].payload > UINT32_MAX || args[3].payload < 1 || args[3].payload > 4 ||
        args[2].type != DAO_VALUE_STRING) return DAO_INVALID_ARGUMENT;
    dao_bytes name{};
    if (dao_value_get_view(&args[2], &name) != DAO_OK) return DAO_TYPE_ERROR;
    try {
        *out = {DAO_VALUE_I64, 0, current->builder.add_record_field(static_cast<uint32_t>(args[1].payload),
            {reinterpret_cast<const char*>(name.data), name.size}, static_cast<uint32_t>(args[3].payload))};
    } catch (...) { return DAO_INVALID_ARGUMENT; }
    return DAO_OK;
}

dao_status builder_emit(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 6 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || !current->function_active || current->encoded)
        return DAO_INVALID_ARGUMENT;
    for (size_t index = 1; index < 6; ++index) {
        if (!is_i64(args[index]))
            return DAO_TYPE_ERROR;
    }
    if (args[1].payload < static_cast<int64_t>(Opcode::Nop) ||
        args[1].payload > static_cast<int64_t>(Opcode::FieldGet) || args[2].payload < 0 ||
        args[2].payload >= kSelfhostRegisterLimit || args[3].payload < 0 ||
        args[3].payload >= kSelfhostRegisterLimit || args[4].payload < 0 ||
        args[4].payload >= kSelfhostRegisterLimit)
        return DAO_INVALID_ARGUMENT;
    try {
        current->function.code.push_back(
            {static_cast<Opcode>(args[1].payload), 0, static_cast<uint16_t>(args[2].payload),
             static_cast<uint16_t>(args[3].payload), static_cast<uint16_t>(args[4].payload),
             args[5].payload});
    } catch (...) {
        return DAO_OUT_OF_MEMORY;
    }
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status builder_add_string(void* user_data, const dao_value* args, size_t count,
                              dao_value* out) {
    auto* current = count == 2 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || current->encoded || args[1].type != DAO_VALUE_STRING)
        return DAO_INVALID_ARGUMENT;
    dao_bytes bytes{};
    if (dao_value_get_view(&args[1], &bytes) != DAO_OK)
        return DAO_TYPE_ERROR;
    try {
        *out = {DAO_VALUE_I64, 0,
                static_cast<int64_t>(current->builder.add_string(std::string_view(
                    reinterpret_cast<const char*>(bytes.data), bytes.size)))};
    } catch (...) {
        return DAO_RUNTIME_ERROR;
    }
    return DAO_OK;
}

dao_status builder_import(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 3 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || current->encoded || !is_i64(args[1]) ||
        !is_i64(args[2]) || args[1].payload < 0 ||
        args[1].payload > std::numeric_limits<uint32_t>::max() || args[2].payload < 0 ||
        args[2].payload > kSelfhostRegisterLimit)
        return DAO_INVALID_ARGUMENT;
    try {
        *out = {DAO_VALUE_I64, 0,
                static_cast<int64_t>(current->builder.add_import(
                    static_cast<uint32_t>(args[1].payload),
                    static_cast<uint16_t>(args[2].payload)))};
    } catch (...) {
        return DAO_RUNTIME_ERROR;
    }
    return DAO_OK;
}

dao_status builder_identity(void* user_data, const dao_value* args, size_t count,
                            dao_value* out) {
    auto* current = count == 5 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || current->encoded || current->function_active ||
        current->function_count != 0 || current->identity_set ||
        args[1].type != DAO_VALUE_STRING) {
        return DAO_INVALID_ARGUMENT;
    }
    for (size_t index = 2; index < 5; ++index) {
        if (!is_i64(args[index]) || args[index].payload < 0 ||
            args[index].payload > std::numeric_limits<uint32_t>::max()) {
            return DAO_INVALID_ARGUMENT;
        }
    }
    dao_bytes name{};
    if (dao_value_get_view(&args[1], &name) != DAO_OK)
        return DAO_TYPE_ERROR;
    try {
        current->builder.set_identity(
            std::string_view(reinterpret_cast<const char*>(name.data), name.size),
            {static_cast<uint32_t>(args[2].payload),
             static_cast<uint32_t>(args[3].payload),
             static_cast<uint32_t>(args[4].payload)});
        current->identity_set = true;
    } catch (...) {
        return DAO_RUNTIME_ERROR;
    }
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status builder_module_import(void* user_data, const dao_value* args, size_t count,
                                 dao_value* out) {
    auto* current = count == 7 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || current->encoded || !current->identity_set ||
        args[1].type != DAO_VALUE_STRING) {
        return DAO_INVALID_ARGUMENT;
    }
    for (size_t index = 2; index < 6; ++index) {
        if (!is_i64(args[index]) || args[index].payload < 0 ||
            args[index].payload > std::numeric_limits<uint32_t>::max()) {
            return DAO_INVALID_ARGUMENT;
        }
    }
    if (!is_i64(args[6]) || args[6].payload < 0 ||
        args[6].payload > kSelfhostRegisterLimit) {
        return DAO_INVALID_ARGUMENT;
    }
    dao_bytes name{};
    if (dao_value_get_view(&args[1], &name) != DAO_OK)
        return DAO_TYPE_ERROR;
    try {
        *out = {DAO_VALUE_I64, 0,
                static_cast<int64_t>(current->builder.add_module_import(
                    std::string_view(reinterpret_cast<const char*>(name.data), name.size),
                    {static_cast<uint32_t>(args[2].payload),
                     static_cast<uint32_t>(args[3].payload),
                     static_cast<uint32_t>(args[4].payload)},
                    static_cast<uint32_t>(args[5].payload),
                    static_cast<uint16_t>(args[6].payload)))};
    } catch (...) {
        return DAO_RUNTIME_ERROR;
    }
    return DAO_OK;
}

dao_status builder_position(void* user_data, const dao_value* args, size_t count,
                            dao_value* out) {
    auto* current = count == 1 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || !current->function_active || current->encoded)
        return DAO_INVALID_ARGUMENT;
    *out = {DAO_VALUE_I64, 0, static_cast<int64_t>(current->function.code.size())};
    return DAO_OK;
}

dao_status builder_patch(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 3 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || !current->function_active || current->encoded ||
        !is_i64(args[1]) || !is_i64(args[2]) || args[1].payload < 0 ||
        static_cast<size_t>(args[1].payload) >= current->function.code.size() ||
        args[2].payload < 0 ||
        static_cast<uint64_t>(args[2].payload) > current->function.code.size() ||
        !patchable(current->function.code[static_cast<size_t>(args[1].payload)].opcode))
        return DAO_INVALID_ARGUMENT;
    current->function.code[static_cast<size_t>(args[1].payload)].immediate = args[2].payload;
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status builder_patch_marked(void* user_data, const dao_value* args, size_t count,
                                dao_value* out) {
    auto* current = count == 5 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || !current->function_active || current->encoded ||
        !is_i64(args[1]) || !is_i64(args[2]) || !is_i64(args[3]) ||
        !is_i64(args[4]) || args[1].payload < 0 || args[2].payload < args[1].payload ||
        static_cast<size_t>(args[2].payload) > current->function.code.size() ||
        (args[3].payload != -1 && args[3].payload != -2) || args[4].payload < 0 ||
        static_cast<uint64_t>(args[4].payload) > current->function.code.size())
        return DAO_INVALID_ARGUMENT;
    for (int64_t index = args[1].payload; index < args[2].payload; ++index) {
        Instruction& instruction = current->function.code[static_cast<size_t>(index)];
        if (instruction.opcode == Opcode::Jump && instruction.immediate == args[3].payload)
            instruction.immediate = args[4].payload;
    }
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status builder_finish(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 3 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || !current->function_active || current->encoded ||
        !is_i64(args[1]) || !is_i64(args[2]) || args[1].payload < 0 ||
        args[1].payload > std::numeric_limits<uint32_t>::max() ||
        args[2].payload <= 0 || args[2].payload > kSelfhostRegisterLimit ||
        args[2].payload < current->function.parameter_count ||
        current->function.code.empty() ||
        current->function.code.back().opcode != Opcode::Return ||
        !finished_targets_are_valid(current->function))
        return DAO_INVALID_ARGUMENT;
    try {
        current->function.register_count = static_cast<uint16_t>(args[2].payload);
        const uint32_t index = current->builder.add_function(std::move(current->function));
        current->builder.add_export(static_cast<uint32_t>(args[1].payload), index);
        ++current->function_count;
        current->function_active = false;
    } catch (...) {
        return DAO_RUNTIME_ERROR;
    }
    *out = {DAO_VALUE_NULL, 0, 0};
    return DAO_OK;
}

dao_status builder_encode(void* user_data, const dao_value* args, size_t count, dao_value* out) {
    auto* current = count == 1 ? state(static_cast<Context*>(user_data), args[0]) : nullptr;
    if (current == nullptr || current->function_active || current->function_count == 0)
        return DAO_INVALID_ARGUMENT;
    if (current->encoded)
        return dao_value_make_bytes_view({current->bytes.data(), current->bytes.size()}, out);
    try {
        current->bytes = current->builder.encode();
    } catch (...) {
        return DAO_RUNTIME_ERROR;
    }
    const dao_status verify_status = verify_encoded_module(current->bytes);
    if (verify_status != DAO_OK) {
        current->bytes.clear();
        return verify_status;
    }
    current->encoded = true;
    return dao_value_make_bytes_view({current->bytes.data(), current->bytes.size()}, out);
}

struct Host {
    const char* name;
    uint32_t arity;
    dao_host_callback callback;
};

constexpr Host hosts[] = {
    {"source_context", 2, source_context},
    {"source_append", 4, source_append},
    {"source_len", 1, source_len},
    {"source_byte", 2, source_byte},
    {"source_slice", 3, source_slice},
    {"source_hash", 3, source_hash},
    {"module_source", 1, module_source},
    {"string_new", 0, string_new},
    {"string_append", 2, string_append},
    {"string_finish", 1, string_finish},
    {"builder_new", 0, builder_new},
    {"builder_begin", 3, builder_begin},
    {"builder_record_type", 2, builder_record_type},
    {"builder_record_field", 4, builder_record_field},
    {"builder_emit_raw", 6, builder_emit},
    {"builder_add_string", 2, builder_add_string},
    {"builder_import", 3, builder_import},
    {"builder_identity", 5, builder_identity},
    {"builder_module_import", 7, builder_module_import},
    {"builder_position", 1, builder_position},
    {"builder_patch", 3, builder_patch},
    {"builder_patch_marked", 5, builder_patch_marked},
    {"builder_finish", 3, builder_finish},
    {"builder_encode", 1, builder_encode},
};

} // namespace

bool compile(std::string_view compiler_source, std::string_view source,
             std::vector<uint8_t>* output, dao_error* error, Options options) {
    if (options.diagnostic) *options.diagnostic = {};
    if (output == nullptr) {
        set_error(error, DAO_INVALID_ARGUMENT, "output is null");
        return false;
    }
    output->clear();
    ModuleBuilder bootstrap;
    if (!km::compile(compiler_source, bootstrap, error))
        return false;
    std::vector<uint8_t> compiler_bytes;
    try {
        compiler_bytes = bootstrap.encode();
    } catch (...) {
        set_error(error, DAO_RUNTIME_ERROR, "cannot encode self-hosted compiler");
        return false;
    }

    dao_vm_config config = dao_vm_config_default();
    config.max_instructions_per_call = options.instruction_limit;
    std::unique_ptr<dao_vm, decltype(&dao_vm_destroy)> vm(dao_vm_create(&config), dao_vm_destroy);
    if (!vm) {
        set_error(error, DAO_OUT_OF_MEMORY, "cannot create compiler VM");
        return false;
    }
    Context context(options);
    context.source_files.push_back({options.source_name, source});
    context.active_source = source;
    for (const Host& host : hosts) {
        const dao_host_function function{sizeof(function), symbol(host.name), host.arity, 0,
                                         host.callback, &context};
        const dao_status status = dao_vm_register_host_function(vm.get(), &function);
        if (status != DAO_OK) {
            set_error(error, status, "cannot register compiler Host capability");
            return false;
        }
    }

    dao_module* raw_module = nullptr;
    const dao_status load_status = dao_vm_load_module(
        vm.get(), {compiler_bytes.data(), compiler_bytes.size()}, &raw_module, error);
    std::unique_ptr<dao_module, decltype(&dao_module_release)> module(raw_module,
                                                                    dao_module_release);
    if (load_status != DAO_OK)
        return false;
    dao_function function = 0;
    const std::string_view export_name =
        options.has_identity ? std::string_view("compile_identified") : std::string_view("compile");
    const dao_status export_status =
        dao_module_find_export(module.get(), symbol(export_name), &function);
    if (export_status != DAO_OK) {
        set_error(error, export_status,
                  options.has_identity ? "self-hosted compiler has no compile_identified export"
                                       : "self-hosted compiler has no compile export");
        return false;
    }
    dao_value arguments[5]{};
    const dao_status value_status = dao_value_make_string_view(
        {reinterpret_cast<const uint8_t*>(source.data()), source.size()}, &arguments[0]);
    if (value_status != DAO_OK) {
        set_error(error, value_status, "source is not valid UTF-8");
        return false;
    }
    size_t argument_count = 1;
    if (options.has_identity) {
        const dao_status identity_status = dao_value_make_string_view(
            {reinterpret_cast<const uint8_t*>(options.identity_name.data()),
             options.identity_name.size()},
            &arguments[1]);
        if (identity_status != DAO_OK) {
            set_error(error, identity_status, "module identity is not valid UTF-8");
            return false;
        }
        arguments[2] = {DAO_VALUE_I64, 0, options.identity_version.major};
        arguments[3] = {DAO_VALUE_I64, 0, options.identity_version.minor};
        arguments[4] = {DAO_VALUE_I64, 0, options.identity_version.patch};
        argument_count = 5;
    }
    dao_value result{};
    const dao_status call_status =
        dao_vm_call(vm.get(), module.get(), function, arguments, argument_count, &result, error);
    if (call_status != DAO_OK) {
        if (!context.resolver_error.empty())
            set_error(error, DAO_IMPORT_NOT_FOUND, context.resolver_error);
        const size_t offset = context.resolver_error.empty()
                                  ? diagnostic_offset(error ? error->message : "")
                                  : context.active_offset;
        capture_source_error(&context, offset);
        return false;
    }
    dao_bytes bytes{};
    const dao_status view_status = dao_value_get_view(&result, &bytes);
    if (view_status != DAO_OK) {
        set_error(error, view_status, "self-hosted compiler returned a non-bytes value");
        return false;
    }
    output->assign(bytes.data, bytes.data + bytes.size);
    return true;
}

} // namespace dao::selfhost
