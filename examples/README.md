# naina examples

Working end-to-end examples for each binding. Each one loads two
images, detects the best face in each, embeds them, and prints a
cosine-similarity score plus a same-person verdict.

## Python

```bash
pip install naina opencv-python-headless

python examples/python/face_verify.py path/to/alice_1.jpg path/to/alice_2.jpg
python examples/python/face_verify.py --threshold 0.30 alice.jpg bob.jpg
```

The threshold defaults to `0.36` (SFace's recommended cutoff). Raise it
for stricter matching, lower it for more permissive matching.

## Node / TypeScript

```bash
npm install naina sharp

node examples/node/face_verify.mjs path/to/alice_1.jpg path/to/alice_2.jpg
```

`sharp` handles image decoding so naina stays small and decoder-free.
Any decoder that produces a raw pixel buffer will work — `naina` only
cares about `{ data, width, height, channels }`.

## C++

```bash
# Build naina-core, install it, then:
c++ -std=c++20 examples/cpp/face_verify.cc -lnaina -o face_verify
./face_verify alice.jpg bob.jpg
```

(C++ example pending.)

## What "similarity" means

We L2-normalise every embedding, so `naina.similarity(a, b)` is just the
dot product of two unit vectors — equivalently the cosine of the angle
between them. For the default SFace 128-d embeddings, scores typically
land in:

| Pair | Typical similarity |
|---|---|
| Same person, similar pose / lighting | 0.55 – 0.85 |
| Same person, different pose / age    | 0.40 – 0.60 |
| Different people                     | 0.10 – 0.35 |

The right threshold depends on your false-accept / false-reject budget;
0.36 is a sensible starting point.

## Notes

- The first run downloads YuNet (~230 KB) + SFace (~37 MB) into
  `~/.cache/naina/models/`. Subsequent runs hit the cache.
- Set `NAINA_OFFLINE=1` to disable network access once you've cached
  the models you need.
- Set `NAINA_CACHE=/some/dir` to relocate the cache.
