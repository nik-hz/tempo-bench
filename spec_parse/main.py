import argparse
import re
from pathlib import Path
import subprocess
from typing import Tuple

from convert_to_scarlet_prefix import convert_to_scarlet
from fix_formulas_for_scarlet import Parser, rewrite, render


def run_syfco(tlsf_file) -> Tuple[str]:
    result = subprocess.run(
        ["syfco", "-f", "ltl", "-m", "fully", tlsf_file],
        capture_output=True,
        text=True,
        check=True,
    )
    ins = subprocess.run(
        ["syfco", "-ins", tlsf_file],
        capture_output=True,
        text=True,
        check=True,
    )
    outs = subprocess.run(
        ["syfco", "-outs", tlsf_file],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout, ins.stdout, outs.stdout


def main():
    parser = argparse.ArgumentParser(
        prog="spec_tracer",
        usage="%(prog)s: check readme how to run in docker",
        description="converts a tlsf specification into a trace",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("-f", "--filepath", default="tlsf_specs/amba_case_study.tlsf")
    parser.add_argument("-o", "--output_dir", default="tlsf_specs")
    args = parser.parse_args()

    """ Convert to scarlet format, capture alphabet"""
    print(f"Running syfco to convert {args.filepath}...")
    ltl, ins, outs = run_syfco(args.filepath)

    ltl.replace("\n", "").lstrip()
    alphabet = re.sub(r"\s+", ",", f"{ins} {outs}").strip(
        ", "
    )  # concat ins and outs and replace whitespace

    conv_ltl = f"{ltl};{alphabet}\n"

    """ convert from infix to postfix for scarlet """
    postfix_ltl = convert_to_scarlet(conv_ltl)

    formula_text, alphabet = postfix_ltl.split(";", 1)
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


if __name__ == "__main__":
    main()
