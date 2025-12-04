import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path
import os

SAVE_FOLDER = Path("/home/os/pmu/advanced-os-pmu/plot")
os.makedirs(SAVE_FOLDER, exist_ok=True)


csv_path = Path("/home/os/pmu/advanced-os-pmu/results.csv")
if not csv_path.exists():
    raise FileNotFoundError("results.csv 파일이 현재 디렉토리에 없습니다.")

df = pd.read_csv(csv_path)

df.set_index("workload", inplace=True)


df["l1i_miss_rate"] = df["l1i_miss"] / df["l1i_ref"].replace(0, pd.NA)
df["l1d_miss_rate"] = df["l1d_miss"] / df["l1d_ref"].replace(0, pd.NA)
df["llc_miss_rate"] = df["llc_miss"] / df["l1d_ref"].replace(0, pd.NA)


df.fillna(0, inplace=True)


plt.figure(figsize=(10, 6))

# 막대 두 개를 나란히 그리기 위해 위치 조정
x = range(len(df.index))
width = 0.35

plt.bar([i - width/2 for i in x], df["instructions"], width=width, label="Instructions")
plt.bar([i + width/2 for i in x], df["cycles"], width=width, label="Cycles")

plt.xticks(ticks=x, labels=df.index, rotation=30, ha="right")
plt.ylabel("Count")
plt.title("Instructions vs Cycles per Workload")
plt.legend()
plt.tight_layout()
save_path = os.path.join(SAVE_FOLDER, "pmu_instructions_cycles.png")
plt.savefig(save_path, dpi=300)

# =========================
# 4. 캐시 미스 개수 비교 그래프
# =========================
plt.figure(figsize=(10, 6))

plt.bar(x, df["l1i_miss"], label="L1I Misses")
plt.bar(x, df["l1d_miss"], bottom=df["l1i_miss"], label="L1D Misses")  # stacked
plt.bar(x, df["llc_miss"], bottom=df["l1i_miss"] + df["l1d_miss"], label="LLC Misses")

plt.xticks(ticks=x, labels=df.index, rotation=30, ha="right")
plt.ylabel("Miss Count")
plt.title("Cache Misses (L1I / L1D / LLC) per Workload")
plt.legend()
plt.tight_layout()
save_path = os.path.join(SAVE_FOLDER, "pmu_cache_misses_stacked.png")
plt.savefig(save_path, dpi=300)

# =========================
# 5. miss rate 그래프 (L1I / L1D / LLC)
# =========================
plt.figure(figsize=(10, 6))

plt.plot(df.index, df["l1i_miss_rate"], marker="o", label="L1I Miss Rate")
plt.plot(df.index, df["l1d_miss_rate"], marker="o", label="L1D Miss Rate")
plt.plot(df.index, df["llc_miss_rate"], marker="o", label="LLC Miss Rate (per L1D ref)")

plt.xticks(rotation=30, ha="right")
plt.ylabel("Miss Rate")
plt.ylim(bottom=0)
plt.title("Cache Miss Rates per Workload")
plt.legend()
plt.tight_layout()
save_path = os.path.join(SAVE_FOLDER, "pmu_cache_miss_rates.png")
plt.savefig(save_path, dpi=300)

print("그래프 3개 생성 완료:")
print(" - pmu_instructions_cycles.png")
print(" - pmu_cache_misses_stacked.png")
print(" - pmu_cache_miss_rates.png")
