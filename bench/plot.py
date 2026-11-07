# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Render verified observations without rerunning or changing the experiment."""
import argparse
import json
import hashlib
from pathlib import Path
from importlib import import_module
import re

try:
    matplotlib = import_module("matplotlib")
    matplotlib.use("Agg")
    plt = import_module("matplotlib.pyplot")
    PdfPages = import_module("matplotlib.backends.backend_pdf").PdfPages
    LogNorm = import_module("matplotlib.colors").LogNorm
    np = import_module("numpy")
except ModuleNotFoundError as error:
    raise SystemExit(
        "Install optional Matplotlib/NumPy in a local plotting environment."
    ) from error

import experiment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = json.loads(args.summary.read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update(
        {"font.family": "DejaVu Sans", "font.size": 9, "svg.fonttype": "none", "pdf.fonttype": 42}
    )
    with PdfPages(args.output / "release-measurements.pdf") as pdf:

        def heatmap(name, title, subtitle, rows, columns, values, unit):
            figure, axis = plt.subplots(figsize=(12, max(5, len(rows) * 0.34 + 2.5)))
            values = np.asarray(values, dtype=float)
            if not np.isfinite(values).all() or (values <= 0).any():
                raise ValueError("Cannot render absent or nonpositive measurements as values")
            norm = LogNorm(vmin=min(values.min(), 1), vmax=max(values.max(), 1.01))
            image = axis.imshow(values, norm=norm, cmap="viridis_r", aspect="auto")
            axis.set_xticks(range(len(columns)), columns, rotation=20, ha="right")
            axis.set_yticks(range(len(rows)), rows)
            for y in range(len(rows)):
                for x in range(len(columns)):
                    color = "white" if norm(values[y, x]) > 0.55 else "#17212b"
                    axis.text(
                        x,
                        y,
                        f"{values[y,x]:.2f}",
                        ha="center",
                        va="center",
                        fontsize=8,
                        color=color,
                    )
            figure.colorbar(image, ax=axis, label=unit, pad=0.02, fraction=0.025)
            height = figure.get_figheight()
            figure.suptitle(title, fontsize=17, x=0.03, y=1 - 0.18 / height, ha="left", va="top")
            figure.text(0.03, 1 - 0.54 / height, subtitle, fontsize=9, ha="left", va="top")
            figure.tight_layout(rect=(0, 0, 1, 1 - 1.05 / height))
            figure.savefig(args.output / (name + ".png"), dpi=150)
            svg_path = args.output / (name + ".svg")
            figure.savefig(svg_path)
            svg = re.sub(
                r'id="([^"]+)"', lambda match: f'id="{name}-{match[1]}"', svg_path.read_text()
            )
            svg = re.sub(r"url\(#([^)]+)\)", lambda match: f"url(#{name}-{match[1]})", svg)
            svg = re.sub(
                r'xlink:href="#([^"]+)"', lambda match: f'xlink:href="#{name}-{match[1]}"', svg
            )
            svg_path.write_text(svg)
            pdf.savefig(figure)
            plt.close(figure)

        metrics = data["metrics"]
        for library in ("asio", "cobalt"):
            selected = [
                m
                for m in metrics
                if m["library"] == library
                and m["mode"] == "workload"
                and m["metric"] == "ns_per_operation"
            ]
            indexed = {
                (m["profile"], m["host"], m["workload"]): m["summary"]["median"] for m in selected
            }
            contexts = sorted({(m["profile"], m["host"]) for m in selected})
            ratios = []
            for profile, host in contexts:
                _, threads, eh = profile.split("-")
                baseline = "native-" + threads + "-" + eh
                ratios.append(
                    [
                        indexed[profile, host, workload] / indexed[baseline, "native", workload]
                        for workload in experiment.WORKLOADS
                    ]
                )
            heatmap(
                library + "-runtime-ratio",
                library.title() + " execution relative to native",
                "Median of 30 fresh executions; O3, no LTO. 1.00 = native; lower is faster.\nAll"
                " workloads initialize a timer/reactor. Native macOS kqueue polling affects these"
                " ratios; see diagnostic.",
                [profile + " / " + host for profile, host in contexts],
                experiment.WORKLOADS,
                ratios,
                "Ratio to native",
            )

        sizes = data["sizes"]
        options = [(opt, lto) for opt in ("O2", "O3", "Os", "Oz") for lto in ("off", "on")]
        profiles = sorted({r["profile"] for r in sizes})
        labels = [opt + (" + LTO" if lto == "on" else "") for opt, lto in options]
        for library in ("asio", "cobalt"):
            selected = [r for r in sizes if r["program"] == library + "_workload"]
            indexed = {
                (r["profile"], r["optimization"], r["lto"]): r["payload_bytes"] / 1024
                for r in selected
            }
            heatmap(
                library + "-payload",
                library.title() + " workload payload",
                "Release, uninstrumented. Native executable; WASI component; Emscripten JavaScript"
                " + Wasm. Raw KiB.",
                profiles,
                labels,
                [[indexed[p, opt, lto] for opt, lto in options] for p in profiles],
                "KiB",
            )
        indexed = {
            (r["profile"], r["optimization"], r["lto"]): r["build_seconds_six_programs"]
            for r in sizes
        }
        heatmap(
            "compilation",
            "Forced Release build of six programs",
            "One observation per setting; three compiler jobs, warm SDK cache. Not a timing"
            " distribution.",
            profiles,
            labels,
            [[indexed[p, opt, lto] for opt, lto in options] for p in profiles],
            "Seconds",
        )
    record = dict(
        matplotlib=matplotlib.__version__,
        numpy=np.__version__,
        summary_sha256=hashlib.sha256(args.summary.read_bytes()).hexdigest(),
        files={
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in args.output.iterdir()
            if path.suffix in (".svg", ".png", ".pdf")
        },
    )
    (args.output / "figures.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
