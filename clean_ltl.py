import re
from pathlib import Path

IN = Path("spec.ltl")
OUT = Path("spec.clean.ltl")

text = IN.read_text()

# 1) Normalize whitespace to make regexes predictable (keep parentheses)
#    Keep it as a single line; Spot doesn't need newlines.
one_line = " ".join(text.split())

# 2) Replace booleans with Spot-safe constants
#    Use word boundaries so we don't touch identifiers like 'serve'.
one_line = re.sub(r"\btrue\b", "1", one_line, flags=re.IGNORECASE)
one_line = re.sub(r"\bfalse\b", "0", one_line, flags=re.IGNORECASE)

# 3) Defensively rename APs that could collide with temporal operators,
#    but only when they are NOT used as an operator (i.e., not followed by '(' after optional spaces).
#    Example: 'X (p)' is the NEXT operator; 'X' alone becomes 'X_ap'.
colliding = ["X", "F", "G", "U", "R", "W", "M", "V"]
renames = {}

def rename_token(s: str, tok: str, repl: str):
    # Replace standalone token not followed by '(' (after optional spaces)
    pattern = rf"\b{tok}\b(?!\s*\()"
    return re.sub(pattern, repl, s)

clean = one_line
for tok in colliding:
    repl = f"{tok}_ap"
    # Check if token appears in a non-operator position
    if re.search(rf"\b{tok}\b(?!\s*\()", clean):
        clean2 = rename_token(clean, tok, repl)
        if clean2 != clean:
            renames[tok] = repl
            clean = clean2

# 4) (Optional) ensure ASCII equivalence operator if any unicode sneaks in
clean = clean.replace("↔", "<->").replace("→", "->").replace("¬", "!")

# 5) Write output
OUT.write_text(clean + "\n")

print(f"Wrote {OUT.name}")
if renames:
    print("Renamed potential operator-like APs:")
    for k, v in renames.items():
        print(f"  {k} -> {v}")
else:
    print("No operator-like APs found to rename.")
