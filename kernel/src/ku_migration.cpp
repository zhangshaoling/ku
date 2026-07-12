#include "dao/ku_migration.hpp"

#include <charconv>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dao::km {
namespace {

enum class Kind { End, Newline, Name, Number, String, LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Colon, Op };
struct Token { Kind kind; std::string text; size_t offset; };

void clear(dao_error* error) {
    if (error) std::memset(error, 0, sizeof(*error));
}

bool fail(dao_error* error, const std::string& message) {
    if (error) {
        clear(error);
        error->code = DAO_VERIFY_ERROR;
        std::strncpy(error->message, message.c_str(), sizeof(error->message) - 1);
    }
    return false;
}

uint32_t symbol_id(std::string_view name) {
    uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::vector<Token> lex(std::string_view source) {
    std::vector<Token> out;
    size_t i = 0;
    auto push = [&](Kind kind, size_t start, size_t length) {
        out.push_back({kind, std::string(source.substr(start, length)), start});
    };
    while (i < source.size()) {
        const unsigned char ch = static_cast<unsigned char>(source[i]);
        if (ch == ' ' || ch == '\t' || ch == '\r') { ++i; continue; }
        if (ch == ';' && i + 1 < source.size() && source[i + 1] == ';') {
            while (i < source.size() && source[i] != '\n') ++i;
            continue;
        }
        if (ch == '\n' || ch == ';') { push(Kind::Newline, i++, 1); continue; }
        if (ch == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') ++i;
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            const size_t start = i++;
            while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
            push(Kind::Number, start, i - start); continue;
        }
        if (ch == '"') {
            const size_t start = i++; std::string value;
            while (i < source.size() && source[i] != '"') {
                if (source[i] == '\\') { if (++i >= source.size()) throw std::runtime_error("unterminated string at offset " + std::to_string(start)); const char esc = source[i++]; if (esc == 'n') value.push_back('\n'); else if (esc == 't') value.push_back('\t'); else if (esc == 'r') value.push_back('\r'); else value.push_back(esc); }
                else value.push_back(source[i++]);
            }
            if (i >= source.size()) throw std::runtime_error("unterminated string at offset " + std::to_string(start));
            ++i; out.push_back({Kind::String, std::move(value), start}); continue;
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || ch >= 0x80) {
            const size_t start = i++;
            while (i < source.size()) {
                const unsigned char c = static_cast<unsigned char>(source[i]);
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c >= 0x80)) break;
                ++i;
            }
            push(Kind::Name, start, i - start); continue;
        }
        const size_t start = i++;
        switch (ch) {
        case '(': push(Kind::LParen, start, 1); break;
        case ')': push(Kind::RParen, start, 1); break;
        case '{': push(Kind::LBrace, start, 1); break;
        case '}': push(Kind::RBrace, start, 1); break;
        case '[': push(Kind::LBracket, start, 1); break;
        case ']': push(Kind::RBracket, start, 1); break;
        case ',': push(Kind::Comma, start, 1); break;
        case ':': push(Kind::Colon, start, 1); break;
        case '+': case '-': case '*': case '/': case '%': case '=': case '!': case '<': case '>': {
            if (i < source.size() && source[i] == '=') ++i;
            push(Kind::Op, start, i - start); break;
        }
        default: throw std::runtime_error("unsupported character at offset " + std::to_string(start));
        }
    }
    out.push_back({Kind::End, {}, source.size()});
    return out;
}

struct Expr {
    enum class Type { Number, String, Name, Unary, Binary, Call, List, Map, Index, Conditional } type = Type::Number;
    std::string value;
    std::vector<Expr> children;
    size_t offset = 0;
};
struct Stmt {
    enum class Type { Expression, Assign, IndexAssign, Return, If, While, For, Try, Throw, Break, Continue } type = Type::Expression;
    std::string target;
    Expr expr;
    Expr assignment_target;
    std::vector<Stmt> body;
    std::vector<Stmt> alternate;
};
struct Function { std::string name; std::vector<std::string> params; std::vector<Stmt> body; };
struct Import { std::string name; uint16_t arity; };
struct ModuleImport { std::string path; std::string alias; };
struct Program {
    std::vector<Import> imports;
    std::vector<ModuleImport> module_imports;
    std::vector<Function> functions;
};

class Parser {
  public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    Program parse() {
        Program program;
        skip_lines();
        while (peek().kind != Kind::End) {
            if ((word("import") && peek(1).kind == Kind::String) || word("\xE5\xBC\x95"))
                program.module_imports.push_back(module_import_decl());
            else if (word("import"))
                program.imports.push_back(import_decl());
            else
                program.functions.push_back(function());
            skip_lines();
        }
        return program;
    }
  private:
    const Token& peek(size_t n = 0) const { return tokens_[pos_ + n]; }
    Token take() { return tokens_[pos_++]; }
    bool word(std::string_view s) const { return peek().kind == Kind::Name && peek().text == s; }
    void expect(Kind kind, std::string_view what) { if (peek().kind != kind) error("expected " + std::string(what)); take(); }
    void skip_lines() { while (peek().kind == Kind::Newline) take(); }
    [[noreturn]] void error(const std::string& message) const {
        throw std::runtime_error(message + " at offset " + std::to_string(peek().offset));
    }
    Import import_decl() {
        take();
        if (peek().kind != Kind::Name) error("expected host import name");
        Import result{take().text, 0}; expect(Kind::LParen, "(");
        if (peek().kind != Kind::Number) error("expected host import parameter count");
        unsigned long arity = std::stoul(take().text);
        if (arity > std::numeric_limits<uint16_t>::max()) error("host import parameter count is too large");
        result.arity = static_cast<uint16_t>(arity); expect(Kind::RParen, ")"); return result;
    }
    ModuleImport module_import_decl() {
        take();
        if (peek().kind != Kind::String) error("expected module import path");
        ModuleImport result{take().text, {}};
        if (word("as") || word("\xE5\x88\xAB")) {
            take();
            if (peek().kind != Kind::Name) error("expected module import alias");
            result.alias = take().text;
        } else {
            const size_t slash = result.path.find_last_of("/\\");
            result.alias = result.path.substr(slash == std::string::npos ? 0 : slash + 1);
        }
        if (result.path.empty() || result.alias.empty()) error("module import path and alias must not be empty");
        return result;
    }
    Function function() {
        if (!(word("thought") || word("func") || word("思"))) error("expected thought, func, or 思");
        take();
        if (peek().kind != Kind::Name) error("expected function name");
        Function fn; fn.name = take().text;
        expect(Kind::LParen, "(");
        if (peek().kind != Kind::RParen) {
            for (;;) {
                if (peek().kind != Kind::Name) error("expected parameter name");
                fn.params.push_back(take().text);
                if (peek().kind != Kind::Comma) break;
                take();
            }
        }
        expect(Kind::RParen, ")"); skip_lines(); fn.body = block();
        return fn;
    }
    std::vector<Stmt> block() {
        expect(Kind::LBrace, "{"); skip_lines();
        std::vector<Stmt> statements;
        while (peek().kind != Kind::RBrace) {
            if (peek().kind == Kind::End) error("unterminated block");
            statements.push_back(statement()); skip_lines();
        }
        take(); return statements;
    }
    Stmt statement() {
        Stmt stmt;
        if (word("break") || word("断")) { take(); stmt.type = Stmt::Type::Break; return stmt; }
        if (word("continue") || word("续")) { take(); stmt.type = Stmt::Type::Continue; return stmt; }
        if (word("throw") || word("抛")) { take(); stmt.type = Stmt::Type::Throw; stmt.expr = expression(); return stmt; }
        if (word("try") || word("试")) {
            take(); stmt.type = Stmt::Type::Try; skip_lines(); stmt.body = block(); skip_lines();
            if (!(word("catch") || word("捕"))) error("expected catch"); take();
            if (peek().kind != Kind::Name) error("expected catch variable"); stmt.target = take().text; skip_lines(); stmt.alternate = block(); return stmt;
        }
        if (word("if") || word("若")) {
            take(); stmt.type = Stmt::Type::If;
            stmt.expr = expression();
            skip_lines(); stmt.body = block();
            const bool bare_alternate = peek().kind == Kind::LBrace;
            skip_lines();
            if (word("else") || word("否")) {
                take();
                skip_lines();
                if (word("if") || word("若"))
                    stmt.alternate.push_back(statement());
                else
                    stmt.alternate = block();
            } else if (bare_alternate) stmt.alternate = block();
            return stmt;
        }
        if (word("while") || word("当")) {
            take(); stmt.type = Stmt::Type::While; stmt.expr = expression(); skip_lines(); stmt.body = block(); return stmt;
        }
        if (word("for") || word("遍")) {
            take(); stmt.type = Stmt::Type::For;
            if (peek().kind != Kind::Name) error("expected loop variable"); stmt.target = take().text;
            if (!(word("in") || word("于"))) error("expected in"); take();
            stmt.expr = expression(); skip_lines(); stmt.body = block(); return stmt;
        }
        if (word("return") || word("返")) { take(); stmt.type = Stmt::Type::Return; stmt.expr = expression(); return stmt; }
        if (peek().kind == Kind::Name && peek(1).kind == Kind::Op && peek(1).text == "=") {
            stmt.type = Stmt::Type::Assign; stmt.target = take().text; take(); stmt.expr = expression(); return stmt;
        }
        stmt.expr = expression();
        if (peek().kind == Kind::Op && peek().text == "=") {
            if (stmt.expr.type != Expr::Type::Index) error("invalid assignment target");
            take(); stmt.type = Stmt::Type::IndexAssign; stmt.assignment_target = std::move(stmt.expr); stmt.expr = expression();
        }
        return stmt;
    }
    static int precedence(const Token& token) {
        if (token.kind == Kind::Name && token.text == "or") return 1;
        if (token.kind == Kind::Name && token.text == "and") return 2;
        if (token.kind != Kind::Op) return -1;
        if (token.text == "==" || token.text == "!=" || token.text == "<" || token.text == ">" || token.text == "<=" || token.text == ">=") return 3;
        if (token.text == "+" || token.text == "-") return 4;
        if (token.text == "*" || token.text == "/" || token.text == "%") return 5;
        return -1;
    }
    Expr expression(int min_bp = 0) {
        Expr left = prefix();
        while (precedence(peek()) >= min_bp) {
            Token op = take(); const int bp = precedence(op);
            Expr right = expression(bp + 1);
            left = {Expr::Type::Binary, op.text, {std::move(left), std::move(right)}, op.offset};
        }
        return left;
    }
    Expr expression_branch() {
        expect(Kind::LBrace, "{");
        skip_lines();
        Expr value = expression();
        skip_lines();
        expect(Kind::RBrace, "}");
        return value;
    }
    Expr prefix() {
        Token token = take();
        if (token.kind == Kind::Name && token.text == "if") {
            Expr condition;
            if (peek().kind == Kind::LParen) {
                take(); condition = expression(); expect(Kind::RParen, ")");
            } else condition = expression();
            skip_lines();
            Expr positive = expression_branch();
            skip_lines();
            if (word("else")) { take(); skip_lines(); }
            if (peek().kind != Kind::LBrace) error("conditional expression requires alternate");
            Expr alternate = expression_branch();
            return {Expr::Type::Conditional, {},
                    {std::move(condition), std::move(positive), std::move(alternate)},
                    token.offset};
        }
        const bool operator_call = peek().kind == Kind::LParen &&
            ((token.kind == Kind::Op && precedence(token) >= 0) ||
             (token.kind == Kind::Name &&
              (token.text == "and" || token.text == "or" || token.text == "not")));
        if (operator_call) {
            take(); skip_lines();
            std::vector<Expr> args;
            if (peek().kind != Kind::RParen) {
                for (;;) {
                    args.push_back(expression());
                    skip_lines();
                    if (peek().kind != Kind::Comma) break;
                    take(); skip_lines();
                }
            }
            expect(Kind::RParen, ")");
            if (args.size() == 1 &&
                (token.text == "-" || token.text == "!" || token.text == "not")) {
                return {Expr::Type::Unary, token.text, std::move(args), token.offset};
            }
            if (args.size() >= 2 && precedence(token) >= 0 &&
                (args.size() == 2 || token.text == "and" || token.text == "or")) {
                Expr combined = std::move(args[0]);
                for (size_t index = 1; index < args.size(); ++index) {
                    combined = {Expr::Type::Binary, token.text,
                                {std::move(combined), std::move(args[index])}, token.offset};
                }
                return combined;
            }
            error("operator call has invalid argument count");
        }
        if ((token.kind == Kind::Op && (token.text == "-" || token.text == "!")) ||
            (token.kind == Kind::Name && token.text == "not")) {
            return {Expr::Type::Unary, token.text, {expression(6)}, token.offset};
        }
        Expr result;
        if (token.kind == Kind::Number) result = {Expr::Type::Number, token.text, {}, token.offset};
        else if (token.kind == Kind::String) result = {Expr::Type::String, token.text, {}, token.offset};
        else if (token.kind == Kind::LBracket) {
            std::vector<Expr> items; skip_lines();
            if (peek().kind != Kind::RBracket) { for (;;) { items.push_back(expression()); skip_lines(); if (peek().kind != Kind::Comma) break; take(); skip_lines(); } }
            expect(Kind::RBracket, "]"); result = {Expr::Type::List, {}, std::move(items), token.offset};
        }
        else if (token.kind == Kind::LBrace) {
            std::vector<Expr> pairs; skip_lines();
            if (peek().kind != Kind::RBrace) {
                for (;;) { if (peek().kind != Kind::String) error("map key must be a string literal"); Token key = take(); pairs.push_back({Expr::Type::String, key.text, {}, key.offset}); expect(Kind::Colon, ":"); skip_lines(); pairs.push_back(expression()); skip_lines(); if (peek().kind != Kind::Comma) break; take(); skip_lines(); }
            }
            expect(Kind::RBrace, "}"); result = {Expr::Type::Map, {}, std::move(pairs), token.offset};
        }
        else if (token.kind == Kind::Name) {
            if (peek().kind != Kind::LParen) result = {Expr::Type::Name, token.text, {}, token.offset};
            else {
                take(); skip_lines(); std::vector<Expr> args;
                if (peek().kind != Kind::RParen) { for (;;) { args.push_back(expression()); skip_lines(); if (peek().kind != Kind::Comma) break; take(); skip_lines(); } }
                expect(Kind::RParen, ")"); result = {Expr::Type::Call, token.text, std::move(args), token.offset};
            }
        }
        else if (token.kind == Kind::LParen) { result = expression(); expect(Kind::RParen, ")"); }
        else error("expected expression");
        while (peek().kind == Kind::LBracket) { take(); Expr index = expression(); expect(Kind::RBracket, "]"); result = {Expr::Type::Index, {}, {std::move(result), std::move(index)}, token.offset}; }
        return result;
    }
    std::vector<Token> tokens_; size_t pos_ = 0;
};

void rewrite_expr(Expr& expr, const std::string& prefix,
                  const std::unordered_set<std::string>& local_functions,
                  const std::vector<ModuleImport>& module_imports) {
    for (Expr& child : expr.children)
        rewrite_expr(child, prefix, local_functions, module_imports);
    if (expr.type != Expr::Type::Call)
        return;
    if (local_functions.contains(expr.value)) {
        expr.value = prefix + expr.value;
        return;
    }
    for (const ModuleImport& module_import : module_imports) {
        const std::string alias_prefix = module_import.alias + "_";
        if (expr.value.starts_with(alias_prefix)) {
            expr.value = prefix + expr.value;
            return;
        }
    }
}

void rewrite_statements(std::vector<Stmt>& statements, const std::string& prefix,
                        const std::unordered_set<std::string>& local_functions,
                        const std::vector<ModuleImport>& module_imports) {
    for (Stmt& statement : statements) {
        rewrite_expr(statement.expr, prefix, local_functions, module_imports);
        rewrite_expr(statement.assignment_target, prefix, local_functions, module_imports);
        rewrite_statements(statement.body, prefix, local_functions, module_imports);
        rewrite_statements(statement.alternate, prefix, local_functions, module_imports);
    }
}

void collect_program(std::string_view source, const std::string& prefix, const Options& options,
                     std::unordered_set<std::string>& active_imports, Program& merged) {
    Program program = Parser(lex(source)).parse();
    for (const ModuleImport& module_import : program.module_imports) {
        if (options.import_resolver == nullptr)
            throw std::runtime_error("module import '" + module_import.path + "' requires an import resolver");
        if (!active_imports.insert(module_import.path).second)
            throw std::runtime_error("cyclic module import '" + module_import.path + "'");
        std::string imported_source;
        std::string resolver_error;
        if (!options.import_resolver(options.import_user_data, module_import.path, &imported_source,
                                     &resolver_error)) {
            throw std::runtime_error("cannot resolve module import '" + module_import.path +
                                     "': " + resolver_error);
        }
        collect_program(imported_source, prefix + module_import.alias + "_", options,
                        active_imports, merged);
        active_imports.erase(module_import.path);
    }

    std::unordered_set<std::string> local_functions;
    for (const Function& function : program.functions)
        local_functions.insert(function.name);
    for (Function& function : program.functions) {
        rewrite_statements(function.body, prefix, local_functions, program.module_imports);
        function.name = prefix + function.name;
        merged.functions.push_back(std::move(function));
    }
    for (Import& import : program.imports)
        merged.imports.push_back(std::move(import));
}

Instruction instruction(Opcode op, uint16_t dst = 0, uint16_t a = 0, uint16_t b = 0, int64_t imm = 0) {
    return Instruction{op, 0, dst, a, b, imm};
}

class Emitter {
  public:
    struct FunctionTarget { uint32_t index; uint16_t arity; };
    struct HostImport { uint32_t index; uint16_t arity; };
    Emitter(const std::unordered_map<std::string, FunctionTarget>& indices,
            const std::unordered_map<std::string, HostImport>& imports, ModuleBuilder& builder, const Function& fn)
        : indices_(indices), imports_(imports), builder_(builder) {
        for (const auto& param : fn.params) variable(param, true);
    }
    FunctionSpec emit(const Function& fn) {
        emit_block(fn.body, true);
        // A real tail instruction keeps branch targets valid when a construct ends a function.
        code_.push_back(instruction(Opcode::Nop));
        const uint16_t zero = temporary(); code_.push_back(instruction(Opcode::LoadI64, zero)); code_.push_back(instruction(Opcode::Return, 0, zero));
        if (next_ > std::numeric_limits<uint16_t>::max()) throw std::runtime_error("register limit exceeded");
        return FunctionSpec{static_cast<uint16_t>(fn.params.size()), static_cast<uint16_t>(next_), std::move(code_)};
    }
  private:
    struct LoopContext { std::vector<size_t> breaks; std::vector<size_t> continues; };
    void patch(size_t index, size_t target) { code_[index].immediate = static_cast<int64_t>(target); }
    std::pair<size_t, size_t> branch_false(uint16_t condition) {
        const size_t negative = code_.size(); code_.push_back(instruction(Opcode::BranchTritNegative, 0, condition));
        const size_t zero = code_.size(); code_.push_back(instruction(Opcode::BranchTritZero, 0, condition));
        return {negative, zero};
    }
    void emit_block(const std::vector<Stmt>& statements, bool implicit_return = false) {
        for (size_t i = 0; i < statements.size(); ++i) {
            const Stmt& stmt = statements[i];
            if (stmt.type == Stmt::Type::If) {
                const auto branches = branch_false(expr(stmt.expr));
                const bool terminal = implicit_return && i + 1 == statements.size();
                emit_block(stmt.body, terminal);
                if (stmt.alternate.empty()) {
                    patch(branches.first, code_.size()); patch(branches.second, code_.size());
                } else {
                    const size_t jump_end = code_.size(); code_.push_back(instruction(Opcode::Jump));
                    patch(branches.first, code_.size()); patch(branches.second, code_.size());
                    emit_block(stmt.alternate, terminal); patch(jump_end, code_.size());
                }
                continue;
            }
            if (stmt.type == Stmt::Type::While) {
                const size_t loop = code_.size(); const auto branches = branch_false(expr(stmt.expr));
                loops_.push_back({{}, {}});
                emit_block(stmt.body); code_.push_back(instruction(Opcode::Jump, 0, 0, 0, static_cast<int64_t>(loop)));
                const size_t end = code_.size();
                patch(branches.first, end); patch(branches.second, end);
                for (const size_t jump : loops_.back().breaks) patch(jump, end);
                for (const size_t jump : loops_.back().continues) patch(jump, loop);
                loops_.pop_back(); continue;
            }
            if (stmt.type == Stmt::Type::For) {
                const uint16_t list = expr(stmt.expr); const uint16_t length = temporary();
                code_.push_back(instruction(Opcode::ListLength, length, list));
                const uint16_t index = temporary(); code_.push_back(instruction(Opcode::LoadI64, index));
                const size_t condition = code_.size(); const uint16_t test = temporary();
                code_.push_back(instruction(Opcode::CompareLtI64, test, index, length)); const auto branches = branch_false(test);
                const uint16_t item = variable(stmt.target, false); code_.push_back(instruction(Opcode::ListGet, item, list, index));
                loops_.push_back({{}, {}}); emit_block(stmt.body);
                const size_t increment = code_.size();
                const uint16_t one = temporary(); code_.push_back(instruction(Opcode::LoadI64, one, 0, 0, 1));
                code_.push_back(instruction(Opcode::AddI64, index, index, one));
                code_.push_back(instruction(Opcode::Jump, 0, 0, 0, static_cast<int64_t>(condition)));
                const size_t end = code_.size(); patch(branches.first, end); patch(branches.second, end);
                for (const size_t jump : loops_.back().breaks) patch(jump, end);
                for (const size_t jump : loops_.back().continues) patch(jump, increment);
                loops_.pop_back(); continue;
            }
            if (stmt.type == Stmt::Type::Try) {
                const size_t begin = code_.size(); code_.push_back(instruction(Opcode::TryBegin));
                emit_block(stmt.body); code_.push_back(instruction(Opcode::TryEnd));
                const size_t jump_end = code_.size(); code_.push_back(instruction(Opcode::Jump));
                patch(begin, code_.size()); const uint16_t caught = variable(stmt.target, false);
                code_.push_back(instruction(Opcode::Catch, caught)); emit_block(stmt.alternate); patch(jump_end, code_.size()); continue;
            }
            if (stmt.type == Stmt::Type::Throw) {
                const uint16_t value = expr(stmt.expr); code_.push_back(instruction(Opcode::Throw, 0, value)); continue;
            }
            if (stmt.type == Stmt::Type::Break) {
                if (loops_.empty()) throw std::runtime_error("break used outside a loop");
                loops_.back().breaks.push_back(code_.size()); code_.push_back(instruction(Opcode::Jump)); continue;
            }
            if (stmt.type == Stmt::Type::Continue) {
                if (loops_.empty()) throw std::runtime_error("continue used outside a loop");
                loops_.back().continues.push_back(code_.size()); code_.push_back(instruction(Opcode::Jump)); continue;
            }
            if (stmt.type == Stmt::Type::IndexAssign) {
                const uint16_t object = expr(stmt.assignment_target.children[0]);
                const uint16_t key = expr(stmt.assignment_target.children[1]);
                const uint16_t value = expr(stmt.expr);
                code_.push_back(instruction(Opcode::IndexSet, object, key, value)); continue;
            }
            const uint16_t value = expr(stmt.expr);
            if (stmt.type == Stmt::Type::Assign) {
                const uint16_t dst = variable(stmt.target, false);
                if (dst != value) code_.push_back(instruction(Opcode::Move, dst, value));
            }
            if (stmt.type == Stmt::Type::Return ||
                (implicit_return && i + 1 == statements.size() && stmt.type == Stmt::Type::Expression)) {
                code_.push_back(instruction(Opcode::Return, 0, value));
            }
        }
    }
    uint16_t temporary() { if (next_ == std::numeric_limits<uint16_t>::max()) throw std::runtime_error("register limit exceeded"); return static_cast<uint16_t>(next_++); }
    uint16_t variable(const std::string& name, bool declare) {
        auto found = variables_.find(name);
        if (found != variables_.end()) return found->second;
        if (!declare) { const uint16_t reg = temporary(); variables_.emplace(name, reg); return reg; }
        const uint16_t reg = temporary(); variables_.emplace(name, reg); return reg;
    }
    uint16_t expr(const Expr& e) {
        if (e.type == Expr::Type::Conditional) {
            const uint16_t condition = expr(e.children[0]);
            const auto branches = branch_false(condition);
            const uint16_t result = temporary();
            const uint16_t positive = expr(e.children[1]);
            code_.push_back(instruction(Opcode::Move, result, positive));
            const size_t jump_end = code_.size();
            code_.push_back(instruction(Opcode::Jump));
            patch(branches.first, code_.size());
            patch(branches.second, code_.size());
            const uint16_t alternate = expr(e.children[2]);
            code_.push_back(instruction(Opcode::Move, result, alternate));
            patch(jump_end, code_.size());
            return result;
        }
        if (e.type == Expr::Type::String) {
            const uint16_t dst = temporary(); const uint32_t index = builder_.add_string(e.value);
            code_.push_back(instruction(Opcode::LoadString, dst, 0, 0, index)); return dst;
        }
        if (e.type == Expr::Type::List) {
            std::vector<uint16_t> values; for (const Expr& item : e.children) values.push_back(expr(item));
            const uint16_t base = static_cast<uint16_t>(next_);
            for (const uint16_t value : values) { const uint16_t slot = temporary(); code_.push_back(instruction(Opcode::Move, slot, value)); }
            const uint16_t dst = temporary(); code_.push_back(instruction(Opcode::MakeList, dst, base, static_cast<uint16_t>(values.size()))); return dst;
        }
        if (e.type == Expr::Type::Map) {
            std::vector<uint16_t> values; for (const Expr& item : e.children) values.push_back(expr(item));
            const uint16_t base = static_cast<uint16_t>(next_);
            for (const uint16_t value : values) { const uint16_t slot = temporary(); code_.push_back(instruction(Opcode::Move, slot, value)); }
            const uint16_t dst = temporary(); code_.push_back(instruction(Opcode::MakeMap, dst, base, static_cast<uint16_t>(values.size() / 2))); return dst;
        }
        if (e.type == Expr::Type::Index) {
            const uint16_t list = expr(e.children[0]); const uint16_t index = expr(e.children[1]); const uint16_t dst = temporary();
            code_.push_back(instruction(Opcode::IndexGet, dst, list, index)); return dst;
        }
        if (e.type == Expr::Type::Number) {
            int64_t value = 0; const auto result = std::from_chars(e.value.data(), e.value.data() + e.value.size(), value);
            if (result.ec != std::errc{}) throw std::runtime_error("integer literal out of range at offset " + std::to_string(e.offset));
            const uint16_t dst = temporary(); code_.push_back(instruction(Opcode::LoadI64, dst, 0, 0, value)); return dst;
        }
        if (e.type == Expr::Type::Name) {
            if (e.value == "true" || e.value == "真") { const uint16_t dst = temporary(); code_.push_back(instruction(Opcode::LoadTrit, dst, 0, 0, 1)); return dst; }
            if (e.value == "false" || e.value == "假") { const uint16_t dst = temporary(); code_.push_back(instruction(Opcode::LoadTrit, dst, 0, 0, -1)); return dst; }
            if (e.value == "null" || e.value == "空") { const uint16_t dst = temporary(); code_.push_back(instruction(Opcode::LoadNull, dst)); return dst; }
            auto found = variables_.find(e.value);
            if (found != variables_.end()) return found->second;
            const auto function = indices_.find(e.value);
            if (function == indices_.end())
                throw std::runtime_error("undefined variable '" + e.value + "' at offset " + std::to_string(e.offset));
            const uint16_t dst = temporary();
            code_.push_back(instruction(Opcode::LoadFunction, dst, 0, 0,
                                        static_cast<int64_t>(function->second.index)));
            return dst;
        }
        if (e.type == Expr::Type::Unary) {
            const uint16_t value = expr(e.children[0]); const uint16_t dst = temporary();
            if (e.value == "!" || e.value == "not") code_.push_back(instruction(Opcode::TritNot, dst, value));
            else { const uint16_t zero = temporary(); code_.push_back(instruction(Opcode::LoadI64, zero)); code_.push_back(instruction(Opcode::SubI64, dst, zero, value)); }
            return dst;
        }
        if (e.type == Expr::Type::Call) {
            if (e.value == "push") {
                if (e.children.size() != 2)
                    throw std::runtime_error("push expects exactly two arguments at offset " +
                                             std::to_string(e.offset));
                const uint16_t list = expr(e.children[0]);
                const uint16_t value = expr(e.children[1]);
                code_.push_back(instruction(Opcode::ListAppend, list, value));
                return list;
            }
            if (e.value == "bind") {
                if (e.children.empty() || e.children[0].type != Expr::Type::Name)
                    throw std::runtime_error("bind expects a named function at offset " +
                                             std::to_string(e.offset));
                const auto target = indices_.find(e.children[0].value);
                if (target == indices_.end())
                    throw std::runtime_error("bind target is not a local function at offset " +
                                             std::to_string(e.offset));
                std::vector<uint16_t> captured;
                for (size_t index = 1; index < e.children.size(); ++index)
                    captured.push_back(expr(e.children[index]));
                const uint16_t base = static_cast<uint16_t>(next_);
                for (const uint16_t value : captured) {
                    const uint16_t slot = temporary();
                    code_.push_back(instruction(Opcode::Move, slot, value));
                }
                const uint16_t dst = temporary();
                code_.push_back(instruction(Opcode::MakeClosure, dst, base,
                                            static_cast<uint16_t>(captured.size()), target->second.index));
                return dst;
            }
            if (e.value == "len") {
                if (e.children.size() != 1)
                    throw std::runtime_error("len expects exactly one argument at offset " +
                                             std::to_string(e.offset));
                const uint16_t value = expr(e.children[0]);
                const uint16_t dst = temporary();
                code_.push_back(instruction(Opcode::ListLength, dst, value));
                return dst;
            }
            auto found = indices_.find(e.value);
            auto host = imports_.find(e.value);
            const auto function_value = variables_.find(e.value);
            if (found == indices_.end() && host == imports_.end() && function_value == variables_.end()) throw std::runtime_error("unknown function '" + e.value + "' at offset " + std::to_string(e.offset));
            if (host != imports_.end() && host->second.arity != e.children.size()) throw std::runtime_error("host function '" + e.value + "' argument count mismatch at offset " + std::to_string(e.offset));
            if (found != indices_.end() && found->second.arity != e.children.size()) throw std::runtime_error("function '" + e.value + "' argument count mismatch at offset " + std::to_string(e.offset));
            std::vector<uint16_t> actuals;
            for (const Expr& arg : e.children) actuals.push_back(expr(arg));
            const uint16_t base = static_cast<uint16_t>(next_);
            for (const uint16_t actual : actuals) { const uint16_t slot = temporary(); code_.push_back(instruction(Opcode::Move, slot, actual)); }
            const uint16_t dst = temporary();
            if (function_value != variables_.end())
                code_.push_back(instruction(Opcode::CallValue, dst, function_value->second, base,
                                            static_cast<int64_t>(e.children.size())));
            else if (host != imports_.end()) code_.push_back(instruction(Opcode::CallHost, dst, base, static_cast<uint16_t>(e.children.size()), host->second.index));
            else code_.push_back(instruction(Opcode::Call, dst, base, static_cast<uint16_t>(e.children.size()), found->second.index));
            return dst;
        }
        if (e.type == Expr::Type::Binary && e.value == "+" &&
            e.children[1].type == Expr::Type::List && e.children[1].children.size() == 1) {
            const uint16_t list = expr(e.children[0]);
            const uint16_t value = expr(e.children[1].children[0]);
            code_.push_back(instruction(Opcode::ListAppend, list, value));
            return list;
        }
        const std::string& op = e.value;
        const uint16_t left = expr(e.children[0]); const uint16_t right = expr(e.children[1]); const uint16_t dst = temporary();
        Opcode opcode = Opcode::AddI64;
        if (op == "-") opcode = Opcode::SubI64; else if (op == "*") opcode = Opcode::MulI64; else if (op == "/") opcode = Opcode::DivI64;
        else if (op == "%") opcode = Opcode::RemI64;
        else if (op == "==") opcode = Opcode::CompareEqI64; else if (op == "!=") opcode = Opcode::CompareNeI64;
        else if (op == "<") opcode = Opcode::CompareLtI64; else if (op == "<=") opcode = Opcode::CompareLeI64;
        else if (op == ">") opcode = Opcode::CompareGtI64; else if (op == ">=") opcode = Opcode::CompareGeI64;
        else if (op == "and") opcode = Opcode::TritAnd; else if (op == "or") opcode = Opcode::TritOr;
        code_.push_back(instruction(opcode, dst, left, right)); return dst;
    }
    const std::unordered_map<std::string, FunctionTarget>& indices_;
    const std::unordered_map<std::string, HostImport>& imports_;
    ModuleBuilder& builder_;
    std::unordered_map<std::string, uint16_t> variables_;
    std::vector<LoopContext> loops_;
    std::vector<Instruction> code_; uint32_t next_ = 0;
};

} // namespace

bool compile(std::string_view source, ModuleBuilder& builder, dao_error* error, Options options) {
    clear(error);
    try {
        Program program;
        std::unordered_set<std::string> active_imports;
        collect_program(source, {}, options, active_imports, program);
        if (program.functions.empty())
            throw std::runtime_error("expected at least one thought/function definition");
        std::unordered_map<std::string, Emitter::FunctionTarget> indices;
        for (uint32_t i = 0; i < program.functions.size(); ++i) {
            if (program.functions[i].params.size() > std::numeric_limits<uint16_t>::max()) throw std::runtime_error("too many parameters in function '" + program.functions[i].name + "'");
            if (!indices.emplace(program.functions[i].name, Emitter::FunctionTarget{i, static_cast<uint16_t>(program.functions[i].params.size())}).second) throw std::runtime_error("duplicate function '" + program.functions[i].name + "'");
        }
        std::unordered_map<std::string, Emitter::HostImport> imports;
        for (const Import& import : program.imports) {
            if (indices.contains(import.name)) throw std::runtime_error("host import conflicts with function '" + import.name + "'");
            const auto existing = imports.find(import.name);
            if (existing != imports.end()) {
                if (existing->second.arity != import.arity)
                    throw std::runtime_error("host import '" + import.name + "' has conflicting arities");
                continue;
            }
            const uint32_t index = builder.add_import(symbol_id(import.name), import.arity);
            imports.emplace(import.name, Emitter::HostImport{index, import.arity});
        }
        for (const Function& fn : program.functions) {
            const uint32_t index = builder.add_function(Emitter(indices, imports, builder, fn).emit(fn));
            builder.add_export(symbol_id(fn.name), index);
        }
        return true;
    } catch (const std::exception& ex) {
        return fail(error, ex.what());
    }
}

} // namespace dao::km
