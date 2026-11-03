#!/bin/sh
# Uso: scripts/run_experiments.sh [saida.csv]      (saida em results/)
# Variaveis: REPS (5) WARMUP (2) MEASURE (10) BLOCK (65536) MAXN (10)
#            HOST (127.0.0.1) PORT (5000) MINN (1)
# Para cada N em MINN..MAXN: REPS vezes -> sobe um servidor novo, dispara
# N clientes simultaneos e registra o RESULT impresso pelo servidor.
cd "$(dirname "$0")/.." || exit 1
mkdir -p results

OUT=results/${1:-results.csv}
REPS=${REPS:-5}; WARMUP=${WARMUP:-2}; MEASURE=${MEASURE:-10}
BLOCK=${BLOCK:-65536}; MAXN=${MAXN:-10}; MINN=${MINN:-1}
HOST=${HOST:-127.0.0.1}; PORT=${PORT:-5000}

[ -f "$OUT" ] || echo "clients,rep,block,bytes,seconds,mbps" > "$OUT"

n=$MINN
while [ "$n" -le "$MAXN" ]; do
    rep=1
    while [ "$rep" -le "$REPS" ]; do
        bin/chat_server "$PORT" --bench "$n" "$WARMUP" "$MEASURE" > results/srv.log 2>&1 &
        SRV=$!
        sleep 0.3
        i=1; PIDS=""
        while [ "$i" -le "$n" ]; do
            bin/chat_client "$HOST" "$PORT" --bench "c$i" "$BLOCK" > /dev/null &
            PIDS="$PIDS $!"
            i=$((i + 1))
        done
        # espera os clientes (duracao = warmup + medicao + 1 s)
        wait $PIDS 2>/dev/null
        sleep 0.2
        kill "$SRV" 2>/dev/null; wait "$SRV" 2>/dev/null

        line=$(grep '^RESULT' results/srv.log)
        bytes=$(echo "$line" | sed 's/.*bytes=\([0-9]*\).*/\1/')
        secs=$(echo "$line"  | sed 's/.*seconds=\([0-9.]*\).*/\1/')
        mbps=$(echo "$line"  | sed 's/.*mbps=\([0-9.]*\).*/\1/')
        echo "$n,$rep,$BLOCK,$bytes,$secs,$mbps" >> "$OUT"
        echo "N=$n rep=$rep block=$BLOCK -> $mbps Mbps"
        rep=$((rep + 1))
    done
    n=$((n + 1))
done
