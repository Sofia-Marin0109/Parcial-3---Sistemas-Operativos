#!/usr/bin/env bash
# Check the policy boundary on a file created by this test.
set -euo pipefail

guard=${1:-build/mmap_guard}
probe=${2:-build/mmap_probe}
allowed_uid=${ALLOWED_UID:-61001}
denied_uid=${DENIED_UID:-61002}

if (( EUID != 0 )) || [[ $allowed_uid == "$denied_uid" ]]; then
    echo "Ejecuta la prueba como root en la VM y usa dos UID distintos." >&2
    exit 1
fi

work=$(mktemp -d "${TMPDIR:-/var/tmp}/mmap-scope.XXXXXX")
daemon=
holder=
cleanup() {
    if [[ -n $holder ]]; then
        kill "$holder" 2>/dev/null || true
        wait "$holder" 2>/dev/null || true
    fi
    if [[ -n $daemon ]]; then
        kill "$daemon" 2>/dev/null || true
        wait "$daemon" 2>/dev/null || true
    fi
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

await_ready() {
    local pid=$1 marker=$2 log=$3 attempt
    for ((attempt = 0; attempt < 100; ++attempt)); do
        if grep -qx "$marker" "$log"; then
            return 0
        fi
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.05
    done
    cat "$log" >&2
    echo "No apareció $marker." >&2
    return 1
}

file=$work/protected.bin
gate=$work/gate
holder_log=$work/holder.log
guard_log=$work/guard.log
chmod 0755 "$work"
truncate -s "$(getconf PAGESIZE)" "$file"
chmod 0666 "$file"
printf A | dd of="$file" bs=1 conv=notrunc status=none
mkfifo "$gate"
exec 9<>"$gate"
first_byte() { head -c 1 -- "$file"; }

# Create the writable region before the guard starts. It waits before writing.
"$probe" "$file" "$denied_uid" existing ok <&9 9>&- >"$holder_log" 2>&1 &
holder=$!
await_ready "$holder" MAPPING_READY "$holder_log"
cat "$holder_log"
[[ $(first_byte) == A ]]
echo 'Antes de activar: mapeo listo, byte A'

"$guard" "$file" "$allowed_uid" 9>&- >"$guard_log" 2>&1 &
daemon=$!
await_ready "$daemon" READY "$guard_log"

"$probe" "$file" "$denied_uid" shared deny
[[ $(first_byte) == A ]]
echo 'Política activa: mapeo nuevo denegado, byte A'

"$probe" "$file" "$denied_uid" write ok
[[ $(first_byte) == D ]]
echo 'Política activa: write() directo, byte D'

printf W >&9
exec 9>&-
wait "$holder"
holder=
[[ $(first_byte) == E ]]
echo 'Política activa: escritura con el mapeo previo, byte E'

kill -TERM "$daemon"
wait "$daemon"
daemon=
cat "$guard_log"
[[ $(grep -c '^event=' "$guard_log") == 1 ]]
grep -Eq "event=mmap .*uid=$denied_uid .*decision=deny status=-1" "$guard_log"
grep -qx 'audit_dropped=0' "$guard_log"
echo 'SCOPE PASS'
