"""
Entry point for the dataset builder.

This module provides the cli for the dataset builder:
Automata Reasoning -> LLM Noise -> Final Dataset
"""

import argparse
import sys
from pathlib import Path


def main():
    """Dataset Builder"""
    parser = argparse.ArgumentParser(
        description="Dataset Builder",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Full pipeline from TLSF file
  python -m Automata_Reasoning --tlsf input.tlsf --output-dir results/ --hoax-config config.toml --outputs "g_0,g_1,g_2"

  # Generate trace from existing HOA
  python -m Automata_Reasoning --hoa system.hoa --hoax-config config.toml --output-dir results/

  # Check existing trace
  python -m Automata_Reasoning --hoa system.hoa --trace trace.txt --check-only

  # Generate reasoning from existing files
  python -m Automata_Reasoning --causal-hoa causal.hoa --trace trace.txt --effect effect.txt --reasoning-only

  # Check tool availability
  python -m Automata_Reasoning --check-tools
        """,
    )
