import re
from pathlib import Path


# ---------- helpers: simple prefix parser ----------
class Parser:
    def __init__(self, s: str):
        # regex out the (true, p) to p
        self.s = s
        self.i = 0
        self.n = len(s)

    def peek(self):
        while self.i < self.n and self.s[self.i].isspace():
            self.i += 1
        return self.s[self.i] if self.i < self.n else ""

    def take(self, k=1):
        ch = self.s[self.i : self.i + k]
        self.i += k
        return ch

    def expect(self, lit: str):
        assert (
            self.s[self.i : self.i + len(lit)] == lit
        ), f"expected '{lit}' near {self.s[self.i:self.i+20]}"
        self.i += len(lit)

    def ident(self):
        # operator tokens or identifiers
        # ops can be: !, &, |, ->, <->, U, R, W, M, V, G, F, X
        # idents: [A-Za-z_][A-Za-z0-9_]*
        c = self.peek()
        if c == "!":
            self.take()
            return "!"
        if c in "&|":
            self.take()
            return c
        if c == "-":
            self.expect("->")
            return "->"
        if c == "<":
            self.expect("<->")
            return "<->"
        if c.isalpha() or c == "_":
            j = self.i
            while self.i < self.n and (self.s[self.i].isalnum() or self.s[self.i] == "_"):
                self.i += 1
            return self.s[j : self.i]
        if c in "01":
            self.take()
            return c
        raise ValueError(f"unexpected char '{c}' at {self.i}")

    def parse_expr(self):
        t = self.ident()
        # constants / variables
        if t in ("0", "1"):
            return ("const", t)
        # variables (not followed by '(')
        if t not in ("!", "&", "|", "->", "<->", "U", "R", "W", "M", "V", "G", "F", "X"):
            return ("var", t)

        # operators: must have '(' expr [ , expr ] ')'
        # arity
        if t in ("!", "G", "F", "X"):
            self.expect("(")
            a = self.parse_expr()
            self.expect(")")
            return (t, (a,))
        else:
            # binary
            self.expect("(")
            a = self.parse_expr()
            self.expect(",")
            b = self.parse_expr()
            self.expect(")")
            return (t, (a, b))


def rewrite(ast):
    """Rewrite <-> and W; leave others as-is."""
    op = ast[0]
    if op == "const" or op == "var":
        return ast
    if op in ("!", "G", "F", "X"):
        return (op, (rewrite(ast[1][0]),))
    # binary
    a, b = ast[1]
    a2, b2 = rewrite(a), rewrite(b)
    if op == "<->":
        # &(->(a,b), ->(b,a))
        return ("&", (("->", (a2, b2)), ("->", (b2, a2))))
    if op == "W":
        # |(G(a), U(a,b))
        return ("|", (("G", (a2,)), ("U", (a2, b2))))
    return (op, (a2, b2))


def render(ast):
    op = ast[0]
    if op == "const":
        return "true" if ast[1] == "1" else "false"
    if op == "var":
        return ast[1]
    if op in ("!", "G", "F", "X"):
        return f"{op}({render(ast[1][0])})"
    # binary pretty names
    op_map = {"&": "&", "|": "|", "->": "->", "U": "U", "R": "R", "M": "M", "V": "V"}
    if op not in op_map:
        raise ValueError(f"unsupported operator after rewrite: {op}")
    a, b = ast[1]
    return f"{op_map[op]}({render(a)},{render(b)})"


if __name__ == "__main__":
    # ---------- process formulas.txt ----------
    line = Path("formulas.txt").read_text().strip()
    if ";" not in line:
        raise SystemExit("formulas.txt must be a single line: <formula>;<alphabet>")

    formula_text, alphabet = line.split(";", 1)
    p = Parser(formula_text)
    ast = p.parse_expr()
    if p.peek():
        # extra input leftover
        pass

    ast2 = rewrite(ast)
    fixed = render(ast2)
    fixed_cleaned = re.sub(r"true,(.*?)", r"\1", fixed)

    out_line = f"{fixed_cleaned};{alphabet.strip()}"
    Path(".txt").write_text(out_line + "\n")
    print("Wrote formulas_scarlet.txt")
