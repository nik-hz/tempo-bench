# -*- coding: utf-8 -*-
"""Main entry point for the Automata Reasoning pipeline.

This module provides a command-line interface for the complete pipeline:
TLSF -> Automata -> Trace -> Causality -> Reasoning
"""

import sys

from .pipeline import pipeline

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: pipeline.py <spec.tlsf> <config.toml>")
        sys.exit(1)

    result = pipeline(sys.argv[1], sys.argv[2])
    print(result)
