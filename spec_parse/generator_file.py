from Scarlet.genBenchmarks import SampleGenerator
from Scarlet.ltllearner import LTLlearner
import os, glob

OUT = "bench_out"
SampleGenerator(
    formula_file="formulas_scarlet.txt",
    sample_sizes=[(2, 2)],  # pos,neg per formula
    trace_lengths=[(5, 5)],  # min,max length
    output_folder=OUT,
).generate()


traces = sorted(glob.glob(os.path.join(f"{OUT}/TracesFiles", "*.trace")))
assert traces, "No .trace files generated"
print("Using trace:", traces[0])

learner = LTLlearner(
    input_file=traces[0], timeout=60, thres=0, csvname=os.path.join(OUT, "learn.csv")
)
print("Learned formula:", learner.learn())
