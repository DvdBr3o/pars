#!/usr/bin/env python3
"""Peak device memory (MiB) used by a child command.

Runs the child and samples `nvidia-smi memory.used` on the given device,
reporting the max minus the pre-run baseline.

usage: measure_child_gpu.py <device_index> <cmd> [args...]
"""
import subprocess
import sys
import time


def used(dev: str) -> int:
    try:
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits", "-i", dev],
            stderr=subprocess.DEVNULL,
        )
        return int(out.decode().strip())
    except Exception:
        return 0


def main() -> int:
    dev = sys.argv[1]
    cmd = sys.argv[2:]
    base = used(dev)
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    peak = base
    while p.poll() is None:
        peak = max(peak, used(dev))
        time.sleep(0.002)
    p.wait()
    peak = max(peak, used(dev))
    print(peak - base)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
