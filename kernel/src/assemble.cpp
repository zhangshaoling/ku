#include "dao/assemble.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace dao {
namespace {

enum class Section {
    None = 0,
    Imports,
    Exports,
    Functions,
};

struct ParsedContext {
    Section section = Section::None;
    uint32_t declared_import_count = 0;
    uint32_t parsed_import_count = 0;
    uint32_t declared_export_count = 0;
    uint32_t parsed_export_count = 0;
    uint32_t declared_function_count = 0;
    uint32_t parsed_function_count = 0;
    ParsedFunction* current_function = nullptr;
    uint32_t declared_instruction_count = 0;
    uint32_t parsed_instruction_count = 0;
};

void clear_error(dao_error* error) {
    if (error == nullptr) return;
    std::memset(error, 0, sizeof(*error));
    error->function_index = std::numeric_limits<uint32_t>::max();
    error->instruction_index = std::numeric_limits<uint32_t>::max();
}

bool fail(dao_error* error, const char* message) {
    if (error != nullptr) {
        clear_error(error);
        error->code = DAO_VERIFY_ERROR;
        if (message != nullptr) {
            std::strncpy(error->message, message, sizeof(error->message) - 1);
            error->message[sizeof(error->message) - 1] = '\0';
        }
    }
    return false;
}

std::string trim(std::string_view view) {
    size_t start = 0;
    while (start < view.size() &&
           (view[start] == ' ' || view[start] == '\t' || view[start] == '\r')) {
        ++start;
    }
    size_t end = view.size();
    while (end > start &&
           (view[end - 1] == ' ' || view[end - 1] == '\t' || view[end - 1] == '\r')) {
        --end;
    }
    return std::string(view.substr(start, end - start));
}

std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == ',')) ++i;
        if (i >= line.size()) break;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != ',') ++j;
        fields.push_back(line.substr(i, j - i));
        i = j;
    }
    return fields;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool strip_prefix(std::string_view& s, std::string_view prefix) {
    if (!starts_with(s, prefix)) return false;
    s.remove_prefix(prefix.size());
    return true;
}

bool parse_uint32_strict(std::string_view s, uint32_t& out) {
    if (s.empty()) return false;
    uint64_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
        if (value > std::numeric_limits<uint32_t>::max()) return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool parse_uint16_strict(std::string_view s, uint16_t& out) {
    uint32_t v32 = 0;
    if (!parse_uint32_strict(s, v32) || v32 > std::numeric_limits<uint16_t>::max()) return false;
    out = static_cast<uint16_t>(v32);
    return true;
}

bool parse_int64_strict(std::string_view s, int64_t& out) {
    if (s.empty()) return false;
    bool neg = false;
    size_t i = 0;
    if (s[0] == '-') {
        neg = true;
        i = 1;
    } else if (s[0] == '+') {
        i = 1;
    }
    if (i >= s.size()) return false;
    uint64_t mag = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        uint64_t digit = static_cast<uint64_t>(s[i] - '0');
        if (mag > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        mag = mag * 10 + digit;
    }
    if (neg) {
        if (mag > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1) return false;
        out = -static_cast<int64_t>(mag);
        return true;
    }
    if (mag > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return false;
    out = static_cast<int64_t>(mag);
    return true;
}

bool parse_register(std::string_view field, uint16_t& out) {
    std::string_view s = field;
    while (!s.empty() && s.back() == ',') s.remove_suffix(1);
    if (!strip_prefix(s, "r") && !strip_prefix(s, "R")) return false;
    return parse_uint16_strict(s, out);
}

Opcode parse_opcode(std::string_view token) {
    if (token.size() >= 2) {
        switch (token[0]) {
        case 'N':
        case 'n':
            if (token == "NOP" || token == "nop") return Opcode::Nop;
            break;
        case 'L':
        case 'l':
            if (token == "LOAD_I64" || token == "load_i64") return Opcode::LoadI64;
            break;
        case 'M':
        case 'm':
            if (token == "MOVE" || token == "move") return Opcode::Move;
            if (token == "MUL_I64" || token == "mul_i64") return Opcode::MulI64;
            break;
        case 'A':
        case 'a':
            if (token == "ADD_I64" || token == "add_i64") return Opcode::AddI64;
            break;
        case 'S':
        case 's':
            if (token == "SUB_I64" || token == "sub_i64") return Opcode::SubI64;
            break;
        case 'D':
        case 'd':
            if (token == "DIV_I64" || token == "div_i64") return Opcode::DivI64;
            break;
        case 'T':
        case 't':
            if (token == "TRIT_NOT" || token == "trit_not") return Opcode::TritNot;
            if (token == "TRIT_AND" || token == "trit_and") return Opcode::TritAnd;
            if (token == "TRIT_OR" || token == "trit_or") return Opcode::TritOr;
            break;
        case 'B':
        case 'b':
            if (token == "BR_TRIT_NEG" || token == "br_trit_neg") return Opcode::BranchTritNegative;
            if (token == "BR_TRIT_ZERO" || token == "br_trit_zero") return Opcode::BranchTritZero;
            if (token == "BR_TRIT_POS" || token == "br_trit_pos") return Opcode::BranchTritPositive;
            break;
        case 'J':
        case 'j':
            if (token == "JUMP" || token == "jump") return Opcode::Jump;
            break;
        case 'C':
        case 'c':
            if (token == "CALL" || token == "call") return Opcode::Call;
            if (token == "CALL_HOST" || token == "call_host") return Opcode::CallHost;
            break;
        case 'R':
        case 'r':
            if (token == "RETURN" || token == "return") return Opcode::Return;
            break;
        }
    }
    return Opcode::Nop;
}

bool is_valid_opcode(std::string_view token) {
    return parse_opcode(token) != Opcode::Nop || token == "NOP" || token == "nop";
}

bool parse_import_line(const std::string& line, ParsedModule& module, ParsedContext& ctx,
                       dao_error* error) {
    std::string t = trim(line);
    if (!starts_with(t, "[import") && !starts_with(t, "[IMPORT")) {
        return fail(error, "import line: expected [import N]");
    }
    size_t close = t.find(']');
    if (close == std::string::npos) {
        return fail(error, "import line: missing ]");
    }
    std::string idx_str = trim(t.substr(7, close - 7));
    uint32_t symbol_id = 0;
    if (!parse_uint32_strict(idx_str, symbol_id)) {
        return fail(error, "import line: invalid symbol id");
    }
    std::string tail = trim(t.substr(close + 1));
    uint16_t params = 0;
    if (!tail.empty()) {
        std::string_view tv = tail;
        if (!strip_prefix(tv, "params=") && !strip_prefix(tv, "PARAMS=")) {
            return fail(error, "import line: missing params=");
        }
        uint16_t v = 0;
        if (!parse_uint16_strict(tv, v)) {
            return fail(error, "import line: invalid parameter count");
        }
        params = v;
    }
    module.imports.push_back({symbol_id, params});
    ++ctx.parsed_import_count;
    return true;
}

bool parse_export_line(const std::string& line, ParsedModule& module, ParsedContext& ctx,
                       dao_error* error) {
    // Format from disassembler: "  symbol=N -> function M"
    // Robust parse: split once on " -> "
    std::string t = line;
    // find " -> "
    std::string arrow = " -> ";
    size_t arrowPos = t.find(arrow);
    if (arrowPos == std::string::npos) {
        return fail(error, "export line: missing ' -> '");
    }
    std::string left = trim(t.substr(0, arrowPos));
    std::string right = trim(t.substr(arrowPos + arrow.size()));
    // left: "symbol=N" or "SYMBOL=N"
    std::string_view leftView = left;
    if (!strip_prefix(leftView, "symbol=") && !strip_prefix(leftView, "SYMBOL=")) {
        return fail(error, "export line: missing symbol=");
    }
    uint32_t symbol_id = 0;
    if (!parse_uint32_strict(leftView, symbol_id)) {
        return fail(error, "export line: invalid symbol id");
    }
    std::string_view rightView = right;
    if (!strip_prefix(rightView, "function ") && !strip_prefix(rightView, "FUNCTION ")) {
        return fail(error, "export line: missing 'function'");
    }
    uint32_t function_index = 0;
    if (!parse_uint32_strict(rightView, function_index)) {
        return fail(error, "export line: invalid function index");
    }
    module.exports.push_back({symbol_id, function_index});
    ++ctx.parsed_export_count;
    return true;
}

bool parse_function_header(const std::string& line, ParsedModule& module, ParsedContext& ctx,
                           dao_error* error) {
    std::string t = trim(line);
    if (!starts_with(t, "[function") && !starts_with(t, "[FUNCTION")) {
        return fail(error, "function block: expected [function N] ...");
    }
    auto fields = split_fields(t);
    if (fields.size() < 3) {
        return fail(error, "function block: header too short");
    }
    std::string idx_str(fields[1]);
    while (!idx_str.empty() && idx_str.back() == ']') idx_str.pop_back();
    uint32_t function_index = 0;
    if (!parse_uint32_strict(idx_str, function_index)) {
        return fail(error, "function block: invalid function index");
    }
    if (function_index != ctx.parsed_function_count) {
        return fail(error, "function block: functions must appear in order");
    }
    ParsedFunction function;
    uint32_t declared_instructions = 0;
    for (size_t i = 2; i < fields.size(); ++i) {
        std::string_view body = fields[i];
        while (!body.empty() && body.back() == ']') body.remove_suffix(1);
        std::string_view bet = body;
        if (strip_prefix(bet, "registers=") || strip_prefix(bet, "REGISTERS=")) {
            uint16_t v = 0;
            if (!parse_uint16_strict(bet, v)) {
                return fail(error, "function block: invalid register count");
            }
            function.register_count = v;
        } else if (strip_prefix(bet, "parameters=") || strip_prefix(bet, "PARAMETERS=")) {
            uint16_t v = 0;
            if (!parse_uint16_strict(bet, v)) {
                return fail(error, "function block: invalid parameter count");
            }
            function.parameter_count = v;
        } else if (strip_prefix(bet, "instructions=") || strip_prefix(bet, "INSTRUCTIONS=")) {
            uint32_t v = 0;
            if (!parse_uint32_strict(bet, v)) {
                return fail(error, "function block: invalid instruction count");
            }
            declared_instructions = v;
        }
    }
    module.functions.push_back(std::move(function));
    ctx.current_function = &module.functions.back();
    ctx.declared_instruction_count = declared_instructions;
    ctx.parsed_instruction_count = 0;
    ++ctx.parsed_function_count;
    return true;
}

bool parse_instruction_line(const std::string& line, ParsedContext& ctx, dao_error* error) {
    if (ctx.current_function == nullptr) {
        return fail(error, "instruction outside function block");
    }
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') {
        return fail(error, "unexpected blank/comment line in function");
    }
    auto fields = split_fields(t);

    size_t opcode_index = 0;
    if (fields.size() >= 2) {
        uint32_t maybe_addr = 0;
        if (parse_uint32_strict(fields[0], maybe_addr) && is_valid_opcode(fields[1])) {
            opcode_index = 1;
        }
    }
    if (opcode_index >= fields.size()) {
        return fail(error, "instruction line: empty");
    }
    if (!is_valid_opcode(fields[opcode_index])) {
        return fail(error, "invalid opcode mnemonic");
    }
    Opcode opcode = parse_opcode(fields[opcode_index]);

    Instruction instruction{};
    instruction.opcode = opcode;
    size_t operand_start = opcode_index + 1;

    switch (opcode) {
    case Opcode::Nop:
        break;
    case Opcode::Move:
    case Opcode::TritNot:
    case Opcode::TritAnd:
    case Opcode::TritOr: {
        if (fields.size() - operand_start < 2) {
            return fail(error, "instruction: expected dst, a");
        }
        if (!parse_register(fields[operand_start], instruction.dst) ||
            !parse_register(fields[operand_start + 1], instruction.a)) {
            return fail(error, "instruction: bad operand");
        }
        break;
    }
    case Opcode::AddI64:
    case Opcode::SubI64:
    case Opcode::MulI64:
    case Opcode::DivI64: {
        if (fields.size() - operand_start < 3) {
            return fail(error, "instruction: expected dst, a, b");
        }
        if (!parse_register(fields[operand_start], instruction.dst) ||
            !parse_register(fields[operand_start + 1], instruction.a) ||
            !parse_register(fields[operand_start + 2], instruction.b)) {
            return fail(error, "instruction: bad operand");
        }
        break;
    }
    case Opcode::LoadI64: {
        if (fields.size() - operand_start < 2) {
            return fail(error, "instruction: expected dst, immediate");
        }
        if (!parse_register(fields[operand_start], instruction.dst)) {
            return fail(error, "instruction: bad dst");
        }
        std::string_view imm_raw = fields[operand_start + 1];
        if (strip_prefix(imm_raw, "->")) {
            // optional keepalive; semantics are unchanged
        }
        if (!parse_int64_strict(imm_raw, instruction.immediate)) {
            return fail(error, "instruction: bad immediate");
        }
        break;
    }
    case Opcode::Return: {
        if (fields.size() - operand_start < 1) {
            return fail(error, "instruction: expected return register");
        }
        if (!parse_register(fields[operand_start], instruction.a)) {
            return fail(error, "instruction: bad a");
        }
        break;
    }
    case Opcode::Jump: {
        if (fields.size() - operand_start < 1) {
            return fail(error, "instruction: expected jump target");
        }
        std::string_view imm_raw = fields[operand_start];
        strip_prefix(imm_raw, "->");
        if (!parse_int64_strict(imm_raw, instruction.immediate)) {
            return fail(error, "instruction: bad target");
        }
        break;
    }
    case Opcode::BranchTritNegative:
    case Opcode::BranchTritZero:
    case Opcode::BranchTritPositive: {
        if (fields.size() - operand_start < 2) {
            return fail(error, "instruction: expected a, target");
        }
        if (!parse_register(fields[operand_start], instruction.a)) {
            return fail(error, "instruction: bad a");
        }
        std::string_view imm_raw = fields[operand_start + 1];
        strip_prefix(imm_raw, "->");
        if (!parse_int64_strict(imm_raw, instruction.immediate)) {
            return fail(error, "instruction: bad branch target");
        }
        break;
    }
    case Opcode::Call: {
        // Disasm emits: CALL fnINDEX, args rA..r(A+B-1), dst rDST
        if (fields.size() - operand_start < 5) {
            return fail(error, "call: expected fn INDEX, args rA..r(A+B-1), dst rDST");
        }
        std::string_view fn_field = fields[operand_start];
        while (!fn_field.empty() && fn_field.back() == ',') fn_field.remove_suffix(1);
        uint32_t fn_index_value = 0;
        bool has_fn_index = false;
        {
            std::string_view bet = fn_field;
            if (strip_prefix(bet, "fn") || strip_prefix(bet, "FN")) {
                has_fn_index = parse_uint32_strict(bet, fn_index_value);
            } else {
                has_fn_index = parse_uint32_strict(bet, fn_index_value);
            }
        }
        if (!has_fn_index) {
            return fail(error, "call instruction: expected function index");
        }
        instruction.immediate = static_cast<int64_t>(fn_index_value);

        std::string_view args_field = fields[operand_start + 1];
        if (!strip_prefix(args_field, "args") && !strip_prefix(args_field, "ARGS")) {
            return fail(error, "call instruction: missing 'args'");
        }
        std::string_view operands_field = fields[operand_start + 2];
        if (starts_with(operands_field, "r")) operands_field.remove_prefix(1);
        size_t dotdot = operands_field.find("..");
        if (dotdot == std::string_view::npos) {
            return fail(error, "call instruction: bad args range");
        }
        uint32_t a_value = 0;
        if (!parse_uint32_strict(operands_field.substr(0, dotdot), a_value)) {
            return fail(error, "call instruction: bad args.a");
        }
        instruction.a = static_cast<uint16_t>(a_value);
        std::string_view end_view = operands_field.substr(dotdot + 2);
        if (starts_with(end_view, "r")) end_view.remove_prefix(1);
        uint32_t end_value = 0;
        if (!parse_uint32_strict(end_view, end_value)) {
            return fail(error, "call instruction: bad args.end");
        }
        if (end_value + 1 < a_value) {
            return fail(error, "call instruction: empty args range");
        }
        instruction.b = static_cast<uint16_t>(end_value - a_value + 1);

        std::string_view dst_field = fields[operand_start + 3];
        if (!strip_prefix(dst_field, "dst") && !strip_prefix(dst_field, "DST")) {
            return fail(error, "call instruction: missing 'dst'");
        }
        std::string_view dst_reg = fields[operand_start + 4];
        while (!dst_reg.empty() && dst_reg.back() == ',') dst_reg.remove_suffix(1);
        if (!parse_register(dst_reg, instruction.dst)) {
            return fail(error, "call instruction: bad dst register");
        }
        break;
    }
    case Opcode::CallHost: {
        if (fields.size() - operand_start < 5) {
            return fail(error, "call_host: expected import INDEX, args rA..r(A+B-1), dst rDST");
        }
        std::string_view import_field = fields[operand_start];
        while (!import_field.empty() && import_field.back() == ',') import_field.remove_suffix(1);
        uint32_t index_value = 0;
        bool has_index = false;
        {
            std::string_view bet = import_field;
            if (strip_prefix(bet, "import") || strip_prefix(bet, "IMPORT")) {
                has_index = parse_uint32_strict(bet, index_value);
            } else {
                has_index = parse_uint32_strict(bet, index_value);
            }
        }
        if (!has_index) {
            return fail(error, "call_host instruction: expected import index");
        }
        instruction.immediate = static_cast<int64_t>(index_value);

        std::string_view args_field = fields[operand_start + 1];
        if (!strip_prefix(args_field, "args") && !strip_prefix(args_field, "ARGS")) {
            return fail(error, "call_host instruction: missing 'args'");
        }
        std::string_view operands_field = fields[operand_start + 2];
        if (starts_with(operands_field, "r")) operands_field.remove_prefix(1);
        size_t dotdot = operands_field.find("..");
        if (dotdot == std::string_view::npos) {
            return fail(error, "call_host instruction: bad args range");
        }
        uint32_t a_value = 0;
        if (!parse_uint32_strict(operands_field.substr(0, dotdot), a_value)) {
            return fail(error, "call_host instruction: bad args.a");
        }
        instruction.a = static_cast<uint16_t>(a_value);
        std::string_view end_view = operands_field.substr(dotdot + 2);
        if (starts_with(end_view, "r")) end_view.remove_prefix(1);
        uint32_t end_value = 0;
        if (!parse_uint32_strict(end_view, end_value)) {
            return fail(error, "call_host instruction: bad args.end");
        }
        if (end_value + 1 < a_value) {
            return fail(error, "call_host instruction: empty args range");
        }
        instruction.b = static_cast<uint16_t>(end_value - a_value + 1);

        std::string_view dst_field = fields[operand_start + 3];
        if (!strip_prefix(dst_field, "dst") && !strip_prefix(dst_field, "DST")) {
            return fail(error, "call_host instruction: missing 'dst'");
        }
        std::string_view dst_reg = fields[operand_start + 4];
        while (!dst_reg.empty() && dst_reg.back() == ',') dst_reg.remove_suffix(1);
        if (!parse_register(dst_reg, instruction.dst)) {
            return fail(error, "call_host instruction: bad dst register");
        }
        break;
    }
    }

    ctx.current_function->code.push_back(std::move(instruction));
    ++ctx.parsed_instruction_count;
    return true;
}

bool asm_parse_module(const std::string& text, ParsedModule* out_module, dao_error* error) {
    if (out_module == nullptr) {
        return fail(error, "out_module is null");
    }
    out_module->~ParsedModule();
    new (out_module) ParsedModule;

    clear_error(error);
    ParsedContext ctx;
    size_t cursor = 0;
    const size_t len = text.size();

    while (cursor < len) {
        size_t line_start = cursor;
        while (cursor < len && text[cursor] != '\n') ++cursor;
        size_t line_end = cursor;
        if (cursor < len && text[cursor] == '\n') ++cursor;

        size_t s = line_start;
        while (s < line_end && (text[s] == ' ' || text[s] == '\t' || text[s] == '\r')) ++s;
        size_t e = line_end;
        while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r')) --e;
        std::string_view trimmed_view(text.data() + s, e - s);

        if (trimmed_view.empty() || trimmed_view[0] == '#') {
            continue;
        }
        if (trimmed_view == "END") {
            break;
        }

        if (starts_with(trimmed_view, "imports:")) {
            if (ctx.section != Section::None) {
                return fail(error, "imports section must come first");
            }
            std::string_view body = trimmed_view;
            body.remove_prefix(strlen("imports:"));
            std::string body_copy = trim(body);
            if (!parse_uint32_strict(body_copy, ctx.declared_import_count)) {
                return fail(error, "imports: invalid count");
            }
            ctx.section = Section::Imports;
            continue;
        }

        if (starts_with(trimmed_view, "exports:")) {
            if (ctx.section != Section::Imports) {
                return fail(error, "unexpected section order");
            }
            std::string_view body = trimmed_view;
            body.remove_prefix(strlen("exports:"));
            std::string body_copy = trim(body);
            if (!parse_uint32_strict(body_copy, ctx.declared_export_count)) {
                return fail(error, "exports: invalid count");
            }
            ctx.section = Section::Exports;
            continue;
        }

        if (starts_with(trimmed_view, "functions:")) {
            if (ctx.section == Section::None || ctx.section == Section::Functions) {
                return fail(error, "unexpected section order");
            }
            std::string_view body = trimmed_view;
            body.remove_prefix(strlen("functions:"));
            std::string body_copy = trim(body);
            if (!parse_uint32_strict(body_copy, ctx.declared_function_count)) {
                return fail(error, "functions: invalid count");
            }
            ctx.section = Section::Functions;
            continue;
        }

        switch (ctx.section) {
        case Section::None:
            return fail(error, "line appears before any section");
        case Section::Imports:
            if (!parse_import_line(std::string(trimmed_view), *out_module, ctx, error)) return false;
            break;
        case Section::Exports:
            if (!parse_export_line(std::string(trimmed_view), *out_module, ctx, error)) return false;
            break;
        case Section::Functions: {
            std::string t = std::string(trimmed_view);
            if (starts_with(t, "[function") || starts_with(t, "[FUNCTION")) {
                if (ctx.current_function != nullptr &&
                    ctx.parsed_instruction_count != ctx.declared_instruction_count) {
                    return fail(error, "function instruction count mismatch");
                }
                if (!parse_function_header(t, *out_module, ctx, error)) return false;
                break;
            }
            if (ctx.current_function == nullptr) {
                return fail(error, "stray line in functions section");
            }
            if (!parse_instruction_line(t, ctx, error)) return false;
            break;
        }
        }
    }

    if (ctx.section != Section::Functions) {
        return fail(error, "functions section missing");
    }
    if (ctx.current_function != nullptr &&
        ctx.parsed_instruction_count != ctx.declared_instruction_count) {
        return fail(error, "function instruction count mismatch");
    }
    if (ctx.parsed_import_count != ctx.declared_import_count) {
        return fail(error, "import count mismatch");
    }
    if (ctx.parsed_export_count != ctx.declared_export_count) {
        return fail(error, "export count mismatch");
    }
    if (ctx.parsed_function_count != ctx.declared_function_count) {
        return fail(error, "function count mismatch");
    }
    return true;
}

bool asm_assemble_to_builder(const std::string& text, ModuleBuilder* builder, dao_error* error) {
    if (builder == nullptr) {
        return fail(error, "builder is null");
    }
    ParsedModule module;
    if (!asm_parse_module(text, &module, error)) return false;
    for (const auto& import : module.imports) {
        try {
            builder->add_import(import.symbol_id, import.parameter_count);
        } catch (const std::exception& e) {
            return fail(error, e.what());
        }
    }
    for (auto& function : module.functions) {
        if (function.register_count == 0 && !function.code.empty()) {
            return fail(error, "function header missing/invalid register count");
        }
        FunctionSpec spec;
        spec.parameter_count = function.parameter_count;
        spec.register_count = function.register_count;
        spec.code = std::move(function.code);
        try {
            builder->add_function(std::move(spec));
        } catch (const std::exception& e) {
            return fail(error, e.what());
        }
    }
    for (const auto& export_ : module.exports) {
        try {
            builder->add_export(export_.symbol_id, export_.function_index);
        } catch (const std::exception& e) {
            return fail(error, e.what());
        }
    }
    return true;
}

bool asm_assemble(const std::string& text, std::vector<uint8_t>* out_bytes, dao_error* error) {
    if (out_bytes == nullptr) {
        return fail(error, "out_bytes is null");
    }
    clear_error(error);
    ModuleBuilder builder;
    if (!asm_assemble_to_builder(text, &builder, error)) return false;
    try {
        *out_bytes = builder.encode();
    } catch (const std::exception& e) {
        return fail(error, e.what());
    }
    return true;
}

} // namespace

bool parse_module_text(const std::string& text, ParsedModule* out_module, dao_error* error) {
    return asm_parse_module(text, out_module, error);
}

bool assemble_text(const std::string& text, std::vector<uint8_t>* out_bytes, dao_error* error) {
    return asm_assemble(text, out_bytes, error);
}

bool assemble_to_builder(const std::string& text, ModuleBuilder* builder, dao_error* error) {
    return asm_assemble_to_builder(text, builder, error);
}

} // namespace dao
