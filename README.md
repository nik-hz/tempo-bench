
# CORP: Causes for Omega-Regular Properties

This repository contains CORP, a prototype tool to synthesize causes for effects that are omega-regular properties on a trace of a reactive system.

CORP implements a cause-synthesis algorithm that is described in more detail in the related paper [1]. This automata-based algorithm constructs a non-deterministic Büchi automaton that characterizes the cause for a given omega-regular effect on a given trace of a given reactive system.

## Structure & Content

This repository contains all source files of CORP, which is written in Python, as well as a number of examples and Python notebooks that illustrate the algorithm proposed in [1].

- The Python source files, such as `corp.py` are found in the same folder as this README.
- `examples` contains two example problems.
- `notebooks` contains `stepbystep.ipynb`, a Python notebook that runs the main algorithm step-by-step and utilizes Spot's excellent visualization to illustrate the intermediate results, and `visualize.ipynb`, a notebook for simply visualizing automata, e.g., the ones computed form the main script.


## Dependencies

CORP consists of Python scripts that manipulate automata from the popular Spot library and that call functions of the same library. Hence, you will only need to install the following dependencies to run CORP:

- [Python 3](https://www.python.org/downloads/) (tested with version 3.11),
- [Spot](https://spot.lre.epita.fr/install.html) (tested with version 2.11.6). Note that we explicitly require Spot's Python bindings so do not disable these during Spot's installation (per default, they are enabled). For many systems, you will need to specify a target directory for the Python bindings by running Spot's `./configure` with `--with-pythondir=...`. Spot's `./configure` will print a warning message at the very end if this is necessary, and will suggest a number of possible directories searched by your Python installation that you can use for again invoking `./configure --with-pythondir=...` before moving on to `make`.

## Usage

CORP is used via the script `corp.py`. For instance, a cause for the problem in folder `examples/simple1` can be synthesized with the command:

```
python3 corp.py -s ./examples/simple1/system.hoa -e ./examples/simple1/effect.txt -t ./examples/simple1/trace.txt -o ./examples/simple1/result.hoa
```

This specifies a system with the argument `-s`, an effect with `-e`, a trace with `-t` and outputs an automaton in the file specified after `-o`. These arguments must always be provided. See the manual (`-h`) for further optional arguments.

We can use CORP also to check whether a given hypothesis is indeed the cause as follows:

```
python3 corp.py -s ./examples/simple1/system.hoa -e ./examples/simple1/effect.txt -t ./examples/simple1/trace.txt -o ./examples/simple1/result.hoa --check ./examples/simple1/hypothesis.hoa
```

Note that this command will still output the true cause that was synthesized along the way.

### Input Formats

To maximize interoperability, CORP uses the same formats as the popular Spot library for all inputs. Systems need to be specified in Spot's [supported fragment](https://spot.lre.epita.fr/hoa.html) of the [Hanoi automata format](http://adl.github.io/hoaf/), traces in Spot's [word format](https://spot.lre.epita.fr/ipynb/word.html), and effects either in Spot's [LTL syntax](https://spot.lre.epita.fr/ioltl.html) or as non-deterministic Büchi automata (to capture omega-regular properties) in the Hanoi format. A comprehensive introduction to these concepts can be found [here](https://spot.lre.epita.fr/concepts.html).

#### System (`-s`): 
We use the Hanoi automaton format to encode reactive systems (Mealy machines) as Büchi automata with a generic acceptance condition, and specify outputs via the `controllable-AP` field, e.g.:

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

specifies a simple automaton with input `a` and output `b`, two states and transitions such that every input `a` is coupled with output `b`. Note that for the synthesis algorithm to work properly, every state input combination needs to be modeled in the automaton, e.g., it needs to be a proper Mealy machine. Note that CORP can easily be used with other formats such as [AIGER](https://fmv.jku.at/aiger/FORMAT.aiger) through utilizing the translation capabilities of the SPOT library.

#### Trace (`-t`): 
The trace is specified in Spot's word format, e.g.: 
```
a&b;cycle{a&b;a&b}
```
is a (lasso-shaped) trace of the aforementioned system: a list of symbolic formulas describing the transition labels separated by semicolons, with a looping `cycle{W}` identified that symbolizes that `W` is repeated infinitely often. 

#### Effect (`-e`): 
The effect can either be specified in Spot's [LTL format](https://spot.lre.epita.fr/ioltl.html), e.g., as a formula: 
```
<>b
```
or as an automaton in the Hanoi format (to express all omega-regular properties through, e.g., an non-deterministic Büchi automaton), e.g.:
```
HOA: v1
States: 2
Start: 1
AP: 1 "b"
acc-name: Buchi
Acceptance: 1 Inf(0)
properties: trans-labels explicit-labels state-acc complete
properties: deterministic terminal
--BODY--
State: 0 {0}
[t] 0
State: 1
[0] 0
[!0] 1
--END--
```
which characterizes the same property as the aforementioned formula. The optional hypotheses for cause checking can be provided in the same ways as effects.

## Visualization via Python Notebooks

Since CORP's output and intermediate results are Hanoi omega-automata that are not easy to parse, we provide Python notebooks for visualization.

To run these, you will need to install additional dependencies:

- [GraphViz](https://graphviz.org/download/) on your _system_, e.g., with `brew install graphviz` or `apt-get install graphviz`

- IPython and some way to run Jupyter notebooks, e.g., `notebook`. These can be installed with:

```
pip3 install ipython notebook
```

Using the above, you can then run and explore the provided notebook in a browser, starting from the following command in the `notebooks/` folder:

```
python3 -m notebook
```


# NOTES 
use a domain specific language, or be aware that noisy channel, twmporal to NL via LLM is not deterministic anymore due to the nature of NL

## Literature

[1] Synthesis of Temporal Causality. Bernd Finkbeiner, Hadar Frenkel, Niklas Metzger and Julian Siber. CAV 2024.
