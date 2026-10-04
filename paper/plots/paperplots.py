"""paperplots — figure library for the pars paper.

Built on pandas + seaborn + matplotlib (+ optional SciencePlots).  Structure:

    CONFIG      user-editable constants (paths, labels, palette, style)
    load_*      data loading (thin wrappers over the benchmark CSVs)
    figure_*    one function per figure, each returns a matplotlib Figure

To change a figure, edit the corresponding ``figure_*`` function; to change
colours/labels/sizes, edit the CONFIG block only.  Nothing here hard-codes
numbers: everything is read from ``plots/data/*.csv`` produced by
``xmake build paper_data``.

Run via ``uv run plots/make_figures.py`` (uv provisions the dependencies).
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd

# ===========================================================================
# CONFIG — edit this block to restyle every figure
# ===========================================================================
HERE = Path(__file__).resolve().parent
DATADIR = HERE / "data"
FIGDIR = HERE.parent / "src" / "figures"

STYLE = ["science", "no-latex"]

FMT_ORDER = ["json", "csv", "toml", "ini", "clike", "xml", "yaml"]
LABELS = {
    "json": "JSON",
    "csv": "CSV",
    "toml": "TOML",
    "ini": "INI",
    "clike": "C-like",
    "xml": "XML",
    "yaml": "YAML",
}

# pars series (uniform colours) …
PARS_LABEL = {"N1a": "pars stage-1 (device)", "N3c": "pars chunked (host)"}
PARS_COLOR = {"N1a": "#4C72B0", "N3c": "#55A868"}

# … and domain SOTA, one colour per \emph{language}'s own SOTA implementation.
# The SOTA bar/line for a format uses only that format's colour.
SOTA_COLOR = {
    "simdjson": "#C44E52",  # JSON
    "simdcsv": "#8172B3",  # CSV
    "toml++": "#CCB974",  # TOML
    "pugixml": "#64B5CD",  # XML
    "rapidyaml": "#E377C2",  # YAML
    "inih": "#8C564B",  # INI
    "flex": "#DD8452",  # C-like
    "cujson": "#DA8BC3",  # JSON (GPU, if added)
}
SOTA_FALLBACK = "#999999"
# Formats with no known SIMD/GPU/optimized parser -> no SOTA bar.
NO_SOTA: set[str] = set()

FIGSIZE = (7.0, 3.2)
FIGSIZE_WIDE = (7.0, 4.4)

PARS_COLS = [
    "format",
    "impl",
    "variant",
    "scale_mb",
    "mean",
    "p50",
    "p95",
    "pmin",
    "pmax",
    "tokens",
    "bytes",
]
# Stage breakdown on the R1-R6 axis (see the measurement lattice in the paper).
STAGE_COLS = ["format", "scale_mb", "r1", "r2r4", "r3r5", "r6"]
STAGE_LABEL = {
    "r1": "R1 classify",
    "r2r4": "R2$\\cdot$R4 mask+scan",
    "r3r5": "R3$\\cdot$R5 compact",
    "r6": "R6 pair",
}
STAGE_COLOR = {"r1": "#A6CEE3", "r2r4": "#B2DF8A", "r3r5": "#FDBF6F", "r6": "#FB9A99"}




# ===========================================================================
# Style
# ===========================================================================
def use_style() -> None:
    import matplotlib.pyplot as plt
    import seaborn as sns

    try:
        import scienceplots  # noqa: F401

        plt.style.use(STYLE)
    except Exception:
        plt.style.use("seaborn-v0_8-whitegrid")
    sns.set_context("paper")
    plt.rcParams.update(
        {
            "figure.dpi": 120,
            "savefig.bbox": "tight",
            "axes.grid": True,
            "grid.alpha": 0.3,
        }
    )


# ===========================================================================
# Data loading
# ===========================================================================
def _read(path: Path, cols) -> pd.DataFrame:
    if not path.exists():
        raise FileNotFoundError(f"{path} missing — run: xmake build paper_data")
    return pd.read_csv(path, comment="#", names=cols)


def load_pars(data_dir: Path = DATADIR) -> pd.DataFrame:
    return _read(data_dir / "pars.csv", PARS_COLS)


def load_baselines(data_dir: Path = DATADIR) -> pd.DataFrame:
    return _read(data_dir / "baselines.csv", PARS_COLS)


def load_stages(data_dir: Path = DATADIR) -> pd.DataFrame:
    return _read(data_dir / "stages.csv", STAGE_COLS)


def load_parse(data_dir: Path = DATADIR) -> pd.DataFrame:
    return _read(data_dir / "parse.csv", PARS_COLS)


# ===========================================================================
# Helpers
# ===========================================================================
def _fmt_labels(dfs) -> list[str]:
    present = set()
    for df in dfs:
        present |= set(df["format"])
    return [f for f in FMT_ORDER if f in present]


def _sota_color(impl: str) -> str:
    return SOTA_COLOR.get(impl, SOTA_FALLBACK)


def _err(df):
    """Asymmetric (min, max) error bars as a 2xN array."""
    return np.vstack([df["p50"] - df["pmin"], df["pmax"] - df["p50"]]).astype(float)


def _grouped_bar(ax, long, group_order, series_order, color_of, logy=False):
    """Grouped bars.  ``color_of(series, group)`` returns a colour (or None to
    skip the bar).  ``long`` columns: group, series, p50, pmin, pmax."""
    x = np.arange(len(group_order))
    n = max(len(series_order), 1)
    w = 0.8 / n
    for i, sv in enumerate(series_order):
        sub = long[long["series"] == sv].set_index("group")
        for j, g in enumerate(group_order):
            if g not in sub.index:
                continue
            col = color_of(sv, g)
            if col is None:
                continue
            row = sub.loc[g]
            y = float(row["p50"])
            err = [[max(y - row["pmin"], 0)], [max(row["pmax"] - y, 0)]]
            ax.bar(
                x[j] + (i - (n - 1) / 2) * w,
                y,
                w,
                yerr=err,
                capsize=2,
                color=col,
                edgecolor="black",
                linewidth=0.4,
            )
    ax.set_xticks(x)
    ax.set_xticklabels([LABELS.get(g, g) for g in group_order])
    if logy:
        ax.set_yscale("log")


def _long_frame(pars, base, scale):
    rows = []
    for _, r in pars[pars.scale_mb == scale].iterrows():
        if r.variant in ("N1a", "N3c"):
            rows.append(
                dict(
                    group=r.format,
                    series=r.variant,
                    p50=r.p50,
                    pmin=r.pmin,
                    pmax=r.pmax,
                )
            )
    seen: set[str] = set()
    for _, r in base[base.scale_mb == scale].iterrows():
        if r.format in seen:  # one SOTA bar per format (first wins)
            continue
        seen.add(r.format)
        rows.append(
            dict(
                group=r.format,
                series="SOTA",
                p50=r.p50,
                pmin=r.pmin,
                pmax=r.pmax,
                impl=r.impl,
            )
        )
    return pd.DataFrame(rows)


def _impl_by_fmt(base, scale):
    out: dict[str, str] = {}
    for _, r in base[base.scale_mb == scale].iterrows():
        out.setdefault(r.format, r.impl)  # first baseline per format
    return out


# ===========================================================================
# Figures
# ===========================================================================
def figure_sota(pars, base, scale=None):
    """Device stage-1 vs chunked host vs domain SOTA, grouped by format (log y).

    Each format's SOTA bar uses that format's own SOTA colour."""
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch

    scale = scale or int(pars["scale_mb"].max())
    long = _long_frame(pars, base, scale)
    order = _fmt_labels([pars, base])
    order = [f for f in order if f in set(long["group"])]
    impl = _impl_by_fmt(base, scale)

    def color_of(series, fmt):
        if series in PARS_COLOR and series in set(long["series"]):
            return PARS_COLOR[series]
        if series == "SOTA":
            return None if fmt in NO_SOTA else _sota_color(impl.get(fmt, ""))
        return None

    color_of._uniform = True

    fig, ax = plt.subplots(figsize=FIGSIZE_WIDE, layout="constrained")
    _grouped_bar(ax, long, order, ["N1a", "N3c", "SOTA"], color_of, logy=True)
    ax.set_ylabel("GB/s (log scale)")
    ax.set_xlabel(f"format (input = {scale} MB)")

    handles = [Patch(color=PARS_COLOR["N1a"], label=PARS_LABEL["N1a"])]
    if (long["series"] == "N3c").any():
        handles.append(Patch(color=PARS_COLOR["N3c"], label=PARS_LABEL["N3c"]))
    for f in order:
        if f in NO_SOTA or f not in impl:
            continue
        handles.append(
            Patch(color=_sota_color(impl[f]), label=f"{impl[f]} ({LABELS.get(f, f)})")
        )
    fig.legend(handles=handles, ncols=4, fontsize="small", loc="outside lower center")
    return fig


def figure_scaling(pars, base):
    """Throughput vs input size, one facet per format, mean over repeats.

    SOTA lines are coloured per format (the same per-language colours as
    figure_sota)."""
    import matplotlib.pyplot as plt

    fmts = _fmt_labels([pars, base])[:6]
    ncol = 2
    nrow = (len(fmts) + ncol - 1) // ncol
    fig, axes = plt.subplots(
        nrow, ncol, figsize=FIGSIZE_WIDE, squeeze=False, layout="constrained"
    )

    def plot(ax, df, color, label, marker="o"):
        df = df.sort_values("scale_mb")
        if df.empty:
            return False
        ax.plot(
            df["scale_mb"],
            df["mean"],
            marker=marker,
            color=color,
            label=label,
            linewidth=1.2,
        )
        return True

    for ax, f in zip(axes.flat, fmts):
        plot(
            ax,
            pars[(pars.format == f) & (pars.variant == "N1a")],
            PARS_COLOR["N1a"],
            PARS_LABEL["N1a"],
        )
        plot(
            ax,
            pars[(pars.format == f) & (pars.variant == "N3c")],
            PARS_COLOR["N3c"],
            PARS_LABEL["N3c"],
            "s",
        )
        b = base[base.format == f]
        for impl in dict.fromkeys(b["impl"]):
            plot(ax, b[b.impl == impl], _sota_color(impl), impl, "^")
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xticks(sorted(set(pars["scale_mb"])))
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.minorticks_off()
        ax.tick_params(axis="x", labelsize="x-small", rotation=45)
        ax.set_title(LABELS.get(f, f), fontsize="medium")
        ax.set_xlabel("input (MB)")
        ax.set_ylabel("GB/s")
    for ax in axes.flat[len(fmts) :]:
        ax.axis("off")
    seen = {}
    for ax in axes.flat:
        for h, l in zip(*ax.get_legend_handles_labels()):
            seen.setdefault(l, h)
    fig.legend(list(seen.values()), list(seen.keys()), ncols=5, fontsize="small",
               loc="outside upper center")
    return fig


def figure_stages(stages, scale=None):
    """Stacked per-stage GPU time on the R1-R6 axis, by format."""
    import matplotlib.pyplot as plt

    scale = scale or int(stages["scale_mb"].max())
    st = stages[stages.scale_mb == scale].set_index("format")
    order = [f for f in FMT_ORDER if f in st.index]
    st = st.loc[order]
    fig, ax = plt.subplots(figsize=FIGSIZE, layout="constrained")
    bottom = np.zeros(len(order))
    for col in ("r1", "r2r4", "r3r5", "r6"):
        ax.bar(
            [LABELS.get(f, f) for f in order],
            st[col],
            bottom=bottom,
            label=STAGE_LABEL[col],
            color=STAGE_COLOR[col],
            edgecolor="black",
            linewidth=0.4,
        )
        bottom += st[col].to_numpy()
    ax.set_ylabel("time (ms)")
    ax.set_xlabel(f"format (input = {scale} MB)")
    fig.legend(ncols=4, fontsize="small", loc="outside lower center")
    return fig


def figure_parse(pars_tiers, parse_data, base, scale=None):
    """End-to-end comparison at the largest scale, matched per format.

    JSON is compared at the *structural* interval (pars N3c = H2D + R1-R6 + D2H)
    against cuJSON (whose standard-JSON total likewise includes D2H); TOML/INI/XML
    are compared as a *full parse* (pars P3 = GPU R1-R6 + host M+D) against the
    DOM parser of each format.
    """
    import matplotlib.pyplot as plt

    scale = scale or int(pars_tiers["scale_mb"].max())
    # (format, pars variant, which frame, baseline impl, kind label)
    specs = [
        ("json", "N3c", "tiers", "cujson", "structural"),
        ("toml", "P3", "parse", "toml++", "full parse"),
        ("ini", "P3", "parse", "inih", "full parse"),
        ("xml", "P3", "parse", "pugixml", "full parse"),
    ]
    pv, bv, bl, kl = [], [], [], []
    for f, var, src, bimpl, kind in specs:
        df = pars_tiers if src == "tiers" else parse_data
        sub = df[(df.format == f) & (df.variant == var) & (df.scale_mb == scale)]
        pv.append(float(sub["mean"].iloc[0]) if not sub.empty else np.nan)
        bs = base[(base.format == f) & (base.impl == bimpl) & (base.scale_mb == scale)]
        bv.append(float(bs["mean"].iloc[0]) if not bs.empty else np.nan)
        bl.append(bimpl)
        kl.append(kind)

    x = np.arange(len(specs))
    w = 0.38
    fig, ax = plt.subplots(figsize=FIGSIZE, layout="constrained")
    ax.bar(x - w / 2, pv, w, label="pars", color=PARS_COLOR["N1a"])
    ax.bar(x + w / 2, bv, w, label="baseline", color="#C44E52")
    for xi, (p, b, l) in enumerate(zip(pv, bv, bl)):
        ax.text(xi - w / 2, p, f"{p:.2f}", ha="center", va="bottom", fontsize="x-small")
        ax.text(xi + w / 2, b, f"{b:.2f} ({l})", rotation=90, ha="center", va="bottom",
                fontsize="x-small")
    ax.set_yscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels([f"{LABELS.get(s[0], s[0])}\n({s[4]})" for s in specs])
    ax.set_ylabel("GB/s (log scale)")
    ax.set_xlabel(f"input = {scale} MB")
    fig.legend(ncols=2, fontsize="small", loc="outside lower center")
    return fig


# fig_distribution was removed: it duplicated fig_sota (same grouped bars,
# only a linear y-axis).  Use fig_sota / fig_scaling instead.

