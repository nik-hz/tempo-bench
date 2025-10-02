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
from datetime import datetime
from multiprocessing import Manager, Pool, Process, cpu_count
from pathlib import Path

from tqdm import tqdm

from .pipeline import pipeline


def cleanup_tempfiles():
    tmpdir = tempfile.gettempdir()
    for tf in glob.glob(os.path.join(tmpdir, "tempo_bench_effect_*.hoa")):
        try:
            os.unlink(tf)
        except OSError as e:
            logging.warning(f"Could not delete temp file {tf}: {e}")


atexit.register(cleanup_tempfiles)


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


def process_tlsf(args):
    """Process a single TLSF file through the pipeline.

    Unpacks args tuple (num_run, tlsf_file, config_file, timeout, queue, quiet,
    cancel_event) and runs pipeline. Results or errors are sent to the queue for
    collection.
    """
    num_run, tlsf_file, config_file, timeout, queue, quiet, cancel_event = args
    try:
        # Suppress logs in parallel mode if quiet is True
        if quiet:
            set_logging_level(logging.WARNING)

        # Check if another proc cancelled already
        if cancel_event.is_set():
            queue.put({"file": tlsf_file, "result": None, "error": "Cancelled"})
            return
        result = pipeline(tlsf_file, config_file, num_run, timeout)
        if "error" in result:
            cancel_event.set()
            queue.put({"file": tlsf_file, "result": None, "error": result["error"]})

        queue.put({"file": tlsf_file, "result": result, "error": None})
    except Exception as e:
        cancel_event.set()  # tell siblings to stop
        queue.put({"file": tlsf_file, "result": None, "error": str(e)})


def writer_process(queue, output_file):
    """Write pipeline results to output file as JSONL.

    Continuously reads from queue until 'DONE' sentinel is received. Each result is
    written as a JSON line and flushed immediately.
    """

    with open(output_file, "w", encoding="utf-8") as f:
        while True:
            item = queue.get()
            if item == "DONE":  # sentinel to stop
                logging.info("writer exiting")
                break
            f.write(json.dumps(item) + "\n")
            f.flush()  # stream safely to disk


def run_parallel(
    tlsf_dir, config_file, output_dir, num_runs, n_jobs=None, timeout=300, quiet=True
):
    """Process all TLSF files in a directory in parallel.

    Uses multiprocessing to run pipeline on multiple files concurrently. Results are
    collected via queue and written to output_file as JSONL. n_jobs defaults to CPU
    count if not specified.
    """
    tlsf_files = [str(p) for p in Path(tlsf_dir).glob("*.tlsf")]
    if not tlsf_files:
        logging.error(f"No TLSF files found in {tlsf_dir}")
        return

    n_jobs = n_jobs or cpu_count()
    manager = Manager()
    queue = manager.Queue()

    cancel_map = {f: manager.Event() for f in tlsf_files}

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_file = os.path.join(output_dir, f"data_{ts}.jsonl")
    writer = Process(target=writer_process, args=(queue, output_file))
    writer.start()

    try:
        with Pool(n_jobs) as pool:
            for _ in tqdm(
                pool.imap_unordered(
                    process_tlsf,
                    [
                        (num_run, f, config_file, timeout, queue, quiet, cancel_map[f])
                        for f in tlsf_files
                        for num_run in range(num_runs)
                    ],
                ),
                total=len(tlsf_files) * num_runs,
                desc="Processing TLSF files",
            ):
                pass
    except KeyboardInterrupt:
        print("\n[!] Caught Ctrl-C, terminating workers...")
        pool.terminate()
        pool.join()
        queue.put("DONE")
        writer.terminate()
        writer.join()
        raise
    else:
        queue.put("DONE")
        writer.join()


if __name__ == "__main__":
    # cleans up tempfiles
    atexit.register(cleanup_tempfiles)

    if len(sys.argv) < 2 or sys.argv[1] == "-h" or sys.argv[1] not in ["-s", "-p"]:
        print(
            "Usage: make_trace [-h] [-s <tlsf_file>] [-p <tlsf_dir>\
 <output_dir> [num_runs per file] [n_jobs] [timeout]]"
        )
        print(
            " \
                -s: Process single TLSF file  | -p: Process directory in parallel"
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
