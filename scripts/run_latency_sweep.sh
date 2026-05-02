#!/usr/bin/env bash
set -euo pipefail

ENGINE=./engine/build/engine
OUT=docs/latency_results.csv
SEED=42
DURATION=60

mkdir -p docs

echo "run_id,lp_to_pe_latency_us,lt_to_pe_latency_us,pe_to_book_latency_us,run_set,seed,duration_s,pnl_realised,pnl_unrealised,pnl_total,fill_rate,fill_count,spread_capture_mean,position_std,position_max_abs,hedge_count,latency_p99_ns" > "$OUT"

run_id=0

run_engine() {
    local lp=$1 lt=$2 pe=$3 run_set=$4
    local output
    output=$("$ENGINE" \
        --lp-to-pe-latency-us "$lp" \
        --lt-to-pe-latency-us "$lt" \
        --pe-to-book-latency-us "$pe" \
        --seed "$SEED" \
        --duration "$DURATION" 2>/dev/null)

    local pnl_r pnl_u fill_count spread_mean pos_std pos_max hedge lat_p99
    pnl_r=$(echo "$output"      | grep "Realised PnL"     | awk '{print $4}')
    pnl_u=$(echo "$output"      | grep "Unrealised PnL"   | awk '{print $4}')
    fill_count=$(echo "$output" | grep "Fill count (PE)"  | awk '{print $5}')
    spread_mean=$(echo "$output"| grep "Spread cap mean"  | awk '{print $5}')
    pos_std=$(echo "$output"    | grep "Position std"     | awk '{print $4}')
    pos_max=$(echo "$output"    | grep "Peak abs position"| awk '{print $5}')
    hedge=$(echo "$output"      | grep "Hedge count"      | awk '{print $4}')
    lat_p99=$(echo "$output"    | grep "Latency p99"      | awk '{print $4}')

    local pnl_total fill_rate
    pnl_total=$(echo "$pnl_r $pnl_u" | awk '{printf "%.4f", $1 + $2}')
    fill_rate=$(echo "$fill_count $DURATION" | awk '{printf "%.4f", $1 / $2}')

    echo "$run_id,$lp,$lt,$pe,$run_set,$SEED,$DURATION,$pnl_r,$pnl_u,$pnl_total,$fill_rate,$fill_count,$spread_mean,$pos_std,$pos_max,$hedge,$lat_p99" >> "$OUT"
    echo "  run $run_id [$run_set] lp=${lp}us lt=${lt}us pe=${pe}us -> pnl=$pnl_total fills=$fill_count"
    run_id=$((run_id + 1))
}

LATENCIES=(100 500 1000 5000 10000)

echo "==> Baseline"
run_engine 0 0 0 baseline

echo "==> LP-to-PE only"
for v in "${LATENCIES[@]}"; do run_engine "$v" 0 0 lp_only; done

echo "==> LT-to-PE only"
for v in "${LATENCIES[@]}"; do run_engine 0 "$v" 0 lt_only; done

echo "==> PE-to-book only"
for v in "${LATENCIES[@]}"; do run_engine 0 0 "$v" pe_only; done

echo "==> Combined"
for v in "${LATENCIES[@]}"; do run_engine "$v" "$v" "$v" combined; done

echo "==> Done. Results in $OUT"