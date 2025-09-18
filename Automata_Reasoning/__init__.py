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

from .spot_synthesis import synthesize_tlsf_to_hoa
from .hoax_trace import generate_hoax_trace
from .trace_checker import check_trace, convert_hoax_to_spot
from .causality import generate_causal_hoa
from .reasoning import generate_reasoning_trace
from .utils import extract_aps_from_hoa, create_effect_file

__version__ = "1.0.0"

__all__ = [
    "synthesize_tlsf_to_hoa",
    "generate_hoax_trace",
    "check_trace",
    "convert_hoax_to_spot",
    "generate_causal_hoa",
    "generate_reasoning_trace",
    "extract_aps_from_hoa",
    "create_effect_file",
]