# save as convert_to_scarlet_prefix.py
import re
from pathlib import Path
from typing import List, Tuple

# IN = Path("spec.ltl")
# OUT = Path("spec.prefix.scarlet")


def convert_to_scarlet(src: str) -> str:

    # 1) squash whitespace, normalize booleans, drop quotes around APs
    s = " ".join(src.split())
    s = re.sub(r"\btrue\b", "1", s, flags=re.I)
    s = re.sub(r"\bfalse\b", "0", s, flags=re.I)
    s = s.replace('"', "")  # remove quotes if any snuck in

    # 2) tokenization
    TOK_RE = re.compile(r"""(<->|->|&&|\|\||!|X|F|G|U|R|W|M|V|\(|\)|[01]|[A-Za-z_][A-Za-z0-9_]*)""")
    tokens: List[str] = [t for t in TOK_RE.findall(s) if t.strip()]
    pos = 0

    # Operator metadata
    UNARY = {"!", "X", "F", "G"}
    BINARY = {"<->", "->", "U", "R", "W", "M", "V", "&&", "||"}
    PREC = {
        "<->": 1,
        "->": 2,
        "||": 3,
        "&&": 4,
        "U": 5,
        "R": 5,
        "W": 5,
        "M": 5,
        "V": 5,
    }
    RIGHT_ASSOC = {"->", "<->", "U", "R", "W", "M", "V"}

    def peek() -> str | None:
        return tokens[pos] if pos < len(tokens) else None

    def take(expected: str | None = None) -> str:
        global pos
        if pos >= len(tokens):
            raise ValueError("Unexpected end of input")
        t = tokens[pos]
        if expected and t != expected:
            raise ValueError(f"Expected '{expected}', got '{t}' near token {pos}")
        pos += 1
        return t

    # 3) Pratt parser for fully parenthesized LTL (works fine even if not fully)
    def parse_expr(min_prec=0):
        t = take()
        # primary / prefix-unary
        if t == "(":
            node = parse_expr(0)
            take(")")
        elif t in UNARY:
            rhs = parse_expr(100)  # unary binds tight
            node = (t, (rhs,))
        elif t in {"0", "1"} or re.match(r"[A-Za-z_]\w*$", t):
            node = ("var", t)
        else:
            raise ValueError(f"Unexpected token '{t}' at start of expression")

        # infix binaries
        while True:
            op = peek()
            if op not in BINARY:
                break
            prec = PREC[op]
            if prec < min_prec:
                break
            take()  # consume operator
            next_min = prec if op in RIGHT_ASSOC else prec + 1
            rhs = parse_expr(next_min)
            node = (op, (node, rhs))
        return node

    ast = parse_expr()
    if pos != len(tokens):
        raise ValueError("Trailing tokens after parse")

    # 4) render to Scarlet prefix (function-style)
    def render(n) -> str:
        op = n[0]
        if op == "var":
            return n[1]
        if op in UNARY:
            return f"{op}({render(n[1][0])})"
        # map &&/|| to &/|
        if op == "&&":
            op2 = "&"
        elif op == "||":
            op2 = "|"
        else:
            op2 = op
        a, b = n[1]
        return f"{op2}({render(a)},{render(b)})"

    pref = render(ast)

    # 5) rename APs that collide with operator names (only when NOT used as op)
    #    We do this on the final string: replace standalone names not followed by '('
    colliders = ["X", "F", "G", "U", "R", "W", "M", "V"]
    renames = {}

    def safe_rename(txt: str, tok: str, repl: str) -> str:
        # \bTOK\b not followed by '('  (i.e., it's a variable, not an operator call)
        return re.sub(rf"\b{tok}\b(?!\()", repl, txt)

    for c in colliders:
        repl = f"{c}_ap"
        newpref = safe_rename(pref, c, repl)
        if newpref != pref:
            renames[c] = repl
            pref = newpref

    print(f"Wrote ({len(pref)} chars)")

    if renames:
        print("Renamed APs to avoid operator collisions:", renames)

    return pref + "\n"
