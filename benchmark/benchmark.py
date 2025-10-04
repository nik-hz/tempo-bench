import json
import os
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

# import torch
from openai import OpenAI
from torch.utils.data import DataLoader
from torch.utils.tensorboard import SummaryWriter
from tqdm import tqdm

from dataset import TempoBench_Dataset

# NOTE You should set model prices here from OpenRouter docs (per 1M tokens)
MODEL_PRICES = {
    "openai/gpt-4o-mini": {"prompt": 0.15, "completion": 0.60},  # $/1M tokens
    "openai/gpt-4o": {"prompt": 2.50, "completion": 10.00},
    "anthropic/claude-3.5-sonnet": {"prompt": 3.00, "completion": 15.00},
    # Add more from https://openrouter.ai/models
}


class BenchmarkRunner:
    def __init__(
        self,
        dataset_path,
        task,
        model_id="openai/gpt-4o-mini",
        batch_size=4,
        log_dir="runs/tempo_bench",
        max_workers=8,
    ):
        self.task = task
        self.dataset = TempoBench_Dataset(dataset_path, tokenizer=None, task=task)
        self.dataloader = DataLoader(self.dataset, batch_size=batch_size, shuffle=False)

        # OpenRouter client
        self.client = OpenAI(
            base_url="https://openrouter.ai/api/v1",
            api_key=os.environ["OPENROUTER_API_KEY"],
        )
        self.model_id = model_id

        # TensorBoard writer
        self.writer = SummaryWriter(log_dir=log_dir)

        self.max_workers = max_workers

    def _query_openrouter(self, prompt: str) -> dict:
        """Send a single prompt to OpenRouter and return structured result."""
        start = time.time()
        response = self.client.chat.completions.create(
            model=self.model_id,
            messages=[{"role": "user", "content": prompt}],
            max_tokens=512,
        )
        latency = time.time() - start

        text = response.choices[0].message.content.strip()
        usage = response.usage.to_dict() if response.usage else {}

        # Cost estimate
        cost = 0.0
        if self.model_id in MODEL_PRICES and usage:
            prices = MODEL_PRICES[self.model_id]
            cost = (usage.get("prompt_tokens", 0) / 1e6) * prices["prompt"] + (
                usage.get("completion_tokens", 0) / 1e6
            ) * prices["completion"]

        return {
            "text": text,
            "latency": latency,
            "usage": usage,
            "cost": cost,
        }

    def evaluate(self):
        total = 0
        correct = 0
        total_cost = 0.0
        total_latency = 0.0
        results = []

        for batch in tqdm(self.dataloader, desc=f"Evaluating {self.task}"):
            prompts, labels = batch

            if isinstance(prompts, str):
                prompts = [prompts]
                labels = [labels]

            responses = []
            with ThreadPoolExecutor(max_workers=self.max_workers) as executor:
                futures = [executor.submit(self._query_openrouter, p) for p in prompts]
                for f in as_completed(futures):
                    responses.append(f.result())

            # Re-align to preserve input order
            responses = [f.result() for f in futures]

            for p, gold, resp in zip(prompts, labels, responses):
                total += 1
                pred = resp["text"]

                # Metrics
                if self.task == "trace_acceptance":
                    gold_bin = "yes" in gold.lower()
                    pred_bin = "yes" in pred.lower()
                    is_correct = gold_bin == pred_bin
                elif self.task == "causality":
                    is_correct = gold.strip() in pred.strip()
                else:
                    is_correct = False

                if is_correct:
                    correct += 1

                total_cost += resp["cost"]
                total_latency += resp["latency"]

                results.append(
                    {
                        "prompt": p,
                        "gold": gold,
                        "pred": pred,
                        "correct": is_correct,
                        "latency": resp["latency"],
                        "usage": resp["usage"],
                        "cost": resp["cost"],
                    }
                )

        accuracy = correct / total if total > 0 else 0.0
        avg_latency = total_latency / total if total > 0 else 0.0
        avg_cost = total_cost / total if total > 0 else 0.0

        # Log to TensorBoard
        self.writer.add_scalar(f"{self.task}/accuracy", accuracy)
        self.writer.add_scalar(f"{self.task}/avg_latency", avg_latency)
        self.writer.add_scalar(f"{self.task}/avg_cost_usd", avg_cost)
        self.writer.flush()

        print(f"[✓] {self.task} benchmark done.")
        print(f"    Accuracy: {accuracy:.3f}")
        print(f"    Avg latency: {avg_latency:.2f} sec")
        print(f"    Avg cost: ${avg_cost:.4f}")

        return results, {
            "accuracy": accuracy,
            "avg_latency": avg_latency,
            "avg_cost": avg_cost,
        }


# tensorboard --logdir runs/tempo_bench --host 0.0.0.0 --port 6006
if __name__ == "__main__":
    runner = BenchmarkRunner(
        dataset_path="/workspaces/tempo-bench/dataset/sample.jsonl",
        task="trace_acceptance",
        model_id="openai/gpt-4o-mini",
        batch_size=4,
        max_workers=8,
    )

    results, stats = runner.evaluate()

    with open("results.jsonl", "w") as f:
        for r in results:
            f.write(json.dumps(r) + "\n")
