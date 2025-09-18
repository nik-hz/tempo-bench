"""
Utility functions for the Automata Reasoning pipeline.
"""

import os
import re
from pathlib import Path
from typing import List, Union


def extract_aps_from_hoa(hoa_file: Union[str, os.PathLike]) -> List[str]:
    """
    Extract atomic propositions from a HOA file.

    Args:
        hoa_file: Path to HOA file

    Returns:
        List of atomic proposition names

    Raises:
        FileNotFoundError: If HOA file doesn't exist
        RuntimeError: If no APs found in file
    """
    hoa_path = Path(hoa_file)

    if not hoa_path.exists():
        raise FileNotFoundError(f"HOA file not found: {hoa_path}")

    aps = []
    with hoa_path.open("r", encoding="utf-8") as f:
        for line in f:
            if line.strip().startswith("AP:"):
                aps = re.findall(r'"([^"]+)"', line)
                break

    if not aps:
        raise RuntimeError(f"No atomic propositions found in {hoa_path}")

    return aps


def create_effect_file(
    aps: List[str],
    output_aps: List[str],
    output_file: Union[str, os.PathLike]
) -> None:
    """
    Create an effect file for corp based on output APs.

    Args:
        aps: List of all atomic propositions from HOA file
        output_aps: List of output AP names (or indices)
        output_file: Path to save effect file

    Raises:
        ValueError: If output APs are invalid
    """
    output_path = Path(output_file)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Build the effect specification
    effects = []
    for ap in output_aps:
        # Check if it's an index or a name
        if ap.isdigit():
            idx = int(ap)
            if idx < len(aps):
                effects.append(f"<>({aps[idx]})")
            else:
                raise ValueError(f"AP index {idx} out of range (0-{len(aps)-1})")
        else:
            if ap in aps:
                effects.append(f"<>({ap})")
            else:
                raise ValueError(f"AP '{ap}' not found in HOA file")

    if not effects:
        raise ValueError("No valid output APs specified")

    effect_spec = " & ".join(effects)

    with output_path.open("w", encoding="utf-8") as f:
        f.write(effect_spec + "\n")


def validate_tools() -> List[str]:
    """
    Check which required tools are available on the system.

    Returns:
        List of missing tools
    """
    import shutil

    required_tools = ["ltlsynt", "hoax", "autfilt", "corp"]
    missing_tools = []

    for tool in required_tools:
        if shutil.which(tool) is None:
            missing_tools.append(tool)

    return missing_tools


def get_tool_versions() -> dict:
    """
    Get version information for available tools.

    Returns:
        Dictionary mapping tool names to version strings
    """
    import subprocess
    import shutil

    tools = {
        "ltlsynt": ["--version"],
        "hoax": ["--version"],
        "autfilt": ["--version"],
        "corp": ["--version"]
    }

    versions = {}
    for tool, version_args in tools.items():
        if shutil.which(tool):
            try:
                result = subprocess.run(
                    [tool] + version_args,
                    capture_output=True,
                    text=True,
                    timeout=5
                )
                if result.returncode == 0:
                    versions[tool] = result.stdout.strip().split('\n')[0]
                else:
                    versions[tool] = "version unknown"
            except (subprocess.TimeoutExpired, subprocess.CalledProcessError, OSError):
                versions[tool] = "version unknown"
        else:
            versions[tool] = "not installed"

    return versions