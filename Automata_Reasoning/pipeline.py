"""
Automata pipeline
Authors: Nikolaus Holzer, Will Fishell
Date: September 2025 

Pipeline to convert a tlsf input file into hoa and run clis to extract reasoning traces

NOTES:
If we give it a lot of traces, it can figure out the legal transitions
"""
import re
from pathlib import Path
import spot
import hoax


def parse_tlsf(tlsf_file: str):
    """
    Minimal TLSF parser: extract INPUTS, OUTPUTS, and the formula.
    This assumes a standard TLSF structure.
    """
    content = Path(tlsf_file).read_text()

    # Extract INPUTS { ... }
    inputs_match = re.search(r"INPUTS\s*{([^}]*)}", content, re.MULTILINE)
    outputs_match = re.search(r"OUTPUTS\s*{([^}]*)}", content, re.MULTILINE)
    formula_match = re.search(r"FORMULA\s*{([^}]*)}", content, re.MULTILINE | re.DOTALL)

    inputs = []
    outputs = []
    formula = None

    if inputs_match:
        inputs = [tok.strip(" ;\n\r\t") for tok in inputs_match.group(1).split(",") if tok.strip()]
    if outputs_match:
        outputs = [
            tok.strip(" ;\n\r\t") for tok in outputs_match.group(1).split(",") if tok.strip()
        ]
    if formula_match:
        formula = formula_match.group(1).strip()

    return inputs, outputs, formula


def synthesize_from_tlsf_formula(formula_str: str):
    """Translate TLSF formula into a Spot automaton."""
    f = spot.formula(formula_str)
    aut = spot.translate(f)
    return aut


def extract_aps(aut: spot.automaton):
    """Extract atomic propositions from a Spot automaton."""
    return [str(ap) for ap in aut.ap()]


def run_hoax(aut: spot.automaton, config_file: str):
    """Run Hoax executor directly from Python (using bindings)."""
    executor = hoax.Executor(aut, config_file=config_file)
    # This depends on hoax API — assume generate() returns a list of sets of APs
    raw_traces = executor.generate()
    return raw_traces


def clean_and_generate_trace(raw_traces, aps):
    """Convert Hoax raw traces into a Spot word string."""
    trace = []
    for step in raw_traces:
        assignment = []
        for ap in aps:
            if ap in step:
                assignment.append(ap)
            else:
                assignment.append("!" + ap)
        trace.append("&".join(assignment))
    return ";".join(trace) + ";cycle{1}"


def check_acceptance(aut: spot.automaton, trace: str):
    """Check if automaton accepts the given trace."""
    word = spot.parse_word(trace, aut.get_dict())
    run = spot.accepting_run(aut, word)
    return run is not None


def pipeline_tlsf(tlsf_file: str, config_file: str):
    """Full pipeline starting from a TLSF file."""
    inputs, outputs, formula = parse_tlsf(tlsf_file)
    if not formula:
        raise ValueError(f"No formula found in {tlsf_file}")

    aut = synthesize_from_tlsf_formula(formula)
    aps = extract_aps(aut)

    raw_traces = run_hoax(aut, config_file)
    trace = clean_and_generate_trace(raw_traces, aps)

    accepted = check_acceptance(aut, trace)

    return {
        "tlsf_file": tlsf_file,
        "inputs": inputs,
        "outputs": outputs,
        "formula": formula,
        "aps": aps,
        "trace": trace,
        "accepted": accepted,
        "states": aut.num_states(),
        "edges": aut.num_edges(),
    }


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 3:
        print("Usage: python pipeline_tlsf.py spec.tlsf pipeline_configs.toml")
        sys.exit(1)

    tlsf_file = sys.argv[1]
    config_file = sys.argv[2]

    result = pipeline_tlsf(tlsf_file, config_file)
    print(result)
