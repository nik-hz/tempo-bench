# make_trace: Temporal Logic Pipeline Module

A comprehensive Python module for temporal logic synthesis, trace generation, and causality analysis. This module orchestrates the complete pipeline from TLSF specifications to causal reasoning extraction.

## Overview

The `make_trace` module implements a five-stage pipeline that transforms temporal logic specifications into causal explanations:

```
TLSF → HOA Automata → Random Traces → Causality Analysis → Reasoning Extraction
```

## Module Architecture

### Core Components

#### `pipeline.py`
The main orchestrator that manages the entire workflow:
- Coordinates subprocess execution for external tools
- Handles file I/O and intermediate data management
- Provides error handling and logging
- Manages the sequential execution of pipeline stages

#### `corp.py`
Implementation of the CORP (Causes for Omega-Regular Properties) algorithm:
- Synthesizes causal automata from system traces
- Handles command-line argument parsing
- Supports hypothesis checking for cause validation
- Outputs results in HOA format

#### `cause.py`
Core causality computation logic:
- Implements the automata-theoretic cause synthesis algorithm
- Constructs non-deterministic Büchi automata for causes
- Handles omega-regular property analysis
- Integrates with Spot library for automata operations

#### `auto.py`
Automata manipulation utilities:
- Provides helper functions for automata transformations
- Handles power set constructions
- Manages BDD (Binary Decision Diagram) operations via buddy
- Implements combinatorial operations on automata states

#### `parse.py`
Parsing utilities for various automata formats:
- Converts between different automata representations
- Handles Spot format parsing
- Manages trace format conversions
- Validates input format correctness

## Dependencies

### Required Python Libraries

- **spot**: Omega-automata manipulation and LTL model checking
- **buddy**: BDD library for efficient boolean operations
- **subprocess**: For executing external tools
- **pathlib**: Modern path handling
- **logging**: Comprehensive error and debug logging
- **re**: Regular expression operations for parsing

### External Tools Required

1. **ltlsynt** (from Spot):
   - Synthesizes realizability for LTL specifications
   - Converts TLSF to HOA format

2. **hoax** (HOA executor):
   - Generates random traces from automata
   - Simulates automaton execution

3. **syfco** (optional):
   - TLSF format manipulation
   - Specification preprocessing

## Installation

### As Part of the Main Project

The module is included in the main tempo-rl installation:

```bash
pip install -r requirements.txt
```

### Module-Specific Usage

```python
from make_trace import pipeline
from make_trace.corp import main as corp_main
from make_trace.cause import synthesize_cause
```

## Usage

### Command Line Interface

Run the complete pipeline:

```bash
python -m make_trace --input path/to/spec.tlsf --output results/
```

Check tool availability:

```bash
python -m make_trace --check-tools
```

### Pipeline Execution

The pipeline can be executed programmatically:

```python
from make_trace.pipeline import pipeline

# Run the complete pipeline
success = pipeline(
    tlsf_file="specs/example.tlsf",
    output_dir="results/",
    verbose=True
)
```

### Individual Components

#### Using CORP directly:

```python
import sys
from make_trace.corp import main

# Set up arguments
sys.argv = [
    'corp.py',
    '-s', 'system.hoa',
    '-e', 'effect.txt',
    '-t', 'trace.txt',
    '-o', 'result.hoa'
]

# Run CORP
main()
```

#### Using cause synthesis:

```python
from make_trace.cause import synthesize_cause
import spot

# Load system and trace
system = spot.automaton("system.hoa")
trace = spot.parse_word("a&b;cycle{a&b}")

# Synthesize cause
cause_automaton = synthesize_cause(system, trace, effect)
```

## Pipeline Stages Detailed

### Stage 1: TLSF to HOA Conversion

Converts temporal logic specifications to automata:

```bash
ltlsynt --ins=1 --outs=1 -f example.tlsf -H > system.hoa
```

- Input: TLSF specification file
- Output: HOA automaton representing the synthesized system
- Tool: ltlsynt from Spot

### Stage 2: Random Trace Generation

Generates execution traces from the automaton:

```bash
hoax -n 10 system.hoa > hoax_output.txt
```

- Input: HOA automaton
- Output: Random trace in hoax format
- Parameters: `-n` specifies trace length

### Stage 3: Trace Format Conversion

Converts hoax output to Spot word format:

- Parses hoax output for state sequences
- Extracts atomic propositions at each step
- Formats as Spot word with cycle detection
- Output: `trace.txt` in format like `a&b;cycle{a&b;a&b}`

### Stage 4: Causality Analysis

Synthesizes causal automaton using CORP:

```bash
python corp.py -s system.hoa -e effect.txt -t trace.txt -o causal.hoa
```

- Analyzes trace for causal relationships
- Constructs Büchi automaton characterizing causes
- Outputs causal automaton in HOA format

### Stage 5: Reasoning Extraction

Extracts human-readable causal explanations:

- Parses causal automaton structure
- Identifies key causal patterns
- Generates textual reasoning trace
- Output: `reasoning.txt` with causal explanations

## File Formats

### Input: TLSF Format

Temporal Logic Synthesis Format specification:

```tlsf
INFO {
  TITLE:       "Example"
  DESCRIPTION: "Simple reactive system"
  SEMANTICS:   Mealy
  TARGET:      Mealy
}
MAIN {
  INPUTS { request }
  OUTPUTS { grant }

  GUARANTEES {
    G (request -> F grant)
  }
}
```

### Intermediate: HOA Format

Hanoi Omega Automaton format:

```hoa
HOA: v1
States: 2
Start: 0
AP: 2 "request" "grant"
acc-name: Buchi
Acceptance: 1 Inf(0)
controllable-AP: 1
--BODY--
State: 0
[0&!1] 1
[!0] 0
State: 1 {0}
[1] 0
[!1] 1
--END--
```

### Output: Trace Format

Spot word format for traces:

```
!request&!grant;request&!grant;cycle{request&grant;!request&!grant}
```

### Effect Specification

LTL formula or HOA automaton:

```ltl
F(grant)
```

## Error Handling

The module includes comprehensive error handling:

- **Tool availability checking**: Verifies external tools are installed
- **Format validation**: Ensures input files are correctly formatted
- **Subprocess management**: Handles tool execution failures
- **Logging**: Detailed debug output with `--verbose` flag

## Logging and Debugging

Enable detailed logging:

```python
import logging
from make_trace import pipeline

logging.basicConfig(level=logging.DEBUG)
pipeline.pipeline(tlsf_file, output_dir, verbose=True)
```

Or via environment variable:

```bash
export DEBUG=1
python -m make_trace --input spec.tlsf --output results/
```

## Performance Considerations

- **Trace length**: Longer traces increase causality analysis time
- **Automaton size**: Large state spaces may require more memory
- **BDD operations**: Complex boolean operations can be computationally intensive
- **Parallel execution**: Currently runs stages sequentially

## Extending the Pipeline

### Adding New Stages

Create a new stage function in `pipeline.py`:

```python
def new_stage(input_file, output_file, **kwargs):
    """Implement new processing stage"""
    # Process input
    # Generate output
    return success_boolean
```

### Custom Effect Specifications

Implement custom effect generators:

```python
from make_trace.parse import create_effect

def custom_effect(trace, aps):
    """Generate custom effect based on trace analysis"""
    return effect_formula
```

## Troubleshooting

### Common Issues

1. **Tool not found**: Install missing external tools (ltlsynt, hoax)
2. **Import errors**: Ensure Spot Python bindings are installed
3. **Memory issues**: Reduce trace length or automaton complexity
4. **Format errors**: Validate input files against specifications

### Debug Mode

Run with maximum verbosity:

```bash
python -m make_trace --input spec.tlsf --output results/ --verbose --debug
```

## Integration with Main Project

The module integrates seamlessly with the tempo-rl framework:

- Uses shared Spot installation
- Follows project conventions for file handling
- Outputs compatible with visualization tools
- Supports Docker environment execution

## Notes

- **Effect Selection**: The `effect.txt` file defines the effect we want to monitor and analyze for causality
- **TODO**: Implement brute force effect finder for automatic effect discovery

## References

- [CORP Repository](https://github.com/reactive-systems/corp): Original CORP implementation
- [Spot Documentation](https://spot.lre.epita.fr/): Automata library reference
- [TLSF Specification](https://arxiv.org/abs/1604.02284): Format specification
- [HOA Format](http://adl.github.io/hoaf/): Automaton format documentation

## Contributing

When extending this module:

1. Follow existing code style and conventions
2. Add appropriate error handling
3. Update pipeline stages documentation
4. Ensure compatibility with Docker environment
5. Add logging for debugging support

## License

Part of the tempo-rl project. See main LICENSE file for details.