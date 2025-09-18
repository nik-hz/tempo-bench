"""
Causality analysis module for generating causal HOA automata using corp.
"""

import os
import subprocess
import sys
from pathlib import Path
from typing import Union


def generate_causal_hoa(
    system_hoa: Union[str, os.PathLike],
    effect_file: Union[str, os.PathLike],
    trace_file: Union[str, os.PathLike],
    output_file: Union[str, os.PathLike]
) -> None:
    """
    Generate causal HOA from system HOA, effect specification, and trace using corp.py.

    Args:
        system_hoa: Path to system HOA file
        effect_file: Path to effect file with output APs
        trace_file: Path to trace file in Spot format
        output_file: Path to output causal HOA file

    Raises:
        RuntimeError: If corp.py execution fails
        FileNotFoundError: If input files don't exist
    """
    system_path = Path(system_hoa)
    effect_path = Path(effect_file)
    trace_path = Path(trace_file)
    output_path = Path(output_file)

    # Find corp.py in the same directory as this module
    corp_path = Path(__file__).parent / "corp.py"
    if not corp_path.exists():
        raise RuntimeError(f"corp.py not found at: {corp_path}")

    if not system_path.exists():
        raise FileNotFoundError(f"System HOA file not found: {system_path}")

    if not effect_path.exists():
        raise FileNotFoundError(f"Effect file not found: {effect_path}")

    if not trace_path.exists():
        raise FileNotFoundError(f"Trace file not found: {trace_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        sys.executable,
        str(corp_path),
        "-s", str(system_path),
        "-e", str(effect_path),
        "-t", str(trace_path),
        "-o", str(output_path)
    ]

    try:
        subprocess.run(
            cmd,
            check=True,
            text=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"corp.py failed (exit {e.returncode}): {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error running corp.py: {e}") from e