#!/usr/bin/env python3
"""Render the paper figures.  Thin CLI over :mod:`paperplots`.

This is a uv project (see ``paper/pyproject.toml``); run it with:

    uv run plots/make_figures.py                 # all figures
    uv run plots/make_figures.py --only sota scaling
    uv run plots/make_figures.py --formats pdf png svg

Data comes from ``plots/data/*.csv`` (run ``xmake build paper_data`` first).
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

import paperplots as P  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", type=Path, default=P.DATADIR)
    ap.add_argument("--out", type=Path, default=P.FIGDIR)
    ap.add_argument(
        "--only",
        nargs="*",
        default=None,
        help="subset of {sota,scaling,stages,query}",
    )
    ap.add_argument("--formats", nargs="*", default=["pdf", "png"])
    args = ap.parse_args()

    P.use_style()
    pars = P.load_pars(args.data)
    base = P.load_baselines(args.data)
    stages = P.load_stages(args.data)
    parse = P.load_parse(args.data)

    builders = {
        "sota": lambda: P.figure_sota(pars, base),
        "scaling": lambda: P.figure_scaling(pars, base),
        "stages": lambda: P.figure_stages(stages),
        "parse": lambda: P.figure_parse(pars, parse, base),
    }
    names = args.only or list(builders)
    args.out.mkdir(parents=True, exist_ok=True)
    for name in names:
        fig = builders[name]()
        for ext in args.formats:
            fig.savefig(args.out / f"fig_{name}.{ext}")
        plt.close(fig)
        print(f"wrote {args.out}/fig_{name}.{{{','.join(args.formats)}}}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
