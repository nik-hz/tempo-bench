# -*- coding: utf-8 -*-
"""
Automata Reasoning Pipeline

A comprehensive pipeline for temporal logic synthesis, trace generation,
causality analysis, and reasoning extraction.

Pipeline:
    TLSF -> Automata -> Trace -> Causality -> Reasoning

Modules:
    - spot_synthesis: TLSF to HOA synthesis using Spot
    - hoax_trace: Random trace generation using hoax
    - trace_checker: Trace validation and format conversion
    - causality: Causal HOA generation using corp
    - reasoning: Reasoning trace extraction
    - utils: Helper functions and utilities
"""

__version__ = "1.0.0"
