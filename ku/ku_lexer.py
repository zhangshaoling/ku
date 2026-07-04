"""Python implementation of Ku lexer (mirrors std/lexer.ku)."""

import re

_KEYWORDS = {"thought", "if", "else", "while", "for", "in", "return",
             "break", "continue", "try", "catch", "throw", "true", "false",
             "null", "and", "or", "not", "import", "as",
             "思", "若", "否则", "当", "遍", "于", "返", "断", "续",
             "真", "假", "空", "且", "或", "非", "试", "捕", "抛",
             "记", "忆", "忘", "强", "弱", "联", "关联", "匹", "为", "则", "终匹"}

_OPS = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=",
        "=", "!", "and", "or", "not"}


def lex(source: str) -> list[dict]:
    """Lex Ku source code into tokens. Each token has 'pos' (char offset in source)."""
    tokens = []
    i = 0
    line = 1
    col = 1
    n = len(source)

    while i < n:
        ch = source[i]

        # Newline
        if ch == "\n":
            tokens.append({"type": "newline", "value": "\n", "line": line, "col": col, "pos": i})
            line += 1
            col = 1
            i += 1
            continue

        # Whitespace (not newline)
        if ch in " \t\r":
            i += 1
            col += 1
            continue

        # Line comment // or ;;
        if i + 1 < n and (source[i:i+2] == "//" or source[i:i+2] == ";;"):
            while i < n and source[i] != "\n":
                i += 1
                col += 1
            continue

        # Block comment /* ... */
        if i + 1 < n and source[i:i+2] == "/*":
            i += 2
            col += 2
            depth = 1
            while i < n and depth > 0:
                if i + 1 < n and source[i:i+2] == "/*":
                    depth += 1
                    i += 2
                    col += 2
                elif i + 1 < n and source[i:i+2] == "*/":
                    depth -= 1
                    i += 2
                    col += 2
                else:
                    if source[i] == "\n":
                        line += 1
                        col = 1
                    else:
                        col += 1
                    i += 1
            continue

        pos = i  # char offset of this token

        # String (double quote)
        if ch == '"':
            s, i, line, col = _lex_string_dq(source, i, line, col)
            tokens.append({"type": "string", "value": s, "line": line, "col": col, "pos": pos})
            continue

        # String (single quote)
        if ch == "'":
            s, i, line, col = _lex_string_sq(source, i, line, col)
            tokens.append({"type": "string", "value": s, "line": line, "col": col, "pos": pos})
            continue

        # Number
        if ch.isdigit() or (ch == "." and i + 1 < n and source[i+1].isdigit()):
            num, i, line, col = _lex_number(source, i, line, col)
            tokens.append({"type": "number", "value": num, "line": line, "col": col, "pos": pos})
            continue

        # Identifier / keyword
        if ch.isalpha() or ch == "_":
            ident, i, line, col = _lex_ident(source, i, line, col)
            if ident in _KEYWORDS:
                tokens.append({"type": "keyword", "value": ident, "line": line, "col": col, "pos": pos})
            else:
                tokens.append({"type": "name", "value": ident, "line": line, "col": col, "pos": pos})
            continue

        # Two-char operators
        if i + 1 < n:
            two = source[i:i+2]
            if two in ("==", "!=", "<=", ">=", "|>", "->"):
                tokens.append({"type": "op", "value": two, "line": line, "col": col, "pos": pos})
                i += 2
                col += 2
                continue

        # Single-char operators
        if ch in "+-*/%<>=!":
            tokens.append({"type": "op", "value": ch, "line": line, "col": col, "pos": pos})
            i += 1
            col += 1
            continue

        # Punctuation
        if ch in "(){}[]:,.":
            tokens.append({"type": "punct", "value": ch, "line": line, "col": col, "pos": pos})
            i += 1
            col += 1
            continue

        # Unknown character — skip
        i += 1
        col += 1

    tokens.append({"type": "eof", "value": "", "line": line, "col": col})
    return tokens


def _lex_number(source, i, line, col):
    start = i
    n = len(source)
    has_dot = False
    while i < n:
        ch = source[i]
        if ch.isdigit():
            i += 1
            col += 1
        elif ch == "." and not has_dot:
            has_dot = True
            i += 1
            col += 1
        else:
            break
    return source[start:i], i, line, col


def _lex_string_dq(source, i, line, col):
    """Lex double-quoted string."""
    i += 1  # skip opening "
    col += 1
    n = len(source)
    parts = []
    while i < n:
        ch = source[i]
        if ch == "\\":
            i += 1
            col += 1
            if i < n:
                esc = source[i]
                if esc == "n": parts.append("\n")
                elif esc == "t": parts.append("\t")
                elif esc == "r": parts.append("\r")
                elif esc == "\\": parts.append("\\")
                elif esc == '"': parts.append('"')
                else: parts.append(esc)
                i += 1
                col += 1
        elif ch == '"':
            i += 1
            col += 1
            return "".join(parts), i, line, col
        elif ch == "\n":
            parts.append(ch)
            i += 1
            line += 1
            col = 1
        else:
            parts.append(ch)
            i += 1
            col += 1
    return "".join(parts), i, line, col


def _lex_string_sq(source, i, line, col):
    """Lex single-quoted string."""
    i += 1  # skip opening '
    col += 1
    n = len(source)
    parts = []
    while i < n:
        ch = source[i]
        if ch == "\\":
            i += 1
            col += 1
            if i < n:
                esc = source[i]
                if esc == "n": parts.append("\n")
                elif esc == "t": parts.append("\t")
                elif esc == "r": parts.append("\r")
                elif esc == "\\": parts.append("\\")
                elif esc == "'": parts.append("'")
                else: parts.append(esc)
                i += 1
                col += 1
        elif ch == "'":
            i += 1
            col += 1
            return "".join(parts), i, line, col
        elif ch == "\n":
            parts.append(ch)
            i += 1
            line += 1
            col = 1
        else:
            parts.append(ch)
            i += 1
            col += 1
    return "".join(parts), i, line, col


def _lex_ident(source, i, line, col):
    start = i
    n = len(source)
    while i < n and (source[i].isalnum() or source[i] == "_"):
        i += 1
        col += 1
    return source[start:i], i, line, col
