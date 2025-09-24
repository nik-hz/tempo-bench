# Tempo-RL: Temporal Logic Synthesis and Causal Reasoning Framework

Tempo-RL is a comprehensive framework for synthesizing causes for omega-regular properties on reactive system traces. It combines temporal logic synthesis, trace generation, and causality analysis to provide insights into system behavior.

## Overview

This project implements CORP (Causes for Omega-Regular Properties), a prototype tool that synthesizes causes for effects that are omega-regular properties on traces of reactive systems. The framework includes an automata-based algorithm that constructs non-deterministic Büchi automata to characterize causes for given omega-regular effects.

## Features

- **Temporal Logic Synthesis**: Convert TLSF (Temporal Logic Synthesis Format) specifications into HOA (Hanoi Omega Automata) format
- **Trace Generation**: Generate random finite traces from automata using the hoax executor
- **Causality Analysis**: Synthesize causes for omega-regular properties using the CORP algorithm
- **Reasoning Extraction**: Extract causal explanations from system traces
- **Visualization**: Interactive Python notebooks for visualizing automata and intermediate results

## Project Structure

```
tempo-rl/
├── make_trace/          # Main pipeline implementation
│   ├── pipeline.py      # Orchestrates the entire synthesis pipeline
│   ├── corp.py          # CORP cause synthesis algorithm
│   ├── cause.py         # Causality computation logic
│   ├── auto.py          # Automata manipulation utilities
│   └── parse.py         # Parsing utilities for automata formats
├── tlsf_specs/          # TLSF specification examples
├── results/             # Output directory for generated results
├── .devcontainer/       # Docker development environment configuration
├── requirements.txt     # Python dependencies
└── trace_visualizer.py  # Tool for visualizing traces
```

## Dependencies

### Core Libraries

- **[Spot](https://spot.lre.epita.fr/)** (v2.14.1): Library for omega-automata manipulation and LTL/PSL model checking
- **[buddy](https://sourceforge.net/projects/buddy/)**: BDD (Binary Decision Diagram) library used by Spot
- **Python 3.12+**: Main implementation language

### Python Packages

Key dependencies from `requirements.txt`:
- `click==8.1.8`: Command-line interface creation
- `graphviz==0.21`: Graph visualization
- `hoa_utils_redux==0.1.1`: HOA format utilities
- `hoax-hoa-executor==0.1.4`: Random trace generation from HOA
- `ltlf2dfa==1.0.1`: LTLf to DFA conversion
- `numpy==2.3.3`: Numerical computations
- `scipy==1.16.2`: Scientific computing
- `sympy==1.14.0`: Symbolic mathematics
- `typer==0.15.4`: CLI application framework

### External Tools

- **ltlsynt**: Part of Spot, used for TLSF synthesis
- **syfco**: Synthesis format converter (included in Docker image)
- **mona**: Monadic second-order logic tool

## Installation

### Option 1: Using Docker (Recommended)

We provide a pre-built Docker image with all dependencies installed:

```bash
# Pull the Docker image
docker pull nik101010/tempo-rl:latest

# Run the container
docker run -it -v $(pwd):/tempo-rl nik101010/tempo-rl:latest
```

### Option 2: Local Installation

1. Install Spot library (requires compilation from source):
```bash
wget http://www.lre.epita.fr/dload/spot/spot-2.14.1.tar.gz
tar xzf spot-2.14.1.tar.gz
cd spot-2.14.1
./configure --prefix=/usr/local --enable-python --enable-tools
make && sudo make install
```

2. Install Python dependencies:
```bash
pip install -r requirements.txt
```

3. Install additional tools:
```bash
# For visualization
apt-get install graphviz  # or brew install graphviz on macOS

# For TLSF synthesis
# syfco and mona need to be installed separately
```

## Docker Development Environment

The project includes a complete Docker development environment with VS Code Dev Container support.

### Using the Docker Image

The Docker image `nik101010/tempo-rl:latest` contains:
- All required Python packages
- Spot library with Python bindings
- syfco for TLSF conversion
- mona for automata operations
- Development tools (zsh, oh-my-zsh, git)

### Building the Docker Image

If you need to rebuild the image:

```bash
cd .devcontainer
docker build -f HOA.Dockerfile -t tempo-rl:latest ..
```

### Using with VS Code Dev Containers

1. Install the "Dev Containers" extension in VS Code
2. Open the project folder in VS Code
3. Press `Ctrl+Shift+P` and select "Dev Containers: Reopen in Container"
4. VS Code will automatically use the configuration in `.devcontainer/devcontainer.json`

The dev container configuration includes:
- Python development extensions
- Vim emulation
- GitHub integration
- Automatic mounting of the workspace
- SSH agent forwarding for git operations

### Docker Compose (Alternative)

Create a `docker-compose.yml` file:

```yaml
version: '3.8'
services:
  tempo-rl:
    image: nik101010/tempo-rl:latest
    volumes:
      - .:/tempo-rl
      - ./tlsf_specs:/specs
    working_dir: /tempo-rl
    command: zsh
    stdin_open: true
    tty: true
```

Then run:
```bash
docker-compose run --rm tempo-rl
```

## Usage

### Basic Usage

Run the CORP synthesis algorithm:

```bash
python corp.py -s ./examples/simple1/system.hoa \
               -e ./examples/simple1/effect.txt \
               -t ./examples/simple1/trace.txt \
               -o ./examples/simple1/result.hoa
```

### Complete Pipeline

Use the make_trace module to run the full pipeline:

```bash
python -m make_trace --input specs/example.tlsf --output results/
```

This will:
1. Convert TLSF to HOA format
2. Generate random traces
3. Validate traces
4. Perform causality analysis
5. Extract reasoning

### Input Formats

#### System (-s): HOA Format
Systems are specified as Mealy machines in HOA format with controllable APs:

```
HOA: v1
States: 2
Start: 0
AP: 2 "a" "b"
acc-name: Buchi
Acceptance: 0 t
controllable-AP: 1
--BODY--
State: 0
[!0&!1] 0
[0&1] 1
State: 1
[!0&!1] 0
[0&1] 1
--END--
```

#### Trace (-t): Spot Word Format
Traces use Spot's word format with lasso-shaped representation:

```
a&b;cycle{a&b;a&b}
```

#### Effect (-e): LTL or HOA Format
Effects can be specified as LTL formulas:

```
<>b
```

Or as Büchi automata in HOA format for more complex omega-regular properties.

### Visualization

Interactive notebooks are available for visualization:

```bash
cd notebooks/
python3 -m notebook
```

Then open:
- `stepbystep.ipynb`: Step-by-step algorithm visualization
- `visualize.ipynb`: Automata visualization tool

## Pipeline Stages

1. **TLSF → HOA**: Synthesize temporal logic specifications into automata
2. **Trace Generation**: Generate random finite traces from automata
3. **Trace Validation**: Check trace acceptance and convert formats
4. **Causality Analysis**: Generate causal automata using CORP
5. **Reasoning Extraction**: Extract causal explanations from traces

## Output Files

The pipeline generates the following outputs:
- `system.hoa`: Synthesized automaton
- `hoax_output.txt`: Raw hoax trace output
- `trace.txt`: Spot-format trace
- `effect.txt`: Effect specification for output APs
- `causal.hoa`: Causal automaton
- `reasoning.txt`: Extracted reasoning trace

## Examples

Example specifications are provided in the `tlsf_specs/` directory. These demonstrate various temporal logic synthesis problems and their solutions.

## Development

### Running Tests

Check tool availability:
```bash
python -m make_trace --check-tools
```

### Debugging

Enable debug logging:
```bash
export DEBUG=1
python -m make_trace --input specs/example.tlsf --output results/
```

## References

- **CORP Algorithm**: "Synthesis of Temporal Causality" by Bernd Finkbeiner, Hadar Frenkel, Niklas Metzger and Julian Siber. CAV 2024.
- **Spot Library**: [https://spot.lre.epita.fr/](https://spot.lre.epita.fr/)
- **TLSF Format**: [https://arxiv.org/abs/1604.02284](https://arxiv.org/abs/1604.02284)
- **HOA Format**: [http://adl.github.io/hoaf/](http://adl.github.io/hoaf/)

## License

This project is licensed under the terms specified in the LICENSE file.

## Contributing

Contributions are welcome! Please ensure:
1. Code follows existing conventions
2. Tests pass with both `npm run lint` and type checking
3. Documentation is updated for new features
4. Docker image builds successfully if dependencies change

## Support

For issues and questions:
- Open an issue on the GitHub repository
- Consult the documentation in the `notebooks/` directory
- Check the example specifications in `tlsf_specs/`