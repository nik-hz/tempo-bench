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

    def construct_acceptance_trace(self, result: dict):
        """Read in hoax and hoa to construct a NL version of the trace.

        hoax: "0: (0, {'s_0'}, 3)\n0: (3, {'g_1', 's_1'}, 12)\n..."
        hoa:  full HOA string
        """
        # hoa = result["hoa"]
        aps = result["aps"]
        hoax = result["hoax"]

        # TODO extract the state rules for each state from the hoa
        nl_transitions = []
        for line in hoax.strip().split("\n"):
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
            nl_transitions.append(
                f"From state {start}, "
                f"on inputs {inputs_str}, "
                f"the automaton moves to state {nxt}."
            )

        prompt = (
            "These are the corresponding state transitions to the automaton:\n\n"
            + "\n".join(nl_transitions)
            + f"\n\n{hoax}"
        )
        return prompt

    def construct_causality_label(self, result: dict) -> str:
        """Construct a descriptive causality label with both NL explanation and the raw
        JSON causality mapping."""
        trace = result["trace"]
        effect = result["effects"][0]
        causality = result["causality"]

        # Count Xs in the effect name → max steps to show
        num_x = effect.count("X")
        max_steps = num_x + 1  # include step 0..num_x

        nl_text = (
            "Causal explanations:\n"
            f"Effect: {effect} (showing first {max_steps} steps of trace)\n"
            "The relevant portion of the trace is:"
            + ";".join(trace.split(";")[0:max_steps])
        )
        return f"{nl_text}\n\nRaw JSON:\n{json.dumps(causality, indent=2)}"

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
                f"You are given an automaton (HOA format) with APs {result['aps']}.\n\n"
                f"Automaton:\n{hoa_pretty}\n\n"
                f"Trace:\n{result['trace']}\n\n"
                f"Question: Does the automaton accept this trace? "
                "Solve this by stepping trough the state machine."
            )
            label = (
                f"{self.construct_acceptance_trace(result=result)}\n\n"
                f"{"Accepted: Yes" if result["accepted"] else "Accepted: No"}"
            )

            if self.tokenizer:
                inputs = self.tokenizer(
                    prompt, return_tensors="pt", padding=True, truncation=True
                )
                labels = self.tokenizer(label, return_tensors="pt")["input_ids"]
                return inputs, labels
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

    # Use a small JSONL file (maybe 1–2 lines from your sample)
    path = "/workspaces/tempo-bench/dataset/sample.jsonl"  # adjust to your file

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
