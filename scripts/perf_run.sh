#!/bin/sh
# Uso (como root, Linux): scripts/perf_run.sh <N> <bloco> <rotulo>
# Perfila o sistema inteiro durante a janela de medicao (aquec. 2 s,
# medicao 10 s): perf stat (3..7 s), uso de CPU via /proc/stat e
# perf record (7..10 s). Saidas em results/perf/<rotulo>.*
# PERF pode ser definido para apontar o binario do perf.
cd "$(dirname "$0")/.." || exit 1
P=${PERF:-perf}
N=$1; BLOCK=$2; L=$3; PORT=5100
D=results/perf
mkdir -p $D
echo -1 > /proc/sys/kernel/perf_event_paranoid

cpu() { awk '/^cpu /{print $2,$3,$4,$5,$6,$7,$8,$9}' /proc/stat; }

bin/chat_server $PORT --bench "$N" 2 10 > $D/$L.srv 2>&1 &
SRV=$!
sleep 0.3
PIDS=""
i=1
while [ "$i" -le "$N" ]; do
    bin/chat_client 127.0.0.1 $PORT --bench c$i "$BLOCK" > /dev/null &
    PIDS="$PIDS $!"
    i=$((i + 1))
done

sleep 3
A=$(cpu)
$P stat -a -e task-clock,context-switches,cpu-migrations,cycles,instructions,cache-misses,cache-references \
    -o $D/$L.stat -- sleep 4
B=$(cpu)
echo "$A" > $D/$L.cpuA; echo "$B" > $D/$L.cpuB
$P record -F 999 -a -g -o $D/$L.data -- sleep 3 > /dev/null 2>&1
$P report -f -i $D/$L.data --no-children --sort comm,dso,sym --stdio -g none \
    --percent-limit 0.3 > $D/$L.report 2> $D/$L.rer
$P report -f -i $D/$L.data --no-children --sort comm --stdio -g none > $D/$L.comm 2>/dev/null

wait $PIDS 2>/dev/null
sleep 0.2
kill $SRV 2>/dev/null; wait $SRV 2>/dev/null

# user nice system idle iowait irq softirq steal -> percentuais
echo "$A $B" | awk '{
  for(i=1;i<=8;i++) d[i]=$(i+8)-$i; t=0; for(i=1;i<=8;i++) t+=d[i];
  printf "cpu%%: user=%.1f sys=%.1f softirq=%.1f idle=%.1f steal=%.1f\n",
    (d[1]+d[2])/t*100, d[3]/t*100, d[7]/t*100, d[4]/t*100, d[8]/t*100 }' > $D/$L.cpu
echo "== $L ==" ; cat $D/$L.cpu; grep RESULT $D/$L.srv
