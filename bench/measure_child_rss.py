#!/usr/bin/env python3
"""Peak resident set size (KiB) of a child command.

usage: measure_child_rss.py <cmd> [args...]
"""
import resource
import subprocess
import sys

p = subprocess.Popen(sys.argv[1:], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
p.wait()
print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
