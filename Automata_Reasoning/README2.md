#Usage
The Automata Reasoning folder takes a TLSF File -> Generates an Automata -> Generates a Random Finite Trace -> Checks the validity of that Trace ->
Converts Trace to Spot friendly Syntax -> Creates a causality HOA -> and Extracts a reasoning.txt file.
##Requirments
- [Python 3](https://www.python.org/)(tested with version 3.12.2)
- [Spot](https://spot.lre.epita.fr/)(tested with version 2.14.1)
- [Syfco](https://github.com/reactive-systems/syfco) (tested with version v1.2.1.2)
- [Hoax-HOA-Executor](https://github.com/lou1306/hoax) (tested with version 0.1.4)
- [Corp](https://github.com/reactive-systems/corp/tree/main) (tested by cloning from source)
Follow installation instructions for syfco, spot, and python3 from links.
```bash
python3 -m pip install hoax-hoa-executor
git clone <Corp.git>
```
##Current Work Flow
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
python check_trace.py ./<PATHFORSYSTEMHOA>.hoa <APs listed in order of appearance in hoa seperated by spaces>
#EXAMPLE output Trace for spot
#!g_0&g_1&!g_2&!g_3&r_0&!r_1&r_2&r_3;!g_0&!g_1&!g_2&g_3&!r_0&!r_1&r_2&!r_3;cycle{1}
#cycle{1} is tagged onto the end so that the finite trace is accepted with buchi automata
