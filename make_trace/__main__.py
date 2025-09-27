# -*- coding: utf-8 -*-
"""Main entry point for the Automata Reasoning pipeline.

This module provides a command-line interface for the complete pipeline:
TLSF -> Automata -> Trace -> Causality -> Reasoning
"""
import json
import logging
import os
import sys
from multiprocessing import Pool, Process, Queue, cpu_count
from pathlib import Path

from tqdm import tqdm

from .pipeline import pipeline


def set_logging_level(level=logging.WARNING):
    """Set the logging level for the pipeline module.

    Args:
        level: logging level (logging.INFO, logging.WARNING, logging.ERROR)
    """
    # Get the pipeline logger
    pipeline_logger = logging.getLogger("make_trace.pipeline")
    pipeline_logger.setLevel(level)

    # Configure handler if not already configured
    if not pipeline_logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        pipeline_logger.addHandler(handler)


def process_tlsf(*args):
    """Helper process."""
    tlsf_file, config_file, queue, quiet = args
    try:
        # Suppress logs in parallel mode if quiet is True
        if quiet:
            set_logging_level(logging.WARNING)
        result = pipeline(tlsf_file, config_file)
        queue.put({"file": tlsf_file, "result": result, "error": None})
    except Exception as e:
        queue.put({"file": tlsf_file, "result": None, "error": str(e)})


def writer_process(queue, output_file):
    """Writer process
    args:
    """

    with open(output_file, "w", encoding="utf-8") as f:
        while True:
            item = queue.get()
            if item == "DONE":  # sentinel to stop
                logging.info("writer exiting")
                break
            f.write(json.dumps(item) + "\n")
            f.flush()  # stream safely to disk


def run_parallel(tlsf_dir, config_file, output_file, n_jobs=None, quiet=True):
    tlsf_files = [str(p) for p in Path(tlsf_dir).glob("*.tlsf")]
    if not tlsf_files:
        logging.error(f"No TLSF files found in {tlsf_dir}")
        return

    n_jobs = n_jobs or cpu_count()
    queue = Queue()

    writer = Process(target=writer_process, args=(queue, output_file))
    writer.start()

    with Pool(n_jobs) as pool:
        for _ in tqdm(
            pool.imap_unordered(
                process_tlsf, [(f, config_file, queue, quiet) for f in tlsf_files]
            ),
            total=len(tlsf_files),
            desc="Processing TLSF files",
        ):
            pass
    # tell writer to finish
    queue.put("DONE")
    writer.join()


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] == "-h" or sys.argv[1] not in ["-s", "-p"]:
        print(
            "\
            Usage: make_trace [-h] [-s <tlsf_file>] [-p <tlsf_dir>\
                <output_file> [n_jobs]]"
        )
        print(
            " \
                -s: Process single TLSF file  |\
                -p: Process directory in parallel"
        )
        print(
            """
              n_jobs: default is cpu_count if no argument is passed
              """
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
        n_jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 5

        # Set logging to WARNING for parallel processing to reduce noise
        set_logging_level(logging.WARNING)
        run_parallel(tlsf_dir, config_file, output_file, n_jobs, quiet=True)
