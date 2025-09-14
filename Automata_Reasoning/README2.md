# Usage
The Automata Reasoning folder takes a TLSF File -> Generates an Automata -> Generates a Random Finite Trace -> Checks the validity of that Trace ->
Converts Trace to Spot friendly Syntax -> Creates a causality HOA -> and Extracts a reasoning.txt file.
##Requirments
- [Python 3](https://www.python.org/) (tested with version 3.12.2)
- [Spot](https://spot.lre.epita.fr/) (tested with version 2.14.1)
- [Syfco](https://github.com/reactive-systems/syfco) (tested with version v1.2.1.2)
- [Hoax-HOA-Executor](https://github.com/lou1306/hoax) (tested with version 0.1.4)
- [Corp](https://github.com/reactive-systems/corp/tree/main) (tested by cloning from source)
Follow installation instructions for syfco, spot, and python3 from links.
```bash
python3 -m pip install hoax-hoa-executor
git clone <Corp.git>
```
## Current Work Flow
for a given TLSF file synthesize an automata
```bash
ltlsynt --tlsf /<PATHFORTLSFFILE>.tlsf > ./<PATHFORSYSTEMHOA>.hoa
```
Generate a finite trace (By changing the bound in the .toml file you can change the length of the trace
```bash
python3 run_hoax.py ./<PATHFORSYSTEMHOA>.hoa random_generator.toml
```
Check Trace and convert to a spot format
```bash
python3 check_trace.py ./<PATHFORSYSTEMHOA>.hoa <APs listed in order of appearance in hoa seperated by spaces>
#EXAMPLE output Trace for spot
#!g_0&g_1&!g_2&!g_3&r_0&!r_1&r_2&r_3;!g_0&!g_1&!g_2&g_3&!r_0&!r_1&r_2&!r_3;cycle{1}
#cycle{1} is tagged onto the end so that the finite trace is accepted with buchi automata
```
Generate a reasoning Trace
```bash
python3 corp.py -s ./<PATHFORSYSTEMHOA>.hoa -e ./<relativepath>/effect.txt -t ./<relativepath>/trace.txt -o ./<relativepath>/result.hoa
#the effect.txt will need to be created and written with all outputs of the system
#it is written like this "<>(AP_Ouput1) & <>(AP_Output2) ...
# the trace.txt is created by the check_trace.py
```
Generate Reasoning Trace
Finally we leverage the causal hoa and the trace to generate a reasoning txt file for the trace. The effect.txt is used to know which outputs in the trace we are interested in accounting for.
```bash
python3 Reasoning.py ./<relativepath>/result.hoa ./<relativepath>/trace.txt ./<relativepath>/effect.txt
```
## Example Reasoning Trace
```
==== Reasoning Trace ====

Time 0:
  Inputs true: AP0, AP2, AP3
  Outputs true: g_1
  Transition: 40 → 42 via [!1&3]
  Interpretation: Because inputs {r_0, r_2, r_3} were true, system caused outputs {g_1}.

Time 1:
  Inputs true: AP2
  Outputs true: g_3
  Transition: 42 → 34 via [!1&2&!3]
  Interpretation: Because inputs {r_2} were true, system caused outputs {g_3}.

Time 2:
  Inputs true: AP0, AP2
  Outputs true: g_2
  Transition: 34 → 30 via [t]
  Interpretation: Because inputs {r_0, r_2} were true, system caused outputs {g_2}.

Time 3:
  Inputs true: AP0, AP3
  Outputs true: g_2
  Transition: 30 → 25 via [t]
  Interpretation: Because inputs {r_0, r_3} were true, system caused outputs {g_2}.

Time 4:
  Inputs true: AP0, AP1, AP2
  Outputs true: g_3
  Transition: 25 → 21 via [t]
  Interpretation: Because inputs {r_0, r_1, r_2} were true, system caused outputs {g_3}.

Time 5:
  Inputs true: AP3
  Outputs true: g_1
  Transition: 21 → 17 via [t]
  Interpretation: Because inputs {r_3} were true, system caused outputs {g_1}.

Time 6:
  Inputs true: AP0, AP1, AP2, AP3
  Outputs true: g_3
  Transition: 17 → 12 via [t]
  Interpretation: Because inputs {r_0, r_1, r_2, r_3} were true, system caused outputs {g_3}.

Time 7:
  Inputs true: AP1
  Outputs true: g_1
  Transition: 12 → 6 via [t]
  Interpretation: Because inputs {r_1} were true, system caused outputs {g_1}.

Time 8:
  Inputs true: AP0, AP1, AP3
  Outputs true: g_1
  Transition: 6 → 1 via [t]
  Interpretation: Because inputs {r_0, r_1, r_3} were true, system caused outputs {g_1}.

Time 9:
  Inputs true: AP3
  Outputs true: g_1
  Transition: 1 → 47 via [t]
  Interpretation: Because inputs {r_3} were true, system caused outputs {g_1}.

Time 10:
  Inputs true: ∅
  Outputs true: ∅
  Transition: 47 → 47 via [!0 | 1 | 2 | 3]
  Interpretation: Because inputs {∅} were true, system caused outputs {∅}.
```
