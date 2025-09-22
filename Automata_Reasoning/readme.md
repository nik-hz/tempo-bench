# Automata Reasoning

A comprehensive pipeline for temporal logic synthesis, trace generation, causality analysis, and reasoning extraction.

## Notes
effect.txt is chosen by us as the effect we want to monitor
TODO: Brute force the effect finder

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

## 

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