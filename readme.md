# Long Horizon Bench
**Rough idea:** We want to show a task, where LLM handing off to spec is as good as LLM only (or better) but less inference cost and more reliable than LLM --> code. Paper should make a strong argument for using synthesis as a tool in AI agents
**Argument and rq:** LLM Agents are being asked to manage an increasing number of real world applications and tasks. Can we quantify and measure the capability of LLM based agentic systems to juggle these tasks? Is a "zero-shot" context enough? Can the LLM hand off tasks to smaller submodules to increase efficiency? 

## Project overview 
Embedding formal reasoning trace in NL space using synthesized trace converted to NL reasoning paths 

## Running the code 

**convert tlsf to ltl**
`docker run --rm -it -v "$PWD":/work scarlet-ltl syfco -f ltl -m fully robot_grid.tlsf > spec.ltl`

**get alphabet**
``` bash
# inputs and outputs from the TLSF (land on your host)
docker run --rm -v "$PWD":/work -w /work scarlet-ltl syfco -ins  robot_grid.tlsf > ins.txt
docker run --rm -v "$PWD":/work -w /work scarlet-ltl syfco -outs robot_grid.tlsf > outs.txt
```

**Make formulas.txt**
``` bash
# one-liners (bash/zsh)
FORMULA="$(tr -d '\n' < spec.ltl | sed 's/^[[:space:]]*//')"
ALPHABET="$(cat ins.txt outs.txt | tr -s '[:space:]' ',' | sed 's/^,*//; s/,*$//')"
printf '%s;%s\n' "$FORMULA" "$ALPHABET" > formulas.txt
```

**Build docker image to run code**
``` bash
docker buildx build --memory=6g --shm-size=1g -t scarlet-ltl .  
```

## Similar works 
- [ShieldAgent](https://arxiv.org/abs/2503.22738) The embedding from rules seems to be some kind of alignment process
- [WebArena](https://webarena.dev) a realistic web env for building autonomous agents
- [SweBench](https://arxiv.org/pdf/2310.06770) long horizon github code issue resolving
- [Measuring AI ability to complete long tasks](https://arxiv.org/pdf/2503.14499v1)

Going from automatas don't let us do nondeterminism # long_horizon_formal
# long_horizon_formal
