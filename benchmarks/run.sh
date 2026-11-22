#!/usr/bin/env bash
# Compare the same authorized load with the policy detached and attached.
set -euo pipefail

guard=${1:-build/mmap_guard}
bench=${2:-build/mmap_bench}
if (( EUID != 0 )); then
    echo "Ejecuta la medición como root en la VM de pruebas." >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/var/tmp}/mmap-bench.XXXXXX")
daemon=
cleanup() {
    if [[ -n $daemon ]]; then
        kill "$daemon" 2>/dev/null || true
        wait "$daemon" 2>/dev/null || true
    fi
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

protected=$work/protected.bin
control=$work/control.bin
log=$work/audit.log
dd if=/dev/zero of="$protected" bs=1M count=4 status=none
dd if=/dev/zero of="$control" bs=1M count=4 status=none

start_guard() {
    local condition=$1
    local -a options=()
    if [[ $condition == policy ]]; then
        options+=(--no-audit)
    fi
    "$guard" "$protected" "$EUID" "${options[@]}" >"$log" 2>&1 &
    daemon=$!
    local attempt
    for ((attempt = 0; attempt < 100; ++attempt)); do
        if grep -qx READY "$log"; then
            return 0
        fi
        kill -0 "$daemon" 2>/dev/null || break
        sleep 0.05
    done
    cat "$log" >&2
    return 1
}

printf 'round,condition,target,mmap_samples,mmap_p50_ns,mmap_p95_ns,touch_samples,pages_per_mapping,touch_total_ns,minor_faults,major_faults,audit_dropped\n'
orders=(
    'baseline policy audit'
    'baseline audit policy'
    'policy baseline audit'
    'policy audit baseline'
    'audit baseline policy'
    'audit policy baseline'
)
for ((round = 0; round < ${#orders[@]}; ++round)); do
    read -ra conditions <<< "${orders[round]}"
    targets=(protected control)
    if (( round % 2 )); then
        targets=(control protected)
    fi
    for condition in "${conditions[@]}"; do
        if [[ $condition != baseline ]]; then
            start_guard "$condition"
        fi
        declare -A measurements=()
        for target in "${targets[@]}"; do
            measurements[$target]=$("$bench" "${!target}")
        done
        dropped=0
        if [[ $condition != baseline ]]; then
            kill "$daemon"
            wait "$daemon"
            daemon=
            dropped=$(grep -xE 'audit_dropped=[0-9]+' "$log" | cut -d= -f2)
        fi
        for target in "${targets[@]}"; do
            printf '%d,%s,%s,%s,%s\n' "$((round + 1))" "$condition" "$target" "${measurements[$target]}" "$dropped"
        done
    done
done

if [[ ${PERF_CHECK:-0} == 1 ]]; then
    for condition in baseline policy; do
        if [[ $condition == policy ]]; then
            start_guard "$condition"
        fi
        perf stat -x, -e page-faults,minor-faults,major-faults \
            -o "$work/perf-stat" -- "$bench" "$protected" >/dev/null
        while IFS= read -r row; do
            [[ -z $row || $row == \#* ]] || printf 'PERF,%s,%s\n' "$condition" "$row" >&2
        done <"$work/perf-stat"
        if [[ $condition == policy ]]; then
            kill "$daemon"
            wait "$daemon"
            daemon=
        fi
    done
fi
