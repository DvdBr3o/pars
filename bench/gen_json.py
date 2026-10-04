#!/usr/bin/env python3
"""Generate the standard JSON benchmark payload: a top-level array of records.

Usage: python3 bench/gen_json.py <size_mb> <out_path>

The record matches bench/bench_boundary.cu so every JSON baseline sees the same
bytes as pars.
"""
import sys

mb = int(sys.argv[1])
path = sys.argv[2]
rec = '{"id":123,"name":"x \\"y\\"","a":[1,2,3],"n":{"z":null}}'
target = mb * 1024 * 1024

with open(path, "w") as f:
    f.write("[")
    first = True
    while f.tell() < target:
        if not first:
            f.write(",")
        first = False
        f.write(rec)
    f.write("]")
