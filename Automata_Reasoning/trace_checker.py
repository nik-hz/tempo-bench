"""
Trace checking and validation module for converting between formats and validating traces.
"""

import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import List, Tuple, Union


def convert_hoax_to_spot(
    hoax_file: Union[str, os.PathLike],
    aps: List[str],
    spot_file: Union[str, os.PathLike] = None
) -> str:
    """
    Convert hoax trace output to Spot word format.

    Args:
        hoax_file: Path to hoax output file
        aps: List of atomic propositions from HOA file
        spot_file: Optional path to save Spot word (if None, returns string)

    Returns:
        Spot word string with cycle notation

    Raises:
        FileNotFoundError: If hoax file doesn't exist
        RuntimeError: If no trace steps found
    """
    hoax_path = Path(hoax_file)

    if not hoax_path.exists():
        raise FileNotFoundError(f"Hoax file not found: {hoax_path}")

    trace = []
    with hoax_path.open("r", encoding="utf-8") as f:
        for line in f:
            # Look for {...} in hoax output
            start = line.find("{")
            end = line.find("}")
            if start == -1 or end == -1:
                continue
            raw = line[start + 1 : end].strip()
            if raw:
                # Handle things like "'a', 'p2'" → [a, p2]
                present = {tok.strip().strip("'\"") for tok in raw.split(",")}
            else:
                present = set()

            # Build assignment for this step
            assignment = []
            for ap in aps:
                if ap in present:
                    assignment.append(ap)
                else:
                    assignment.append("!" + ap)
            trace.append("&".join(assignment))

    if not trace:
        raise RuntimeError("No trace steps found in hoax output")

    # Build Spot word with infinite cycle
    spot_word = ";".join(trace) + ";cycle{1}"

    # Save to file if specified
    if spot_file:
        spot_path = Path(spot_file)
        spot_path.parent.mkdir(parents=True, exist_ok=True)
        with spot_path.open("w", encoding="utf-8") as f:
            f.write(spot_word + "\n")

    return spot_word


def check_trace(
    hoa_file: Union[str, os.PathLike],
    trace_file: Union[str, os.PathLike],
    output_file: Union[str, os.PathLike] = None
) -> Tuple[bool, str]:
    """
    Check if a trace is accepted by a HOA automaton.
    Always adds the ;cycle{1} to the end for spot to check infinite traces.

    Args:
        hoa_file: Path to HOA automaton file
        trace_file: Path to trace file (either hoax output or Spot word format)
        output_file: Optional path to save the Spot-formatted trace

    Returns:
        Tuple of (accepted: bool, message: str)

    Raises:
        RuntimeError: If autfilt is not found or checking fails
        FileNotFoundError: If input files don't exist
    """
    hoa_path = Path(hoa_file)
    trace_path = Path(trace_file)

    if shutil.which("autfilt") is None:
        raise RuntimeError("autfilt not found on PATH. Please install Spot.")

    if not hoa_path.exists():
        raise FileNotFoundError(f"HOA file not found: {hoa_path}")

    if not trace_path.exists():
        raise FileNotFoundError(f"Trace file not found: {trace_path}")

    # Read the trace file content
    with trace_path.open("r", encoding="utf-8") as f:
        trace_content = f.read().strip()

    # Check if the trace is already in Spot word format
    # Spot format uses ; and & separators with optional cycle notation
    if ";" in trace_content or "&" in trace_content or "cycle{" in trace_content:
        # It's already a Spot word, just ensure it has the cycle notation
        spot_word = trace_content
        if not re.search(r';cycle\{.*\}$', spot_word):
            # Add default infinite cycle if not present
            spot_word = spot_word.rstrip(";") + ";cycle{1}"
    else:
        # It's hoax output format, parse it
        from .utils import extract_aps_from_hoa
        aps = extract_aps_from_hoa(hoa_path)
        spot_word = convert_hoax_to_spot(trace_path, aps)

    # Save to output file if specified
    if output_file:
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8") as f:
            f.write(spot_word + "\n")

    # Run autfilt to check trace acceptance
    cmd = ["autfilt", str(hoa_path), f"--accept-word={spot_word}"]

    try:
        result = subprocess.run(
            cmd,
            check=False,
            text=True,
            capture_output=True,
        )
        if result.returncode == 0:
            return True, "Trace is ACCEPTED by automaton"
        else:
            return False, "Trace is REJECTED by automaton"
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"autfilt failed (exit {e.returncode}): {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error running autfilt: {e}") from e