#!/usr/bin/env python3
"""Generate README performance figures as static PNG images."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def apply_style() -> None:
    plt.rcParams.update(
        {
            "font.size": 11,
            "axes.titlesize": 13,
            "axes.labelsize": 11,
            "axes.edgecolor": "#57606a",
            "axes.linewidth": 1.0,
            "axes.grid": True,
            "grid.color": "#d0d7de",
            "grid.linestyle": "-",
            "grid.alpha": 0.75,
            "figure.facecolor": "#f6f8fa",
            "savefig.facecolor": "#f6f8fa",
            "axes.facecolor": "#ffffff",
            "legend.frameon": True,
            "legend.facecolor": "#ffffff",
            "legend.edgecolor": "#d0d7de",
        }
    )


def generate_core_relative(output_dir: Path) -> None:
    labels = [
        "idle_wait",
        "echo_64",
        "echo_1024",
        "echo_16384",
        "churn_16",
        "churn_32",
        "churn_64",
    ]
    # libsimplenet/boost ratio derived from README's median comparison table.
    values = np.array([1.14, 1.01, 1.00, 1.01, 1.12, 1.13, 1.14])

    fig, ax = plt.subplots(figsize=(11.8, 4.8))
    bars = ax.bar(labels, values, color="#0969da", edgecolor="#1f2328", linewidth=0.6)
    ax.axhline(1.0, color="#cf222e", linestyle="--", linewidth=1.4, label="Parity (1.00)")
    ax.set_ylim(0.95, 1.18)
    ax.set_ylabel("Speed Ratio (higher favors libsimplenet)")
    ax.set_title("Relative Speed From Median Runs (libsimplenet / Boost.Asio)")
    ax.legend(loc="upper left")
    ax.tick_params(axis="x", rotation=20)
    for bar, value in zip(bars, values, strict=True):
        ax.text(
            bar.get_x() + bar.get_width() / 2.0,
            value + 0.004,
            f"{value:.2f}",
            ha="center",
            va="bottom",
            fontsize=10,
            color="#1f2328",
        )
    fig.tight_layout()
    fig.savefig(output_dir / "perf-core-relative-speed.png", dpi=180)
    plt.close(fig)


def generate_async_medians(output_dir: Path) -> None:
    payload_labels = ["64B", "1KiB", "16KiB"]
    libs_epoll = np.array([82.739, 85.042, 109.326])
    libs_uring = np.array([83.655, 81.731, 108.340])
    boost_epoll = np.array([76.742, 74.517, 107.950])

    x = np.arange(len(payload_labels))
    width = 0.24

    fig, ax = plt.subplots(figsize=(9.6, 5.2))
    ax.bar(
        x - width,
        libs_epoll,
        width,
        label="libsimplenet (epoll)",
        color="#0969da",
        edgecolor="#1f2328",
        linewidth=0.6,
    )
    ax.bar(
        x,
        libs_uring,
        width,
        label="libsimplenet (io_uring)",
        color="#2da44e",
        edgecolor="#1f2328",
        linewidth=0.6,
    )
    ax.bar(
        x + width,
        boost_epoll,
        width,
        label="Boost.Asio (epoll)",
        color="#8250df",
        edgecolor="#1f2328",
        linewidth=0.6,
    )

    ax.set_xticks(x)
    ax.set_xticklabels(payload_labels)
    ax.set_ylabel("Median total time (ms, lower is better)")
    ax.set_title("Async Echo Median Total Time")
    ax.legend(loc="upper left")
    fig.tight_layout()
    fig.savefig(output_dir / "perf-async-echo-medians-ms.png", dpi=180)
    plt.close(fig)


def generate_async_paired_ratio(output_dir: Path) -> None:
    payload_labels = ["64B", "1KiB", "16KiB"]
    ratio_epoll = np.array([0.897905, 0.884594, 0.969676])
    ratio_uring = np.array([0.935181, 0.925209, 1.002933])
    x = np.arange(len(payload_labels))

    fig, ax = plt.subplots(figsize=(9.6, 4.8))
    ax.plot(
        x,
        ratio_epoll,
        marker="o",
        linewidth=2.1,
        color="#0969da",
        label="boost_over_libs(epoll)",
    )
    ax.plot(
        x,
        ratio_uring,
        marker="o",
        linewidth=2.1,
        color="#2da44e",
        label="boost_over_libs(io_uring)",
    )
    ax.axhline(1.0, color="#cf222e", linestyle="--", linewidth=1.4, label="Parity (1.00)")
    ax.set_xticks(x)
    ax.set_xticklabels(payload_labels)
    ax.set_ylim(0.87, 1.03)
    ax.set_ylabel("Paired ratio (higher means smaller gap)")
    ax.set_title("Async Paired Ratio (boost_over_libs)")
    ax.legend(loc="lower right")
    for idx, value in enumerate(ratio_epoll):
        ax.text(idx, value - 0.008, f"{value:.3f}", ha="center", va="top", color="#1f2328")
    for idx, value in enumerate(ratio_uring):
        ax.text(idx, value + 0.004, f"{value:.3f}", ha="center", va="bottom", color="#1f2328")
    fig.tight_layout()
    fig.savefig(output_dir / "perf-async-paired-ratio.png", dpi=180)
    plt.close(fig)


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    output_dir = repo_root / "docs" / "usage" / "figures"
    output_dir.mkdir(parents=True, exist_ok=True)

    apply_style()
    generate_core_relative(output_dir)
    generate_async_medians(output_dir)
    generate_async_paired_ratio(output_dir)


if __name__ == "__main__":
    main()
