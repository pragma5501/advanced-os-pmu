import subprocess
import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path

RANDOM_BIN = "./bin/random_access_phases"
MATRIX_BIN = "./bin/matrix_phases"

PLOT_PATH = "/home/os/pmu/advanced-os-pmu/plot"

def run_program(cmd):
    print(f"Running: {' '.join(cmd)}")
    out = subprocess.check_output(cmd, text=True)
    print(out)
    return out


def parse_pmu_output(text):
    stats = {}
    current_label = None

    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i].strip()

        if line.startswith("==== PMU statistics for"):
            current_label = line.replace("==== PMU statistics for", "") \
                                .replace("====", "").strip()
            stats[current_label] = {}
            i += 1
            continue

        if current_label:
            if line.startswith("instructions"):
                stats[current_label]["instructions"] = int(line.split(":")[1])
            elif line.startswith("l1i_ref"):
                stats[current_label]["l1i_ref"] = int(line.split(":")[1])
            elif line.startswith("l1i_miss"):
                stats[current_label]["l1i_miss"] = int(line.split(":")[1])
            elif line.startswith("l1d_ref"):
                stats[current_label]["l1d_ref"] = int(line.split(":")[1])
            elif line.startswith("l1d_miss"):
                stats[current_label]["l1d_miss"] = int(line.split(":")[1])
            elif line.startswith("llc_miss"):
                stats[current_label]["llc_miss"] = int(line.split(":")[1])
            elif line.startswith("cycles"):
                stats[current_label]["cycles"] = int(line.split(":")[1])

        i += 1

    return stats


def stats_to_dataframe(stats_dict):
    df = pd.DataFrame.from_dict(stats_dict, orient="index")
    df.index.name = "phase"
    return df


def add_miss_rates(df):
    df = df.copy()
    df["l1i_miss_rate"] = df["l1i_miss"] / df["l1i_ref"].replace(0, pd.NA)
    df["l1d_miss_rate"] = df["l1d_miss"] / df["l1d_ref"].replace(0, pd.NA)
    df["llc_miss_rate"] = df["llc_miss"] / df["l1d_ref"].replace(0, pd.NA)
    df = df.fillna(0)
    return df


def plot_phase_workload(df, prefix):
    df = add_miss_rates(df)
    phases = list(df.index)
    x = range(len(phases))

    
    plt.figure(figsize=(8, 5))
    width = 0.35
    plt.bar([i - width/2 for i in x], df["instructions"], width=width, label="Instructions")
    plt.bar([i + width/2 for i in x], df["cycles"], width=width, label="Cycles")
    plt.xticks(x, phases, rotation=20, ha="right")
    plt.ylabel("Count")
    plt.title(f"{prefix.capitalize()} workload: Instructions vs Cycles")
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{PLOT_PATH}/{prefix}_instructions_cycles.png", dpi=300)

    
    plt.figure(figsize=(8, 5))
    plt.bar(x, df["l1i_miss"], label="L1I Misses")
    plt.bar(x, df["l1d_miss"], bottom=df["l1i_miss"], label="L1D Misses")
    plt.bar(x, df["llc_miss"], bottom=df["l1i_miss"] + df["l1d_miss"], label="LLC Misses")
    plt.xticks(x, phases, rotation=20, ha="right")
    plt.ylabel("Miss Count")
    plt.title(f"{prefix.capitalize()} workload: Cache Miss Counts")
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{PLOT_PATH}/{prefix}_cache_misses.png", dpi=300)

    
    plt.figure(figsize=(8, 5))
    plt.plot(phases, df["l1i_miss_rate"], marker="o", label="L1I Miss Rate")
    plt.plot(phases, df["l1d_miss_rate"], marker="o", label="L1D Miss Rate")
    plt.plot(phases, df["llc_miss_rate"], marker="o", label="LLC Miss Rate (per L1D ref)")
    plt.xticks(rotation=20, ha="right")
    plt.ylabel("Miss Rate")
    plt.ylim(bottom=0)
    plt.title(f"{prefix.capitalize()} workload: Cache Miss Rates")
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{PLOT_PATH}/{prefix}_cache_miss_rates.png", dpi=300)


def main():
    
    if not Path(RANDOM_BIN).exists():
        print(f"{RANDOM_BIN} not found. Compile random_access_phases.c first.")
    else:
        random_out = run_program([RANDOM_BIN])
        random_stats = parse_pmu_output(random_out)
        random_df = stats_to_dataframe(random_stats)
        random_df.to_csv("random_results.csv")
        print("\n[Random access] DataFrame:")
        print(random_df)
        plot_phase_workload(random_df, prefix="random")

    
    if not Path(MATRIX_BIN).exists():
        print(f"{MATRIX_BIN} not found. Compile matrix_phases.c first.")
    else:
        matrix_out = run_program([MATRIX_BIN])
        matrix_stats = parse_pmu_output(matrix_out)
        matrix_df = stats_to_dataframe(matrix_stats)
        matrix_df.to_csv("matrix_results.csv")
        print("\n[Matrix] DataFrame:")
        print(matrix_df)
        plot_phase_workload(matrix_df, prefix="matrix")

    print("\nDone. Generated:")
    print("  random_results.csv, matrix_results.csv")
    print("  random_instructions_cycles.png, random_cache_misses.png, random_cache_miss_rates.png")
    print("  matrix_instructions_cycles.png, matrix_cache_misses.png, matrix_cache_miss_rates.png")


if __name__ == "__main__":
    main()
