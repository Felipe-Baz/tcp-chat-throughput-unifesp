#!/bin/sh
# Experimento completo: N=1..10 (64 KiB) + varredura de blocos com N=10.
cd "$(dirname "$0")/.." || exit 1
sh scripts/build.sh || exit 1
REPS=5 sh scripts/run_experiments.sh results.csv
for b in 512 4096 16384 262144; do
  BLOCK=$b REPS=3 MINN=10 sh scripts/run_experiments.sh blocks.csv
done
echo ALLDONE
