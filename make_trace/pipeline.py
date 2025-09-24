"""
Automata pipeline
Authors: Nikolaus Holzer, Will Fishell
Date: September 2025

Pipeline to convert a tlsf input file into hoa and run clis to extract reasoning traces

NOTES:
If we give it a lot of traces, it can figure out the legal transitions
"""

import os
import subprocess
import re
from pathlib import Path
import logging
import sys, getopt


# local imports to abstract away the corp call
# import parse
# import spot
# import cause

logging.basicConfig(level=logging.ERROR)

os.environ["PATH"] = "/usr/local/bin:" + os.environ["PATH"]
os.environ["LD_LIBRARY_PATH"] = "/usr/local/lib:" + os.environ.get("LD_LIBRARY_PATH", "")


def run_ltlsynt(tlsf_file: Path, hoa_file: Path):
    """Run ltlsynt on a TLSF file, strip the first line, and save HOA."""
    cmd = ["ltlsynt", "--tlsf", str(tlsf_file)]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except Exception as e:
        logging.error(f"Error: {e}")
    # Drop first line
    lines = res.stdout.splitlines()[1:]
    hoa_file.write_text("\n".join(lines))


def extract_aps(hoa_file: Path):
    """Extract atomic propositions from line 4 of the HOA file."""
    with open(hoa_file) as f:
        for i, line in enumerate(f, start=1):
            if i == 4:
                return re.findall(r'"([^"]+)"', line)
    return []


def make_replacement(aps):
    """Build replacement string for empty set() in hoax output."""
    parts = [f"'!{ap}'" for ap in aps]
    return "{" + ", ".join(parts) + "}"


def run_hoax(hoa_file: Path, hoax_file: Path, config_file: Path, aps):
    """Run hoax with config, clean its output, and save result."""
    print(config_file)
    cmd = ["hoax", str(hoa_file), "--config", str(config_file)]

    try:
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    except Exception as e:
        logging.error(f"Error: {e}")

    # Drop last line (like `sed '$d'`)
    lines = res.stdout.strip().splitlines()[:-1]
    content = "\n".join(lines)

    # Replace set() with {!ap...}
    replacement = make_replacement(aps)
    content = re.sub(r"set\(\)", replacement, content)

    hoax_file.write_text(content)


def generate_trace(hoax_file: Path, aps):
    """Generate trace string from hoax output (Spot word)."""
    trace = []
    for line in hoax_file.read_text().splitlines():
        raw = re.search(r"{(.*)}", line)
        if not raw:
            continue
        present = [tok.strip("'\" ") for tok in raw.group(1).split(",") if tok.strip()]
        assignment = [ap if ap in present else f"!{ap}" for ap in aps]
        trace.append("&".join(assignment))
    return ";".join(trace) + ";cycle{1}"


def run_autfilt_stats(hoa_file: Path, stats_file: Path):
    """Show automaton stats via autfilt."""
    print("[+] Automaton stats:")
    with open(stats_file, "w") as f:
        subprocess.run(
            ["autfilt", "--stats=%s states, %e edges, %a acc-sets, %c SCCs, det=%d"],
            input=hoa_file.read_text(),
            text=True,
            stdout=f,
            check=True,
        )


def run_autfilt_accept(hoa_file: Path, trace: str, output_file: Path):
    """Check acceptance of a trace in automaton using autfilt."""
    cmd = ["autfilt", f"--accept-word={trace}"]
    res = subprocess.run(cmd, input=hoa_file.read_bytes(), capture_output=True)
    output_file.write_bytes(res.stdout)
    return res.returncode == 0


# TODO implement the proposition stuff with spot outputs
def extract_effects(
    tlsf_file: Path,
    trace_file: Path,
    effects_file: Path,
    outputs_file: Path,
):
    """Extracts the output parameters of the tlsf file."""

    # run syfco to get outputs
    cmd = ["syfco", tlsf_file, "-outs"]
    try:
        res = subprocess.run(cmd, capture_output=True)
    except Exception as e:
        logging.error(f"Error: {e}")

    output_params = res.stdout.decode("utf-8").strip().split(",")
    outputs_file.write_text(res.stdout.decode("utf-8"))

    # NOTE there may be some better way of doing this, but it should be O(N)
    indexed_trace = [s.split("&") for s in trace_file.read_text().strip().split(";")[:-1]]

    effects_str = ""
    effects_arr = []
    for i, params in enumerate(indexed_trace):
        for output in output_params:  # is O(N) output params fixed
            if output in params:
                effects_str += f"{'X'*i} {output}\n"
                effects_arr.append(f"{'X'*i} {output}\n")

    effects_file.write_text(effects_str)

    return effects_arr


def pipeline(tlsf_file: str, config_file: str):
    tlsf_path = Path(tlsf_file)
    base = tlsf_path.stem

    # Create results/<specname> directory
    results_dir = Path("results") / base
    results_dir.mkdir(parents=True, exist_ok=True)

    hoa_file = results_dir / "01-system.hoa"
    hoax_file = results_dir / "02-hoax.cleaned.hoa"
    trace_file = results_dir / "03-trace.spot.txt"
    stats_file = results_dir / "04-autfilt.stats.txt"
    accepted_file = results_dir / "05-autfilt.accepted.hoa"
    effects_file = results_dir / "06-effects.txt"
    outputs_file = results_dir / "07-outputs.txt"
    causal_file = results_dir / "08-causal.hoa"
    log_file = results_dir / "acceptance.log"

    print(f"[+] Running ltlsynt on {tlsf_file}")
    run_ltlsynt(tlsf_path, hoa_file)

    aps = extract_aps(hoa_file)

    print(f"[+] Running hoax on {hoa_file}")
    run_hoax(hoa_file, hoax_file, Path(config_file), aps)

    print("[+] Generating trace")
    trace = generate_trace(hoax_file, aps)
    trace_file.write_text(trace)

    print("[+] Automaton stats:")
    run_autfilt_stats(hoa_file, stats_file)

    print("[+] Checking acceptance")
    accepted = run_autfilt_accept(hoa_file, trace, accepted_file)

    with open(log_file, "w") as f:
        f.write("Pass.\n" if accepted else "Did not pass.\n")

    print("[+] Checking causality")
    effects = extract_effects(tlsf_file, trace_file, effects_file, outputs_file)

    return {
        "aps": aps,
        "trace": trace,
        "accepted": accepted,
    }


# This should just run from main.py
if __name__ == "__main__":
    import sys

    if len(sys.argv) < 3:
        print("Usage: pipeline.py <spec.tlsf> <config.toml>")
        sys.exit(1)

    result = pipeline(sys.argv[1], sys.argv[2])
    print(result)
