from pathlib import Path
import sys

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


INPUT_FILE = "u101a_4v_trim_1k_9k.txt"
OUTPUT_CSV = "u101a_transfer_lut.csv"
OUTPUT_PLOT = "u101a_transfer_plot.png"
SETTLING_TIME_SECONDS = 0.005
LUT_SIZE = 2048

REQUIRED_COLUMNS = ("time", "V(in_stage)", "V(op_out)", "V(trim_out)")


def read_ltspice_export(path: Path) -> pd.DataFrame:
    if not path.exists():
        raise FileNotFoundError(f"Input file not found: {path}")

    data = pd.read_csv(path, sep="\t")
    missing_columns = [col for col in REQUIRED_COLUMNS if col not in data.columns]
    if missing_columns:
        available = ", ".join(data.columns)
        missing = ", ".join(missing_columns)
        raise ValueError(
            f"Missing required column(s): {missing}. Available columns: {available}"
        )

    return data


def build_lut(data: pd.DataFrame) -> tuple[pd.DataFrame, pd.DataFrame]:
    settled = data.loc[data["time"] >= SETTLING_TIME_SECONDS].copy()
    if settled.empty:
        raise ValueError("No rows remain after removing the first 5ms of data.")

    input_voltage = settled["V(in_stage)"].to_numpy(dtype=float) - 5.0
    output_voltage = settled["V(trim_out)"].to_numpy(dtype=float)

    max_output_abs = np.max(np.abs(output_voltage))
    if not np.isfinite(max_output_abs) or max_output_abs == 0.0:
        raise ValueError("Cannot normalize output: max(abs(V(trim_out))) is zero.")

    normalized = pd.DataFrame(
        {
            "input_norm": input_voltage / 4.0,
            "output_norm": output_voltage / max_output_abs,
        }
    )

    normalized = normalized.loc[
        normalized["input_norm"].between(-1.0, 1.0)
    ].copy()
    if normalized.empty:
        raise ValueError("No rows remain after filtering input_norm to [-1.0, 1.0].")

    normalized = normalized.sort_values("input_norm").reset_index(drop=True)

    bin_edges = np.linspace(-1.0, 1.0, LUT_SIZE + 1)
    bin_centers = (bin_edges[:-1] + bin_edges[1:]) * 0.5
    bin_index = np.digitize(normalized["input_norm"], bin_edges) - 1
    bin_index = np.clip(bin_index, 0, LUT_SIZE - 1)

    binned = (
        pd.DataFrame(
            {
                "bin": bin_index,
                "input_norm": normalized["input_norm"],
                "output_norm": normalized["output_norm"],
            }
        )
        .groupby("bin", as_index=False)
        .agg(input_norm=("input_norm", "mean"), output_norm=("output_norm", "mean"))
        .sort_values("input_norm")
    )

    if len(binned) < 2:
        raise ValueError("Not enough unique input bins to interpolate a LUT.")

    lut_x = np.linspace(-1.0, 1.0, LUT_SIZE)
    lut_y = np.interp(
        lut_x,
        binned["input_norm"].to_numpy(dtype=float),
        binned["output_norm"].to_numpy(dtype=float),
    )

    lut = pd.DataFrame({"input_norm": lut_x, "output_norm": lut_y})
    return normalized, lut


def save_plot(normalized: pd.DataFrame, lut: pd.DataFrame, path: Path) -> None:
    fig, ax = plt.subplots(figsize=(9, 6), dpi=140)
    ax.scatter(
        normalized["input_norm"],
        normalized["output_norm"],
        s=4,
        alpha=0.18,
        linewidths=0,
        label="Settled LTspice samples",
    )
    ax.plot(
        lut["input_norm"],
        lut["output_norm"],
        color="#d62728",
        linewidth=1.6,
        label=f"{LUT_SIZE}-point LUT",
    )
    ax.set_xlabel("input_norm")
    ax.set_ylabel("output_norm")
    ax.set_title("U101A Transfer LUT")
    ax.grid(True, alpha=0.28)
    ax.legend(loc="best")
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def main() -> int:
    base_dir = Path(__file__).resolve().parent
    input_path = base_dir / INPUT_FILE
    csv_path = base_dir / OUTPUT_CSV
    plot_path = base_dir / OUTPUT_PLOT

    try:
        data = read_ltspice_export(input_path)
        settled_rows = int((data["time"] >= SETTLING_TIME_SECONDS).sum())
        normalized, lut = build_lut(data)

        lut.to_csv(csv_path, index=False)
        save_plot(normalized, lut, plot_path)

        print(f"Rows read: {len(data)}")
        print(f"Rows used after 5ms: {settled_rows}")
        print(
            "input_norm min/max: "
            f"{normalized['input_norm'].min():.9g}, {normalized['input_norm'].max():.9g}"
        )
        print(
            "output_norm min/max: "
            f"{normalized['output_norm'].min():.9g}, {normalized['output_norm'].max():.9g}"
        )
        print(f"Saved CSV: {csv_path}")
        print(f"Saved plot: {plot_path}")
        return 0
    except (FileNotFoundError, ValueError, pd.errors.ParserError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
