import json

import torch
from torch.utils.data import Dataset


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

    # TODO: We should have some NL template that converts the

    def __len__(self):
        return len(self.data)

    def __getitem__(self, idx):
        item = self.data[idx]
        result = item["result"]

        if self.task == "trace_acceptance":
            # Example task: predict acceptance from trace
            x = result["trace"]
            y = int(result["accepted"])
            if self.tokenizer:
                x = self.tokenizer(x)
            return x, torch.tensor(y, dtype=torch.long)

        elif self.task == "causality":
            # Example: predict causality from trace
            x = result["trace"]
            y = result["causality"]
            if self.tokenizer:
                x = self.tokenizer(x)
            return x, y

        elif self.task == "hoa_embedding":
            # Example: encode automaton structure
            x = result["hoa"]
            if self.tokenizer:
                x = self.tokenizer(x)
            return x

        else:
            raise ValueError(f"Unknown task: {self.task}")


# if __name__ == "__main__":
