import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

workers = [1, 2, 4, 8]

# BFS uses sequential baseline (not normalized to parallel N=1)
data = {
    "Fibonacci 7.67x":  ([1.00, 1.97, 3.88, 7.67], "#9b59b6", "-"),
    "N-Queens 7.22x":   ([1.00, 1.99, 3.86, 7.22], "#2ecc71", "-"),
    "Heat2 7.21x":      ([1.00, 1.97, 3.85, 7.21], "#f39c12", "-"),
    "Imbalanced 7.26x": ([1.00, 1.96, 3.73, 7.26], "#5b9bd5", "-"),
    "Mergesort 4.84x":  ([1.00, 1.92, 3.23, 4.84], "#1abc9c", "-"),
    "BFS 2.13x":        ([0.54, 0.87, 1.42, 2.13], "#e74c3c", "-"),
    "Ideal":            ([1,    2,    4,    8   ],  "#aaaaaa", "--"),
}

plt.style.use('dark_background')
fig, ax = plt.subplots(figsize=(9, 5.5))
fig.patch.set_facecolor('#1e1e1e')
ax.set_facecolor('#1e1e1e')

for label, (speedups, color, ls) in data.items():
    ax.plot(workers, speedups, color=color, linestyle=ls,
            linewidth=2.2, marker='o', markersize=5, label=label)

ax.set_xlabel("workers (N)", fontsize=11, color='white')
ax.set_ylabel("speedup", fontsize=11, color='white')
ax.set_xticks(workers)
ax.set_yticks(range(0, 10))
ax.set_xlim(0.7, 8.5)
ax.set_ylim(0, 9)
ax.tick_params(colors='white')
for spine in ax.spines.values():
    spine.set_edgecolor('#444444')
ax.grid(True, linestyle=':', color='#444444', alpha=0.6)

ax.legend(loc='upper left', fontsize=9.5, framealpha=0.15,
          labelcolor='white', edgecolor='#555555')

plt.tight_layout()
plt.savefig("speedup_ghc.png", dpi=180, facecolor=fig.get_facecolor())
print("Saved speedup_ghc.png")
