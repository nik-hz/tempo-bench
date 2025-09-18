# Automata Reasoning

A comprehensive pipeline for temporal logic synthesis, trace generation, causality analysis, and reasoning extraction.

## Pipeline Overview

```
TLSF → Automata → Trace → Causality → Reasoning
```

The pipeline consists of five main stages:

1. **TLSF to HOA**: Synthesize temporal logic specifications into automata
2. **Trace Generation**: Generate random finite traces from automata
3. **Trace Validation**: Check trace acceptance and convert formats
4. **Causality Analysis**: Generate causal automata using corp
5. **Reasoning Extraction**: Extract causal explanations from traces

## Modules

- **spot_synthesis**: TLSF to HOA synthesis using Spot's ltlsynt
- **hoax_trace**: Random trace generation using hoax
- **trace_checker**: Trace validation and format conversion
- **causality**: Causal HOA generation using corp
- **reasoning**: Reasoning trace extraction and analysis
- **utils**: Helper functions and utilities

## Usage

### Command Line Interface

```bash
# Full pipeline from TLSF file
python -m automata_reasoning --tlsf input.tlsf --output-dir results/ --hoax-config config.toml --outputs "g_0,g_1,g_2"

# Generate trace from existing HOA
python -m automata_reasoning --hoa system.hoa --hoax-config config.toml --output-dir results/

# Check existing trace
python -m automata_reasoning --hoa system.hoa --trace trace.txt --check-only

# Generate reasoning from existing files
python -m automata_reasoning --causal-hoa causal.hoa --trace trace.txt --effect effect.txt --reasoning-only

# Check tool availability
python -m automata_reasoning --check-tools
```

### Python API

```python
from automata_reasoning import (
    synthesize_tlsf_to_hoa,
    generate_hoax_trace,
    check_trace,
    generate_causal_hoa,
    generate_reasoning_trace
)

# Synthesize TLSF to HOA
synthesize_tlsf_to_hoa("input.tlsf", "system.hoa")

# Generate random trace
generate_hoax_trace("system.hoa", "trace_output.txt", "config.toml")

# Validate trace
accepted, message = check_trace("system.hoa", "trace.txt")

# Generate causal automaton
generate_causal_hoa("system.hoa", "effect.txt", "trace.txt", "causal.hoa")

# Extract reasoning
generate_reasoning_trace("causal.hoa", "trace.txt", "effect.txt", "reasoning.txt")
```

## Dependencies

Required external tools:
- **Spot**: For TLSF synthesis (`ltlsynt`)
- **hoax**: For random trace generation

Internal tools:
- **corp.py**: For causality analysis (included in package)

Check tool availability:
```bash
python -m automata_reasoning --check-tools
```

## Output Files

- `system.hoa`: Synthesized automaton
- `hoax_output.txt`: Raw hoax trace output
- `trace.txt`: Spot-format trace
- `effect.txt`: Effect specification for output APs
- `causal.hoa`: Causal automaton
- `reasoning.txt`: Extracted reasoning trace