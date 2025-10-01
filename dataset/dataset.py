import ast
import json

# import torch
from torch.utils.data import Dataset

# from pathlib import Path


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
        hoax = result["hoax"]

        # TODO extract the state rules for each state from the hoa
        nl_transitions = []
        for line in hoax.strip().split("\n"):
            _, tup_str = line.split(":", 1)
            start, inputs, nxt = ast.literal_eval(tup_str.strip())
            inputs_list = sorted(list(inputs))  # stable order
            if inputs_list:
                inputs_str = " and ".join(inputs_list)
            else:
                inputs_str = "no inputs"
            nl_transitions.append(
                f"From state {start},"
                f"on inputs {inputs_str},"
                f"the automaton moves to state {nxt}."
            )

        prompt = (
            "These are the corresponding state transitions to the automaton:\n\n"
            + "\n".join(nl_transitions)
        )
        return prompt

    def __len__(self):
        return len(self.data)

    def __getitem__(self, idx):
        result = self.data[idx]["result"]

        if self.task == "trace_acceptance":
            prompt = (
                f"You are given an automaton (HOA format) with APs {result['aps']}.\n\n"
                f"Automaton:\n{result['hoa']}\n\n"
                f"Trace:\n{result['trace']}\n\n"
                f"Question: Does the automaton accept this trace?"
                "Solve this by stepping trough the state machine."
            )
            label = (
                f"{self.construct_acceptance_trace(result=result)}\n\n"
                f"{"Yes" if result["accepted"] else "No"}"
            )

            if self.tokenizer:
                inputs = self.tokenizer(
                    prompt, return_tensors="pt", padding=True, truncation=True
                )
                labels = self.tokenizer(label, return_tensors="pt")["input_ids"]
                return inputs, labels
            return prompt, label

        elif self.task == "causality":
            prompt = (
                f"Trace:\n{result['trace']}\n\n"
                f"Effects to analyze: {result['effects']}\n\n"
                f"Explain the causal constraints that make each effect true."
            )
            label = json.dumps(result["causality"])
            return prompt, label


if __name__ == "__main__":
    print("[ ] Testing TempoBench Dataset builder")

    # Use a small JSONL file (maybe 1–2 lines from your sample)
    path = "sample.jsonl"  # adjust to your file
    ds = TempoBench_Dataset(path, tokenizer=None, task="trace_acceptance")

    print(f"Loaded {len(ds)} items")
    print("--- First item ---")
    prompt, label = ds[0]
    print("Prompt:\n", prompt)  # truncate for readability
    print("Label:", label)
