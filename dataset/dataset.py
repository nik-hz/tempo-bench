import ast
import json
import re
import sys

# import torch
from torch.utils.data import Dataset

# from pathlib import Path


def replace_indices_with_APs(line: str, aps: list[str]) -> str:
    """Replace only AP indices inside [...] with their AP names.

    Leaves state numbers outside the brackets unchanged.
    """

    def repl(match):
        idx = int(match.group(0))
        if 0 <= idx < len(aps):
            return aps[idx]
        return match.group(0)  # leave as is if not a valid AP index

    # process only the [...] section
    return re.sub(
        r"\[(.*?)\]", lambda m: "[" + re.sub(r"\b\d+\b", repl, m.group(1)) + "]", line
    )


class TempoBench_Dataset(Dataset):
    def __init__(self, path, tokenizer=None, task="trace_acceptance"):
        self.data = []
        self.task = task
        self.tokenizer = tokenizer

        with open(path, "r") as f:
            for line in f:
                item = json.loads(line)
                if item.get("error") is None:  # skip failed runs
                    self.data.append(item)

    def construct_acceptance_trace(self, result: dict, hoax_idx: int = -1):
        """Read in hoax and hoa to construct a NL version of the trace.

        hoax: "0: (0, {'s_0'}, 3)\n0: (3, {'g_1', 's_1'}, 12)\n..."
        hoa:  full HOA string

        also returns a json object of the hoax for exact labeling
        """
        # hoa = result["hoa"]
        aps = result["aps"]
        hoax = result["hoax"]

        # TODO extract the state rules for each state from the hoa
        nl_transitions = []
        json_transitions = {}
        for i, line in enumerate(hoax.strip().split("\n")):
            _, tup_str = line.split(":", 1)
            start, inputs, nxt = ast.literal_eval(tup_str.strip())
            inputs_list = sorted(list(inputs))  # stable order
            inputs_list_named = [
                aps[int(ap.split("_")[-1])] if ap.isdigit() else ap
                for ap in inputs_list
            ]
            if inputs_list_named:
                inputs_str = " and ".join(inputs_list)
            else:
                inputs_str = "no inputs"

            # NL prompt
            nl_transitions.append(
                f"From state {start}, "
                f"on inputs {inputs_str}, "
                f"the automaton moves to state {nxt}."
            )
            # JSON prompt
            json_transitions[f"step_{i}"] = {
                "current state": start,
                "defining inputs": inputs_str,
                "next state": nxt,
            }
            if hoax_idx > 0 and i >= hoax_idx:
                break

        prompt = (
            "These are the corresponding state transitions to the automaton:\n\n"
            + "\n".join(nl_transitions)
        )
        json_gt = (
            "\n\n### JSON Ground Truth ###\n"
            + f"{json.dumps(json_transitions, indent=4)}"
        )

        return prompt, json_gt

    def construct_causality_label(self, result: dict) -> str:
        """Construct a descriptive causality label with both NL explanation and the raw
        JSON causality mapping."""
        trace = result["trace"]
        effect = result["effects"][0]
        causality = result["causality"]
        # hoax = result["hoax"]

        # Count Xs in the effect name → max steps to show
        num_x = effect.count("X")
        max_steps = num_x + 1  # include step 0..num_x

        # edge case, cycle{1} is in first step, should never happen though
        trace_steps = [s for s in trace.split(";") if s and not s.startswith("cycle")]
        truncated_trace = ";".join(trace_steps[0:max_steps])

        nl_text = (
            "Causal explanations:\n"
            f"Effect: {effect} (showing first {max_steps} steps of trace)\n"
            f"The relevant portion of the trace is: {truncated_trace}\n"
            f"Reasoning over the transitions for the first {max_steps}: \n\n"
            f"{self.construct_acceptance_trace(result, num_x)[0]}"
            f"\n\n### JSON Ground Truth ###:\n{json.dumps(causality, indent=2)}"
        )
        return nl_text

    def __len__(self):
        return len(self.data)

    def __getitem__(self, idx):
        result = self.data[idx]["result"]
        aps = result["aps"]
        hoa_pretty = "\n".join(
            replace_indices_with_APs(line, aps) for line in result["hoa"].splitlines()
        )

        if self.task == "trace_acceptance":
            prompt = (
                "This is task that requires you to trace through a state machine.\n"
                "You must step through state by state, compare the inputs with "
                "all accepted inputs at each state, determine the transition and then "
                "after consuming the whole state, determine if this trace will be "
                "accepted by the state machine.\n\n"
                f"You are given an automaton (HOA format) with APs {result['aps']}.\n\n"
                f"Automaton:\n{hoa_pretty}\n\n"
                f"Trace:\n{result['trace']}\n\n"
                f"Question: Does the automaton accept this trace? "
                "Solve this by stepping trough the state machine."
            )
            label = (
                f"{self.construct_acceptance_trace(result=result)[0]}\n"
                f"{self.construct_acceptance_trace(result=result)[1]}\n\n"
                f"{"Accepted: Yes" if result["accepted"] else "Accepted: No"}"
            )

            if self.tokenizer:
                inputs = self.tokenizer(
                    prompt, return_tensors="pt", padding=True, truncation=True
                )
                labels = self.tokenizer(label, return_tensors="pt")["input_ids"]
                return {"inputs": inputs, "labels": labels}

            return prompt, label

        elif self.task == "causality":
            """
            NOTE: Will's NL description of the task it is a credit assignment task over
            time the goal is to identify over time the minimumn set of inputs that were
            given which caused the effect that is given. Specifically your goal is to
            find the minimumn set of inputs over time such that if one did not occur
            the output observed would also not occur.
            """
            prompt = (
                "This is a credit assignment task over time.\n"
                "Your goal is to identify the minimal set of inputs "
                "that caused a given effect in the automaton. "
                "If any one of these inputs were missing, the effect "
                "would not have occurred.\n\n"
                f"You are given an automaton (HOA format) with APs:\n"
                f"{result['aps']}\n\n"
                f"Automaton:\n{hoa_pretty}\n\n"
                f"Trace:\n{result['trace']}\n\n"
                f"Effects to analyze:\n{result['effects']}\n\n"
                "Explain the causal constraints step by step.\n"
            )

            label = self.construct_causality_label(result)
            return prompt, label


if __name__ == "__main__":
    print("[ ] Testing TempoBench Dataset builder")
    path = "/workspaces/tempo-bench/data/sample.jsonl"

    if sys.argv[1] == "-t":
        ds = TempoBench_Dataset(path, tokenizer=None, task="trace_acceptance")

        print(f"Loaded {len(ds)} items")
        print("--- First item ---")
        prompt, label = ds[0]
        print("Prompt:\n", prompt)  # truncate for readability
        print("Label:\n", label)

    elif sys.argv[1] == "-c":
        ds = TempoBench_Dataset(path, tokenizer=None, task="causality")

        print(f"Loaded {len(ds)} items")
        print("--- First item ---")
        prompt, label = ds[0]
        print("Prompt:\n", prompt)
        print("Label:\n", label)
