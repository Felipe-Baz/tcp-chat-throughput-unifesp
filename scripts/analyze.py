#!/usr/bin/env python3
"""Gera tabelas (Markdown) e graficos a partir de results/results.csv e results/blocks.csv; grava figures/*.png e results/tables.md."""
import csv, os, statistics as st, sys
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
RES = os.path.join(ROOT, "results")
FIG = os.path.join(ROOT, "figures")
os.makedirs(FIG, exist_ok=True)


def load(path):
    rows = defaultdict(list)
    with open(path) as f:
        for r in csv.DictReader(f):
            rows[(int(r["clients"]), int(r["block"]))].append(float(r["mbps"]))
    return rows


def stats(v):
    return (st.mean(v), st.stdev(v) if len(v) > 1 else 0.0, min(v), max(v))


main = load(os.path.join(RES, "results.csv"))
ns = sorted({k[0] for k in main})
out = ["| Clientes | Reps | Média (Mbps) | Desvio | Mín | Máx (Mbps) | Média (Gbps) | Por cliente (Gbps) |",
       "|---|---|---|---|---|---|---|---|"]
means, sds, maxs = [], [], []
for n in ns:
    v = main[(n, 65536)]
    m, s, lo, hi = stats(v)
    means.append(m); sds.append(s); maxs.append(hi)
    out.append(f"| {n} | {len(v)} | {m:,.0f} | {s:,.0f} | {lo:,.0f} | {hi:,.0f} | {m/1e3:.2f} | {m/n/1e3:.2f} |")
tab1 = "\n".join(out)

plt.figure(figsize=(7, 4))
plt.errorbar(ns, [m / 1e3 for m in means], yerr=[s / 1e3 for s in sds],
             marker="o", capsize=3, label="média ± desvio")
plt.plot(ns, [x / 1e3 for x in maxs], "--", marker="^", alpha=.6, label="máximo")
plt.plot(ns, [means[0] * n / 1e3 for n in ns], ":", color="gray", label="escala linear (ideal)")
plt.xlabel("Clientes simultâneos"); plt.ylabel("Throughput agregado (Gbps)")
plt.xticks(ns); plt.grid(alpha=.3); plt.legend(); plt.tight_layout()
plt.savefig(os.path.join(FIG, "throughput.png"), dpi=150)

plt.figure(figsize=(7, 4))
plt.bar(ns, [m / n / 1e3 for m, n in zip(means, ns)])
plt.xlabel("Clientes simultâneos"); plt.ylabel("Throughput por cliente (Gbps)")
plt.xticks(ns); plt.grid(alpha=.3, axis="y"); plt.tight_layout()
plt.savefig(os.path.join(FIG, "per_client.png"), dpi=150)

tab2 = ""
try:
    blk = load(os.path.join(RES, "blocks.csv"))
    blk[(10, 65536)] = main[(10, 65536)]
    sizes = sorted({k[1] for k in blk})
    o = ["| Bloco (bytes) | Reps | Média (Mbps) | Desvio | Máx (Mbps) |", "|---|---|---|---|---|"]
    bm = []
    for b in sizes:
        v = blk[(10, b)]
        m, s, lo, hi = stats(v)
        bm.append(m)
        o.append(f"| {b} | {len(v)} | {m:,.0f} | {s:,.0f} | {hi:,.0f} |")
    tab2 = "\n".join(o)
    plt.figure(figsize=(7, 4))
    plt.semilogx(sizes, [m / 1e3 for m in bm], marker="o")
    plt.xlabel("Tamanho do bloco (bytes)"); plt.ylabel("Throughput agregado, 10 clientes (Gbps)")
    plt.grid(alpha=.3, which="both"); plt.tight_layout()
    plt.savefig(os.path.join(FIG, "blocks.png"), dpi=150)
except FileNotFoundError:
    pass

open(os.path.join(RES, "tables.md"), "w", encoding="utf-8").write(
    "## Throughput x clientes (bloco 64 KiB)\n\n" + tab1 +
    "\n\n## Throughput x tamanho de bloco (10 clientes)\n\n" + tab2 + "\n")
print(open(os.path.join(RES, "tables.md"), encoding="utf-8").read())
