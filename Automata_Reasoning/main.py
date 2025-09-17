import os
import shutil
import argparse
import re
from pathlib import Path
import subprocess
from typing import Tuple, List


def run_spot(fp_in: str | os.PathLike, fp_out: str | os.PathLike) -> None:
    fp_in = Path(fp_in)
    fp_out = Path(fp_out)

    if shutil.which("ltlsynt") is None:
        raise RuntimeError(f"`ltlsynt` not found on PATH.")

    if not fp_in.exists():
        raise RuntimeError(f"Input TLSF file not found: {fp_in}")

    fp_out.parent.mkdir(parents=True, exists_ok=True)

    cmd = ["ltlsynt", "--tlsf", str(fp_in)]

    try:
        with fp_out.open("w", encoding="utf-8") as fout:
            subprocess.run(
                cmd,
                stdout=fout,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"ltlsynt err (exit {e.returncode}). {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error")


def run_hoaX(
    fp_in: str | os.PathLike, fp_out: str | os.PathLike, hoax_config: str | os.PathLike
) -> None:
    """runs hoax to generate a finite trace"""
    fp_in = Path(fp_in)
    fp_out = Path(fp_out)
    hoax_config = Path(hoax_config)

    if shutil.which("hoax") is None:
        raise RuntimeError(f"`hoax` not found on PATH.")

    if not fp_in.exists():
        raise RuntimeError(f"Input hoa file not found: {fp_in}")

    if not hoax_config.exists():
        raise RuntimeError(f"hoax configuration file not found: {hoax_config}")

    fp_out.parent.mkdir(parents=True, exists_ok=True)

    cmd = ["hoax", str(fp_in), "--config", str(hoax_config)]

    try:
        with fp_out.open("w", encoding="utf-8") as fout:
            subprocess.run(
                cmd,
                stdout=fout,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"hoax err (exit {e.returncode}). {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error")


def check_trace(
    fp_hoa: str | os.PathLike, fp_trace: str | os.PathLike, fp_out: str | os.PathLike = None
) -> Tuple[bool, str]:
    """
    Check if a trace is accepted by a HOA automaton.
    Always adds the ;cycle{1} to the end for spot to check infinite traces.

    Args:
        fp_hoa: Path to HOA automaton file
        fp_trace: Path to trace file (either hoax output or Spot word format)
        fp_out: Optional path to save the Spot-formatted trace

    Returns:
        Tuple of (accepted: bool, message: str)
    """
    fp_hoa = Path(fp_hoa)
    fp_trace = Path(fp_trace)
    if fp_out:
        fp_out = Path(fp_out)

    if shutil.which("autfilt") is None:
        raise RuntimeError("`autfilt` not found on PATH.")

    if not fp_hoa.exists():
        raise RuntimeError(f"HOA file not found: {fp_hoa}")

    if not fp_trace.exists():
        raise RuntimeError(f"Trace file not found: {fp_trace}")

    # Read the trace file content
    with fp_trace.open("r", encoding="utf-8") as f:
        trace_content = f.read().strip()

    # Check if the trace is already in Spot word format
    # Spot format uses ; and & separators with optional cycle notation
    if ";" in trace_content or "&" in trace_content or "cycle{" in trace_content:
        # It's already a Spot word, just ensure it has the cycle notation
        spot_word = trace_content
        if not re.search(r';cycle\{.*\}$', spot_word):
            # Add default infinite cycle if not present
            spot_word = spot_word.rstrip(";") + ";cycle{1}"
    else:
        # It's hoax output format, parse it
        # Extract atomic propositions from HOA file
        aps = []
        with fp_hoa.open("r", encoding="utf-8") as f:
            for line in f:
                if line.strip().startswith("AP:"):
                    # Extract quoted AP names
                    aps = re.findall(r'"([^"]+)"', line)
                    break

        if not aps:
            raise RuntimeError("No atomic propositions found in HOA file")

        # Parse trace from hoax output
        trace = []
        for line in trace_content.split('\n'):
            # Look for {...} in hoax output
            start = line.find("{")
            end = line.find("}")
            if start == -1 or end == -1:
                continue
            raw = line[start + 1 : end].strip()
            if raw:
                # Handle things like "'a', 'p2'" → [a, p2]
                present = {tok.strip().strip("'\"") for tok in raw.split(",")}
            else:
                present = set()

            # Build assignment for this step
            assignment = []
            for ap in aps:
                if ap in present:
                    assignment.append(ap)
                else:
                    assignment.append("!" + ap)
            trace.append("&".join(assignment))

        if not trace:
            raise RuntimeError("No trace steps found in input")

        # Build Spot word with infinite cycle
        spot_word = ";".join(trace) + ";cycle{1}"

    # Save to output file if specified
    if fp_out:
        fp_out.parent.mkdir(parents=True, exist_ok=True)
        with fp_out.open("w", encoding="utf-8") as f:
            f.write(spot_word + "\n")

    # Run autfilt to check trace acceptance
    cmd = ["autfilt", str(fp_hoa), f"--accept-word={spot_word}"]

    try:
        result = subprocess.run(
            cmd,
            check=False,
            text=True,
            capture_output=True,
        )
        if result.returncode == 0:
            return True, "Trace is ACCEPTED by automaton"
        else:
            return False, "Trace is REJECTED by automaton"
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"autfilt err (exit {e.returncode}). {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error running autfilt") from e


def run_corp(
    fp_system: str | os.PathLike,
    fp_effect: str | os.PathLike,
    fp_trace: str | os.PathLike,
    fp_out: str | os.PathLike
) -> None:
    """
    Run corp to generate causal HOA from system HOA, effect specification, and trace.

    Args:
        fp_system: Path to system HOA file
        fp_effect: Path to effect file with output APs
        fp_trace: Path to trace file in Spot format
        fp_out: Path to output causal HOA file
    """
    fp_system = Path(fp_system)
    fp_effect = Path(fp_effect)
    fp_trace = Path(fp_trace)
    fp_out = Path(fp_out)

    if shutil.which("corp") is None:
        raise RuntimeError("`corp` not found on PATH. Please install corp from https://github.com/reactive-systems/corp")

    if not fp_system.exists():
        raise RuntimeError(f"System HOA file not found: {fp_system}")

    if not fp_effect.exists():
        raise RuntimeError(f"Effect file not found: {fp_effect}")

    if not fp_trace.exists():
        raise RuntimeError(f"Trace file not found: {fp_trace}")

    fp_out.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        "corp",
        "-s", str(fp_system),
        "-e", str(fp_effect),
        "-t", str(fp_trace),
        "-o", str(fp_out)
    ]

    try:
        subprocess.run(
            cmd,
            check=True,
            text=True,
            capture_output=True,
        )
        print(f"✅ Causal HOA written to {fp_out}")
    except subprocess.CalledProcessError as e:
        err = (e.stderr or "").strip()
        raise RuntimeError(f"corp err (exit {e.returncode}). {err}") from e
    except OSError as e:
        raise RuntimeError(f"I/O error running corp") from e


def extract_aps_from_hoa(fp_hoa: str | os.PathLike) -> List[str]:
    """Extract atomic propositions from a HOA file."""
    fp_hoa = Path(fp_hoa)
    aps = []
    with fp_hoa.open("r", encoding="utf-8") as f:
        for line in f:
            if line.strip().startswith("AP:"):
                aps = re.findall(r'"([^"]+)"', line)
                break
    return aps


def create_effect_file(aps: List[str], output_aps: List[str], fp_out: str | os.PathLike) -> None:
    """
    Create an effect file for corp based on output APs.

    Args:
        aps: List of all atomic propositions
        output_aps: List of output AP names (or indices)
        fp_out: Path to save effect file
    """
    fp_out = Path(fp_out)
    fp_out.parent.mkdir(parents=True, exist_ok=True)

    # Build the effect specification
    effects = []
    for ap in output_aps:
        # Check if it's an index or a name
        if ap.isdigit():
            idx = int(ap)
            if idx < len(aps):
                effects.append(f"<>({aps[idx]})")
        else:
            if ap in aps:
                effects.append(f"<>({ap})")

    effect_spec = " & ".join(effects)

    with fp_out.open("w", encoding="utf-8") as f:
        f.write(effect_spec + "\n")

    print(f"📝 Effect file created: {fp_out}")


def main():
    parser = argparse.ArgumentParser(
        description="Automata Reasoning Pipeline: TLSF → Automata → Trace → Causality → Reasoning",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Full pipeline from TLSF file
  %(prog)s --tlsf input.tlsf --output-dir results/ --hoax-config config.toml --outputs "g_0,g_1,g_2"

  # Generate trace from existing HOA
  %(prog)s --hoa system.hoa --hoax-config config.toml --output-dir results/

  # Check existing trace
  %(prog)s --hoa system.hoa --trace trace.txt --check-only

  # Generate reasoning from existing files
  %(prog)s --causal-hoa causal.hoa --trace trace.txt --effect effect.txt --reasoning-only
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

    args = parser.parse_args()

    # Set up output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        # Determine starting point and files
        if args.reasoning_only:
            # Just generate reasoning from existing files
            if not all([args.causal_hoa, args.trace, args.effect]):
                parser.error("--reasoning-only requires --causal-hoa, --trace, and --effect")

            from Reasoning import run_reasoning_trace
            print("🔍 Generating reasoning trace...")
            run_reasoning_trace(
                args.causal_hoa,
                args.trace,
                args.effect,
                output_dir / "reasoning.txt"
            )
            print(f"✅ Reasoning trace written to {output_dir / 'reasoning.txt'}")
            return

        # Step 1: Get HOA file (synthesize from TLSF or use existing)
        if args.tlsf:
            hoa_file = output_dir / "system.hoa"
            print(f"🔧 Synthesizing TLSF to HOA...")
            run_spot(args.tlsf, hoa_file)
            print(f"✅ HOA synthesized: {hoa_file}")
        elif args.hoa:
            hoa_file = Path(args.hoa)
            if not hoa_file.exists():
                raise FileNotFoundError(f"HOA file not found: {hoa_file}")
        else:
            parser.error("Either --tlsf or --hoa must be provided")

        # Extract APs for later use
        aps = extract_aps_from_hoa(hoa_file)
        print(f"📋 Found APs: {', '.join(aps)}")

        # Step 2: Get or generate trace
        if args.trace:
            trace_file = Path(args.trace)
            hoax_output = None
        else:
            # Generate trace with hoax
            print(f"🎲 Generating random trace with hoax...")
            hoax_output = output_dir / "hoax_output.txt"
            run_hoaX(hoa_file, hoax_output, args.hoax_config)
            print(f"✅ Hoax trace generated: {hoax_output}")

            # Convert to Spot format
            trace_file = output_dir / "trace.txt"
            print(f"🔄 Converting to Spot format...")
            accepted, msg = check_trace(hoa_file, hoax_output, trace_file)
            print(f"📄 Spot trace written to {trace_file}")
            print(f"🔍 Trace validation: {msg}")

            if not accepted:
                print("⚠️  Warning: Generated trace is not accepted by automaton")

        # Step 3: Check trace if requested or if just checking
        if args.check_only:
            if not trace_file:
                parser.error("--check-only requires a trace (provide --trace or generate one)")

            print(f"🔍 Checking trace acceptance...")
            accepted, msg = check_trace(hoa_file, trace_file)
            print(f"Result: {msg}")
            return

        # Step 4: Generate causal HOA with corp (if not skipping)
        if not args.skip_reasoning:
            # Get or create effect file
            if args.effect:
                effect_file = Path(args.effect)
            else:
                if not args.outputs:
                    print("⚠️  No outputs specified. Enter comma-separated output APs (e.g., 'g_0,g_1' or '1,2'):")
                    outputs_input = input("Outputs: ").strip()
                    output_aps = [o.strip() for o in outputs_input.split(",") if o.strip()]
                else:
                    output_aps = [o.strip() for o in args.outputs.split(",")]

                effect_file = output_dir / "effect.txt"
                create_effect_file(aps, output_aps, effect_file)

            # Run corp to generate causal HOA
            causal_hoa = output_dir / "causal.hoa"
            print(f"🔨 Generating causal HOA with corp...")
            run_corp(hoa_file, effect_file, trace_file, causal_hoa)

            # Step 5: Generate reasoning trace
            from Reasoning import run_reasoning_trace
            print("🔍 Generating reasoning trace...")
            run_reasoning_trace(
                causal_hoa,
                trace_file,
                effect_file,
                output_dir / "reasoning.txt"
            )
            print(f"✅ Reasoning trace written to {output_dir / 'reasoning.txt'}")

        print("\n🎉 Pipeline completed successfully!")
        print(f"📁 All outputs saved to: {output_dir}")

    except Exception as e:
        print(f"\n❌ Error: {e}")
        return 1

    return 0


if __name__ == "__main__":
    exit(main())
