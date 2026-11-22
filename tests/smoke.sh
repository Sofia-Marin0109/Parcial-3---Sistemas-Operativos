#!/usr/bin/env bash
# Demonstrate enforcement on a fresh, owned test file.
set -euo pipefail

guard=${1:-build/mmap_guard}
probe=${2:-build/mmap_probe}
allowed_uid=${ALLOWED_UID:-61001}
denied_uid=${DENIED_UID:-61002}

if (( EUID != 0 )); then
    echo "Ejecuta la demostración como root en la VM de pruebas." >&2
    exit 1
fi
if [[ $allowed_uid == "$denied_uid" ]]; then
    echo "La demostración requiere dos UID distintos." >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/var/tmp}/mmap-guard.XXXXXX")
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

file=$work/protected.bin
control=$work/control.bin
log=$work/guard.log
chmod 0755 "$work"
truncate -s "$(getconf PAGESIZE)" "$file" "$control"
chmod 0666 "$file" "$control"

reset_file() { printf A | dd of="$file" bs=1 conv=notrunc status=none; }
first_byte() { head -c 1 -- "$file"; }

reset_file
"$probe" "$file" "$denied_uid" shared ok
[[ $(first_byte) == B ]]

reset_file
"$guard" "$file" "$allowed_uid" >"$log" 2>&1 &
daemon=$!
for (( attempt = 0; attempt < 100; ++attempt )); do
    if grep -qx READY "$log"; then
        break
    fi
    kill -0 "$daemon" 2>/dev/null || break
    sleep 0.05
done
if ! grep -qx READY "$log"; then
    cat -- "$log" >&2
    echo "El cargador no confirmó la activación." >&2
    exit 1
fi

"$probe" "$file" "$denied_uid" shared deny
"$probe" "$file" "$denied_uid" mprotect deny
"$probe" "$file" "$denied_uid" validate deny
"$probe" "$file" "$denied_uid" private ok
"$probe" "$file" "$allowed_uid" readonly dac
"$probe" "$control" "$denied_uid" shared ok
[[ $(head -c 1 -- "$control") == B ]]
[[ $(first_byte) == A ]]

# Release all workers from the same gate and check the entire file afterward.
before=$(sha256sum "$file")
workers=8
requests=16
gate=$work/start
mkfifo "$gate"
exec 9<>"$gate"
pids=()
for ((worker = 0; worker < workers; ++worker)); do
    (
        IFS= read -r -u 9 _
        for ((request = 0; request < requests; ++request)); do
            "$probe" "$file" "$denied_uid" shared deny
        done
    ) >"$work/worker-$worker.log" 2>&1 &
    pids+=("$!")
done
for ((worker = 0; worker < workers; ++worker)); do
    printf 'start\n' >&9
done
exec 9>&-
failed=0
for pid in "${pids[@]}"; do
    wait "$pid" || failed=1
done
if (( failed )); then
    cat "$work"/worker-*.log >&2
    exit 1
fi
[[ $(sha256sum "$file") == "$before" ]]
printf 'Concurrencia: %d solicitudes denegadas, SHA-256 intacto\n' "$((workers * requests))"

if [[ ${STRACE_CHECK:-0} == 1 ]]; then
    strace -qq -e trace=mmap,mprotect -o "$work/trace" \
        "$probe" "$file" "$denied_uid" mprotect deny
    grep -E 'mprotect\(.* = -1 EPERM' "$work/trace"
fi

"$probe" "$file" "$allowed_uid" shared ok
[[ $(first_byte) == B ]]

kill "$daemon"
wait "$daemon"
daemon=
sed -n '1,9p' "$log"
grep -Eq "event=mmap .*uid=$denied_uid .*decision=deny status=-1" "$log"
grep -Eq "event=mprotect .*uid=$denied_uid .*decision=deny status=-1" "$log"
grep -Eq "event=mmap .*uid=$allowed_uid .*decision=pass status=0" "$log"
[[ $(grep -Ec "event=mmap .*uid=$denied_uid .*decision=deny status=-1" "$log") == "$((workers * requests + 2))" ]]
grep -x 'audit_dropped=0' "$log"
reset_file
"$probe" "$file" "$denied_uid" shared ok
[[ $(first_byte) == B ]]
echo "SMOKE PASS"
