#!/bin/bash
# Smoke-test mfi-cli write paths against a fake proc/etc tree.
# Asserts relay and LED writes actually mutate the backing files.
# Usage: bash tests/cli_smoke.sh /path/to/mfi-cli
set -u
BIN="${1:?usage: cli_smoke.sh <mfi-cli-binary>}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

mkdir -p etc proc/power proc/led
cat > etc/board.info <<'EOF'
board.name=Smoke Board
board.shortname=smoke
board.sysid=0xe648
EOF
echo "MF.smoke" > etc/version
for i in 1 2 3 4 5 6 7 8; do
  echo "0.0" > "proc/power/active_pwr$i"
  echo "0.0" > "proc/power/i_rms$i"
  echo "120.0" > "proc/power/v_rms$i"
  echo "1.0" > "proc/power/pf$i"
  echo "0" > "proc/power/relay$i"
done
echo "0 0" > proc/led/status
echo "0" > proc/led/freq

fail() { echo "FAIL: $1"; exit 1; }

# relay set on
"$BIN" relay set 3 on >/dev/null 2>&1 || fail "relay set exit"
[ "$(cat proc/power/relay3)" = "1" ] || fail "relay set on did not write 1"

# relay off
"$BIN" relay off 3 >/dev/null 2>&1 || fail "relay off exit"
[ "$(cat proc/power/relay3)" = "0" ] || fail "relay off did not write 0"

# relay toggle (0 -> 1)
"$BIN" relay toggle 3 >/dev/null 2>&1 || fail "relay toggle exit"
[ "$(cat proc/power/relay3)" = "1" ] || fail "relay toggle did not write 1"

# relay get reflects state
[ "$("$BIN" relay get 3 2>/dev/null)" = "On" ] || fail "relay get not On"

# bad id -> nonzero
if "$BIN" relay get 99 >/dev/null 2>&1; then fail "bad id should be nonzero"; fi

# led set color both -> code 3
"$BIN" led set --color both >/dev/null 2>&1 || fail "led set color exit"
[ "$(cat proc/led/status)" = "3" ] || fail "led color did not write 3"

# led set frequency
"$BIN" led set --frequency 7 >/dev/null 2>&1 || fail "led set freq exit"
[ "$(cat proc/led/freq)" = "7" ] || fail "led freq did not write 7"

# led set with no options -> nonzero
if "$BIN" led set >/dev/null 2>&1; then fail "led set no-opts should be nonzero"; fi

# config get
[ "$("$BIN" config get etc/board.info board.shortname 2>/dev/null)" = "smoke" ] \
  || fail "config get wrong value"

# config get missing key with default
[ "$("$BIN" config get etc/board.info missing --default d 2>/dev/null)" = "d" ] \
  || fail "config get default not returned"

echo "PASS: all mfi-cli smoke checks"
