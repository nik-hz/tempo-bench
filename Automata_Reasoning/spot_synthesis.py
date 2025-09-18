"""
Spot synthesis module for converting TLSF to HOA automata.
"""

import os
import shutil
import subprocess
from pathlib import Path
from typing import Union


def synthesize_tlsf_to_hoa(tlsf_file: Union[str, os.PathLike], hoa_file: Union[str, os.PathLike]) -> None:
    """
    Synthesize a TLSF file into a HOA automaton using ltlsynt.

    Args:
        tlsf_file: Path to input TLSF file
        hoa_file: Path to output HOA file

    Raises:
        RuntimeError: If ltlsynt is not found or synthesis fails
        FileNotFoundError: If input TLSF file doesn't exist
    """
    tlsf_path = Path(tlsf_file)
    hoa_path = Path(hoa_file)

    if shutil.which("ltlsynt") is None:
        raise RuntimeError("ltlsynt not found on PATH. Please install Spot.")

    if not tlsf_path.exists():
        raise FileNotFoundError(f"Input TLSF file not found: {tlsf_path}")

    hoa_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = ["ltlsynt", "--tlsf", str(tlsf_path)]

    try:
        with hoa_path.open("w", encoding="utf-8") as fout:
            subprocess.run(
                cmd,
                stdout=fout,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"ltlsynt failed (exit {e.returncode}): {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error during synthesis: {e}") from e