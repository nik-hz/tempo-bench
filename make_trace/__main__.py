# -*- coding: utf-8 -*-
"""Main entry point for the Automata Reasoning pipeline.

This module provides a command-line interface for the complete pipeline:
TLSF -> Automata -> Trace -> Causality -> Reasoning
"""
import atexit
import glob
import json
import logging
import os
import sys
import tempfile
from pathlib import Path

from .convert_dataset import split_effects
from .pipeline import pipeline
from .run_parallel import run_parallel, set_logging_level


def cleanup_tempfiles():
    tmpdir = tempfile.gettempdir()
    for tf in glob.glob(os.path.join(tmpdir, "tempo_bench_effect_*.hoa")):
        try:
            os.unlink(tf)
        except OSError as e:
            logging.warning(f"Could not delete temp file {tf}: {e}")


atexit.register(cleanup_tempfiles)


if __name__ == "__main__":
    # cleans up tempfiles
    atexit.register(cleanup_tempfiles)

    if (
        len(sys.argv) < 2
        or sys.argv[1] == "-h"
        or sys.argv[1] not in ["-s", "-p", "-c"]
    ):
        print(
            "Usage: make_trace [-h] help, [-s] single thread, [-p] parallel, "
            "[-c] convert data\n\n"
            # per command help
            "[-s <tlsf_file>]\n"
            "[-p <tlsf_dir> <output_dir> [num_runs per file] [n_jobs] [timeout]]\n"
            "[-c <jsonl_file file> <output_file>]\n"
        )
        sys.exit(1)

    config_file = os.path.join(os.path.dirname(__file__), "random_config.toml")

    if sys.argv[1] == "-s":
        # Set logging to INFO for single file processing
        set_logging_level(logging.INFO)
        result = pipeline(sys.argv[2], config_file)
        print(json.dumps(result, indent=2))
        exit(0)

    elif sys.argv[1] == "-p":
        tlsf_dir = sys.argv[2]
        output_file = sys.argv[3]
        num_runs = int(sys.argv[4]) if len(sys.argv) > 4 else None
        n_jobs = int(sys.argv[5]) if len(sys.argv) > 5 else 5
        timeout = int(sys.argv[6]) if len(sys.argv) > 6 else 300

        # Set logging to WARNING for parallel processing to reduce noise
        set_logging_level(logging.WARNING)
        run_parallel(
            tlsf_dir,
            config_file,
            output_file,
            num_runs,
            n_jobs,
            timeout=timeout,
            quiet=True,
        )

    elif sys.argv[1] == "-c":
        input_file = Path(sys.argv[2])
        output_file = Path(sys.argv[3])
        split_effects(input_file, output_file)
