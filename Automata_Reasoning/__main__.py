# -*- coding: utf-8 -*-
"""
Main entry point for the Automata Reasoning pipeline.

This module provides a command-line interface for the complete pipeline:
TLSF -> Automata -> Trace -> Causality -> Reasoning
"""

import argparse
import sys
from pathlib import Path

from . import (
    synthesize_tlsf_to_hoa,
    generate_hoax_trace,
    check_trace,
    generate_causal_hoa,
    generate_reasoning_trace,
    extract_aps_from_hoa,
    create_effect_file,
)
from .utils import validate_tools, get_tool_versions


def main():
    """Main entry point for the Automata Reasoning pipeline."""
    parser = argparse.ArgumentParser(
        description="Automata Reasoning Pipeline: TLSF -> Automata -> Trace -> Causality -> Reasoning",
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
        """
    )

    # Input options
    parser.add_argument("--tlsf", type=str, help="Path to TLSF file to synthesize")
    parser.add_argument("--hoa", type=str, help="Path to existing HOA file")
    parser.add_argument("--causal-hoa", type=str, help="Path to existing causal HOA file")

    # Configuration
    parser.add_argument("--hoax-config", type=str, default="random_generator.toml",
                        help="Path to hoax configuration file (default: random_generator.toml)")
    parser.add_argument("--outputs", type=str,
                        help="Comma-separated list of output APs (e.g., 'g_0,g_1,g_2' or '0,1,2')")

    # Trace options
    parser.add_argument("--trace", type=str, help="Path to existing trace file")
    parser.add_argument("--effect", type=str, help="Path to existing effect file")

    # Output options
    parser.add_argument("--output-dir", type=str, default="output",
                        help="Directory for output files (default: output)")

    # Control flow options
    parser.add_argument("--check-only", action="store_true",
                        help="Only check trace validity, don't generate causality")
    parser.add_argument("--reasoning-only", action="store_true",
                        help="Only generate reasoning from existing causal HOA and trace")
    parser.add_argument("--skip-reasoning", action="store_true",
                        help="Skip reasoning generation step")

    # Utility options
    parser.add_argument("--check-tools", action="store_true",
                        help="Check availability of required tools")
    parser.add_argument("--tool-versions", action="store_true",
                        help="Show versions of available tools")

    args = parser.parse_args()

    # Handle utility commands
    if args.check_tools:
        missing = validate_tools()
        if missing:
            print("Missing required tools:")
            for tool in missing:
                print(f"  - {tool}")
            print("\nPlease install the missing tools and ensure they are in your PATH.")
            return 1
        else:
            print("All required tools are available")
            return 0

    if args.tool_versions:
        versions = get_tool_versions()
        print("Tool versions:")
        for tool, version in versions.items():
            print(f"  {tool}: {version}")
        return 0

    # Set up output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        # Determine starting point and files
        if args.reasoning_only:
            # Just generate reasoning from existing files
            if not all([args.causal_hoa, args.trace, args.effect]):
                parser.error("--reasoning-only requires --causal-hoa, --trace, and --effect")

            print("Generating reasoning trace...")
            generate_reasoning_trace(
                args.causal_hoa,
                args.trace,
                args.effect,
                output_dir / "reasoning.txt"
            )
            print(f"Reasoning trace written to {output_dir / 'reasoning.txt'}")
            return 0

        # Step 1: Get HOA file (synthesize from TLSF or use existing)
        if args.tlsf:
            hoa_file = output_dir / "system.hoa"
            print(f"Synthesizing TLSF to HOA...")
            synthesize_tlsf_to_hoa(args.tlsf, hoa_file)
            print(f"HOA synthesized: {hoa_file}")
        elif args.hoa:
            hoa_file = Path(args.hoa)
            if not hoa_file.exists():
                raise FileNotFoundError(f"HOA file not found: {hoa_file}")
        else:
            parser.error("Either --tlsf or --hoa must be provided")

        # Extract APs for later use
        aps = extract_aps_from_hoa(hoa_file)
        print(f"Found APs: {', '.join(aps)}")

        # Step 2: Get or generate trace
        if args.trace:
            trace_file = Path(args.trace)
            hoax_output = None
        else:
            # Generate trace with hoax
            print(f"Generating random trace with hoax...")
            hoax_output = output_dir / "hoax_output.txt"
            generate_hoax_trace(hoa_file, hoax_output, args.hoax_config)
            print(f"Hoax trace generated: {hoax_output}")

            # Convert to Spot format
            trace_file = output_dir / "trace.txt"
            print(f"Converting to Spot format...")
            accepted, msg = check_trace(hoa_file, hoax_output, trace_file)
            print(f"Spot trace written to {trace_file}")
            print(f"Trace validation: {msg}")

            if not accepted:
                print("Warning: Generated trace is not accepted by automaton")

        # Step 3: Check trace if requested or if just checking
        if args.check_only:
            if not args.trace and not hoax_output:
                parser.error("--check-only requires a trace (provide --trace or generate one)")

            print(f"Checking trace acceptance...")
            accepted, msg = check_trace(hoa_file, trace_file)
            print(f"Result: {msg}")
            return 0

        # Step 4: Generate causal HOA with corp (if not skipping)
        if not args.skip_reasoning:
            # Get or create effect file
            if args.effect:
                effect_file = Path(args.effect)
            else:
                if not args.outputs:
                    print("No outputs specified. Enter comma-separated output APs (e.g., 'g_0,g_1' or '1,2'):")
                    outputs_input = input("Outputs: ").strip()
                    output_aps = [o.strip() for o in outputs_input.split(",") if o.strip()]
                else:
                    output_aps = [o.strip() for o in args.outputs.split(",")]

                effect_file = output_dir / "effect.txt"
                create_effect_file(aps, output_aps, effect_file)
                print(f"Effect file created: {effect_file}")

            # Run corp to generate causal HOA
            causal_hoa = output_dir / "causal.hoa"
            print(f"Generating causal HOA with corp...")
            generate_causal_hoa(hoa_file, effect_file, trace_file, causal_hoa)
            print(f"Causal HOA written to {causal_hoa}")

            # Step 5: Generate reasoning trace
            print("Generating reasoning trace...")
            generate_reasoning_trace(
                causal_hoa,
                trace_file,
                effect_file,
                output_dir / "reasoning.txt"
            )
            print(f"Reasoning trace written to {output_dir / 'reasoning.txt'}")

        print("\nPipeline completed successfully!")
        print(f"All outputs saved to: {output_dir}")

    except Exception as e:
        print(f"\nError: {e}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())