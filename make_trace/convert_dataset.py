#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def split_effects(input_path: Path, output_path: Path):
    """Read a JSONL file where each line may contain multiple effects, and write a new
    JSONL file with one effect per line."""
    with input_path.open("r") as infile, output_path.open("w") as outfile:
        for line in infile:
            if not line.strip():
                continue
            entry = json.loads(line)

            # Skip entries that don't have the expected structure
            result = entry.get("result")
            if not result or "effects" not in result or not result["effects"]:
                # Skip error / null entries
                continue

            effects = entry["result"]["effects"]
            causality = entry["result"].get("causality", {})

            for effect in effects:
                new_entry = entry.copy()
                new_entry["result"] = entry["result"].copy()
                # overwrite effects to contain only this one
                new_entry["result"]["effects"] = [effect]
                # filter causality for this effect only
                new_entry["result"]["causality"] = {effect: causality.get(effect, {})}
                outfile.write(json.dumps(new_entry) + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Split multi-effect JSONL entries into one-effect-per-line JSONL"
    )
    parser.add_argument("input", type=Path, help="Input JSONL file")
    parser.add_argument("output", type=Path, help="Output JSONL file")
    args = parser.parse_args()

    split_effects(args.input, args.output)


if __name__ == "__main__":
    main()
