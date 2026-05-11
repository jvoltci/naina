# naina benchmarks

End-to-end latency timings for face detection and embedding. The harness
uses the Python binding, so any backend / platform that supports
`pip install naina` can produce comparable numbers.

## Run

```bash
# 1. Install naina (one-time).
pip install -e .

# 2. Time the default tier on the current host.
python benchmarks/latency.py --target my-laptop

# 3. Time the research tier (opt-in, non-commercial weights).
python benchmarks/latency.py --target my-laptop --research

# 4. Regenerate the README benchmark table from all collected JSONs.
python benchmarks/runner.py --update-readme
```

Each `latency.py` invocation writes `benchmarks/results/<target>-<tier>.json`
with p50 / p95 / p99 for detect + embed, plus host / system info. Commit
those files to publish results.

## Conventions

- `--target` is a free-form label — use something stable and unique
  (`pi5`, `jetson-orin-nano`, `m3-pro`, `x86-rtx4090`, etc).
- Pass `--image path/to/face.jpg` to time on a real face. Without it the
  benchmark uses synthetic noise, which still measures inference latency
  accurately but skips embed timing (no face found).
- Embed numbers are *per face*; detect numbers are *per frame*.

## Accuracy (planned)

Accuracy benchmarks (WIDERFACE, IJB-C, LFW) are scoped for the eval phase
and require dataset downloads that we don't yet automate. The README
benchmark section shows latency only for v0.1.
