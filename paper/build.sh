#!/usr/bin/env bash
# Build the paper.
#
# Layout follows the Tectonic v2 convention: sources live in ./src, the
# manifest is ./Tectonic.toml.
#
#   Figures : uv run plots/make_figures.py   (uv provisions matplotlib etc.)
#   Engine  : `tectonic -X build` (fetches the bundle once, then cached)
#             or, with a full TeX Live + acmart, latexmk -pdf src/main.tex.
set -euo pipefail
cd "$(dirname "$0")"

# Optional first argument (or $ENV) tags the environment; figures are written to
# src/figures/<env>/ so per-machine plots stay separate.  Data is shared
# (plots/data/), produced by `xmake build paper_data`.
ENV="${1:-${ENV:-}}"

# regenerate the data figures
if command -v uv >/dev/null 2>&1; then
    if [ -n "$ENV" ]; then uv run plots/make_figures.py --env "$ENV"; else uv run plots/make_figures.py; fi
else
    echo "uv not found; install uv (https://docs.astral.sh/uv/) to render figures" >&2
    echo "(compile will fall back to placeholder boxes)" >&2
fi

# Point the LaTeX build at the selected environment's figure directory.  With
# no tag we use the shared figures/ directory as a fallback.
if [ -n "$ENV" ]; then
    printf '\\renewcommand{\\figroot}{figures/%s/}\n' "$ENV" > src/figures/env.tex
else
    rm -f src/figures/env.tex
fi

if command -v kpsewhich >/dev/null 2>&1 && kpsewhich acmart.cls >/dev/null 2>&1; then
    latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=src src/main.tex
elif command -v tectonic >/dev/null 2>&1; then
    tectonic -X build          # outputs to build/<name>/<name>.pdf
else
    echo "No usable LaTeX engine (need pdflatex+acmart, or tectonic)." >&2
    exit 1
fi
