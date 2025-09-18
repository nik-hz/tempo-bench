"""
Hoax trace generation module for creating random finite traces from HOA automata.
"""

import os
import shutil
import subprocess
from pathlib import Path
from typing import Union


def generate_hoax_trace(
    hoa_file: Union[str, os.PathLike],
    output_file: Union[str, os.PathLike],
    config_file: Union[str, os.PathLike]
) -> None:
    """
    Generate a finite trace from a HOA automaton using hoax.

    Args:
        hoa_file: Path to input HOA file
        output_file: Path to save hoax trace output
        config_file: Path to hoax configuration file

    Raises:
        RuntimeError: If hoax is not found or generation fails
        FileNotFoundError: If input files don't exist
    """
    hoa_path = Path(hoa_file)
    output_path = Path(output_file)
    config_path = Path(config_file)

    if shutil.which("hoax") is None:
        raise RuntimeError("hoax not found on PATH. Please install hoax-hoa-executor.")

    if not hoa_path.exists():
        raise FileNotFoundError(f"Input HOA file not found: {hoa_path}")

    if not config_path.exists():
        raise FileNotFoundError(f"Configuration file not found: {config_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = ["hoax", str(hoa_path), "--config", str(config_path)]

    try:
        with output_path.open("w", encoding="utf-8") as fout:
            subprocess.run(
                cmd,
                stdout=fout,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"hoax failed (exit {e.returncode}): {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error during trace generation: {e}") from e