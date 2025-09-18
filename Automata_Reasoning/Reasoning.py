# -*- coding: utf-8 -*-
"""
Reasoning trace generation module for extracting causal explanations from traces.
"""

import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple, Union


def parse_hoa(hoa_file: Union[str, os.PathLike]) -> Tuple[List[str], int, Dict[int, List[Tuple[str, int]]]]:
    """
    Parse a causal HOA file into (APs, start_state, states).

    Args:
        hoa_file: Path to HOA file

    Returns:
        Tuple of (ap_names, start_state, states_dict)
    """
    with open(hoa_file, encoding="utf-8") as f:
        lines = f.readlines()

    ap_names = []
    start = None
    states = {}
    current_state = None

    for line in lines:
        line = line.strip()
        if line.startswith("AP:"):
            ap_names = re.findall(r'"([^"]+)"', line)
        elif line.startswith("Start:"):
            start = int(line.split()[1])
        elif line.startswith("State:"):
            parts = line.split()
            current_state = int(parts[1])
            states[current_state] = []
        elif line.startswith("["):
            cond, target = line.split("]")
            cond = cond[1:].strip()
            target = int(target.strip())
            states[current_state].append((cond, target))

    return ap_names, start, states


def parse_trace_file(trace_file: Union[str, os.PathLike]) -> List[List[str]]:
    """
    Split trace into time steps with literal valuations.

    Args:
        trace_file: Path to trace file

    Returns:
        List of steps, where each step is a list of literals
    """
    with open(trace_file, encoding="utf-8") as f:
        text = f.read().strip()

    # Remove cycle notation if present
    if "cycle{" in text:
        text = text.split("cycle{")[0].rstrip(";")

    steps = text.split(";")
    parsed = []
    for step in steps:
        lits = step.split("&")
        parsed.append([lit.strip() for lit in lits if lit.strip()])
    return parsed


def parse_effects(effect_file: Union[str, os.PathLike]) -> Set[str]:
    """
    Read effect.txt and return the set of APs that count as outputs.

    Args:
        effect_file: Path to effect file

    Returns:
        Set of output AP names
    """
    with open(effect_file, encoding="utf-8") as f:
        text = f.read().strip()
    # Expect format: <>(ap1)&<>(ap2)&...
    aps = re.findall(r"<>\(([^)]+)\)", text)
    return set(aps)


def project_inputs(step: List[str], input_aps: List[str]) -> Dict[int, bool]:
    """
    Extract valuation for inputs from a full trace step.

    Args:
        step: List of literals in the step
        input_aps: List of atomic propositions

    Returns:
        Dictionary mapping AP indices to boolean values
    """
    vals = {}
    for idx, ap in enumerate(input_aps):
        if "!" + ap in step:
            vals[idx] = False
        elif ap in step:
            vals[idx] = True
        else:
            vals[idx] = None
    return vals


def project_outputs(step: List[str], effect_aps: Set[str]) -> List[str]:
    """
    Extract outputs based only on effect_aps.

    Args:
        step: List of literals in the step
        effect_aps: Set of output AP names

    Returns:
        List of output APs that are true in this step
    """
    outputs = []
    for lit in step:
        name = lit.replace("!", "")
        if name in effect_aps and not lit.startswith("!"):
            outputs.append(name)
    return outputs


def matches_condition(cond: str, valuation: Dict[int, bool]) -> bool:
    """
    Check if a condition like '!0&!1 | 2' matches a valuation dict.

    Args:
        cond: Condition string from HOA file
        valuation: Dictionary mapping AP indices to boolean values

    Returns:
        True if condition matches, False otherwise
    """
    if cond == "t":  # unconditional transition
        return True

    expr = cond
    expr = expr.replace("!", " not ")
    for idx, val in valuation.items():
        token = str(idx)
        if val is True:
            expr = re.sub(rf"\b{token}\b", "True", expr)
        elif val is False:
            expr = re.sub(rf"\b{token}\b", "False", expr)
        else:
            expr = re.sub(rf"\b{token}\b", "False", expr)

    expr = expr.replace("&", " and ").replace("|", " or ")
    try:
        return eval(expr)
    except Exception as e:
        print(f"Error evaluating condition '{cond}' as '{expr}': {e}")
        return False


def generate_reasoning_trace(
    causal_hoa_file: Union[str, os.PathLike],
    trace_file: Union[str, os.PathLike],
    effect_file: Union[str, os.PathLike],
    output_file: Union[str, os.PathLike] = "reasoning.txt"
) -> None:
    """
    Generate a reasoning trace explaining causal relationships in the trace.

    Args:
        causal_hoa_file: Path to causal HOA file
        trace_file: Path to trace file
        effect_file: Path to effect specification file
        output_file: Path to save reasoning trace
    """
    input_aps, start_state, states = parse_hoa(causal_hoa_file)
    trace_steps = parse_trace_file(trace_file)
    effect_aps = parse_effects(effect_file)

    output_path = Path(output_file)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    state = start_state
    with output_path.open("w", encoding="utf-8") as f:
        f.write("==== Reasoning Trace ====\n")
        for t, step in enumerate(trace_steps):
            inputs = project_inputs(step, input_aps)
            outputs = project_outputs(step, effect_aps)

            # --- transition matching ---
            next_state, cond = None, None
            for c, target in states[state]:
                if matches_condition(c, inputs):
                    next_state, cond = target, c
                    break

            # --- reporting ---
            active_inputs = [f"AP{idx}" for idx, val in inputs.items() if val]
            pretty_inputs = [input_aps[idx] for idx, val in inputs.items() if val]

            f.write(f"\nTime {t}:\n")
            f.write(f"  Inputs true: {', '.join(active_inputs) if active_inputs else '∅'}\n")
            f.write(f"  Outputs true: {', '.join(outputs) if outputs else '∅'}\n")

            if next_state is None:
                f.write(f"  Transition: {state} -> (no valid transition)\n")
                f.write(f"  Interpretation: No condition matched for inputs.\n")
                break
            else:
                f.write(f"  Transition: {state} -> {next_state} via [{cond}]\n")
                f.write(
                    f"  Interpretation: Because inputs "
                    f"{{{', '.join(pretty_inputs) if pretty_inputs else '∅'}}} were true, "
                    f"system caused outputs {{{', '.join(outputs) if outputs else '∅'}}}.\n"
                )
                state = next_state


# For backward compatibility
run_reasoning_trace = generate_reasoning_trace


def main():
    """Command-line interface for reasoning trace generation."""
    if len(sys.argv) != 4:
        print("Usage: python reasoning.py <causal.hoa> <trace.txt> <effect.txt>")
        sys.exit(1)

    causal_hoa_file = sys.argv[1]
    trace_file = sys.argv[2]
    effect_file = sys.argv[3]

    generate_reasoning_trace(causal_hoa_file, trace_file, effect_file)
    print("Reasoning trace generated successfully")


if __name__ == "__main__":
    main()