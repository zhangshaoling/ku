"""Python implementation of 道 lexer (mirrors std/lexer.道)."""

import re

_KEYWORDS = {"thought", "if", "else", "while", "for", "in", "return",
             "break", "continue", "try", "catch", "throw", "true", "false",
             "null", "and", "or", "not", "import", "as",
             "思", "若", "否", "当", "遍", "返",
             "断", "续", "试", "捕", "抛", "终", "真", "假",
             "空", "且", "或", "非", "引", "别",
             "设", "函", "己", 
             "于"}

_OPS = {"+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=",
        "=", "!", "and", "or", "not", "且", "或", "非"}


def lex(source: str) -> list[dict]:
    """Lex 道 source code into tokens. Each token has 'pos' (char offset in source)."""
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

        # Chinese number
        _cn_digits = "零〇一二两三四五六七八九"
        _cn_units = "十百千万亿点"
        if ch in _cn_digits or (ch in _cn_units and i + 1 < n and (source[i+1] in _cn_digits or source[i+1].isdigit())):
            _cn_num, i, line, col = _lex_cn_number(source, i, line, col)
            tokens.append({"type": "number", "value": _cn_num, "line": line, "col": col, "pos": pos})
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
    while i < n and (source[i].isalnum() or source[i] == "_" or "\u4e00" <= source[i] <= "\u9fff"):
        i += 1
        col += 1
    return source[start:i], i, line, col

_CN_DIGITS = {
    chr(0x96f6): 0, chr(0x3007): 0,  # 零〇
    chr(0x4e00): 1,                   # 一
    chr(0x4e8c): 2, chr(0x4e24): 2,  # 二两
    chr(0x4e09): 3,                   # 三
    chr(0x56db): 4,                   # 四
    chr(0x4e94): 5,                   # 五
    chr(0x516d): 6,                   # 六
    chr(0x4e03): 7,                   # 七
    chr(0x516b): 8,                   # 八
    chr(0x4e5d): 9,                   # 九
    chr(0x5341): 10,                  # 十
    chr(0x767e): 100,                 # 百
    chr(0x5343): 1000,                # 千
    chr(0x4e07): 10000,               # 万
    chr(0x4ebf): 100000000,           # 亿
}

def _lex_cn_number(source, i, line, col):
    start = i
    n = len(source)
    result = 0
    current = 0
    has_unit = False
    
    while i < n:
        ch = source[i]
        if ch in _CN_DIGITS:
            val = _CN_DIGITS[ch]
            if val >= 10:
                has_unit = True
                if current == 0:
                    current = 1
                result += current * val
                current = 0
            else:
                current = current * 10 + val
            i += 1
            col += 1
        elif ch == chr(0x70b9):  # 点
            i += 1
            col += 1
            result += current
            current = 0
            decimal_str = ''
            while i < n and source[i] in _CN_DIGITS and _CN_DIGITS[source[i]] < 10:
                decimal_str += str(_CN_DIGITS[source[i]])
                i += 1
                col += 1
            if decimal_str:
                result += float('0.' + decimal_str)
            break
        elif ch.isdigit():
            current = current * 10 + int(ch)
            i += 1
            col += 1
        else:
            break
    
    result += current
    num_str = source[start:i]
    return source[start:i], i, line, col

