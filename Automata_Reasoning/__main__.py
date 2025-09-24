# -*- coding: utf-8 -*-
"""
Main entry point for the Automata Reasoning pipeline.

This module provides a command-line interface for the complete pipeline:
TLSF -> Automata -> Trace -> Causality -> Reasoning
"""

import sys

from .pipeline import pipeline

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: pipeline.py <spec.tlsf>")
        sys.exit(1)

    result = pipeline(sys.argv[1], "pipeline_configs.toml")
    print(result)