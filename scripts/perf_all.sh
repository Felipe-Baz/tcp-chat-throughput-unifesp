#!/bin/sh
# Perfilamento com perf dos cenarios do relatorio (Linux, requer root).
cd "$(dirname "$0")/.." || exit 1
sh scripts/build.sh || exit 1
rm -rf results/perf
sh scripts/perf_run.sh 1 65536 n01_64k
sh scripts/perf_run.sh 4 65536 n04_64k
sh scripts/perf_run.sh 6 65536 n06_64k
sh scripts/perf_run.sh 10 65536 n10_64k
sh scripts/perf_run.sh 10 512 n10_512
sh scripts/perf_run.sh 10 4096 n10_4k
sh scripts/perf_run.sh 10 262144 n10_256k
echo PERFDONE
