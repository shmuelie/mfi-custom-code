#!/bin/bash
# Integration smoke test for mfi-rest-server (civetweb).
# Starts the server against a fake proc/etc tree, exercises every endpoint,
# and asserts status codes plus that relay/led POSTs mutate the backing files.
# Usage: bash tests/rest_smoke.sh /path/to/mfi-rest-server
set -u
BIN="${1:?usage: rest_smoke.sh <mfi-rest-server-binary>}"
command -v curl >/dev/null 2>&1 || { echo "SKIP: curl not available"; exit 0; }

WORK="$(mktemp -d)"
PORT=8791
SRV_PID=""
cleanup() {
	[ -n "$SRV_PID" ] && [ -d "/proc/$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null
	rm -rf "$WORK"
}
trap cleanup EXIT
cd "$WORK"

mkdir -p etc proc/power proc/led
cat > etc/board.info <<'EOF'
board.name=Smoke Board
board.shortname=smoke
board.sysid=0xe648
EOF
echo "MF.smoke" > etc/version
for i in 1 2 3 4 5 6 7 8; do
  echo "10.0" > "proc/power/active_pwr$i"
  echo "0.1"  > "proc/power/i_rms$i"
  echo "120.0" > "proc/power/v_rms$i"
  echo "1.0" > "proc/power/pf$i"
  echo "0" > "proc/power/relay$i"
done
echo "0 0" > proc/led/status
echo "0" > proc/led/freq

"$BIN" -i 127.0.0.1 -p "$PORT" >server.log 2>&1 &
SRV_PID=$!
# wait for the port to accept connections
for _ in $(seq 1 20); do
	if curl -s -o /dev/null "http://127.0.0.1:$PORT/api/v2/info"; then break; fi
	sleep 0.2
done

BASE="http://127.0.0.1:$PORT/api/v2"
fail() { echo "FAIL: $1"; echo "--- server.log ---"; cat server.log; exit 1; }
code() { curl -s -o /dev/null -w "%{http_code}" "$@"; }

# info
[ "$(code $BASE/info)" = "200" ] || fail "info status"
curl -s $BASE/info | grep -q '"sensorCount":8' || fail "info sensorCount"

# sensor list
[ "$(code $BASE/sensor)" = "200" ] || fail "sensor list status"
curl -s $BASE/sensor | grep -q '"power"' || fail "sensor list body"

# sensor item
[ "$(code $BASE/sensor/3)" = "200" ] || fail "sensor item status"

# relay on/off via POST -> assert proc file changes
[ "$(code -X POST -d on $BASE/sensor/3)" = "200" ] || fail "relay on status"
[ "$(cat proc/power/relay3)" = "1" ] || fail "relay on did not write 1"
[ "$(code -X POST -d off $BASE/sensor/3)" = "200" ] || fail "relay off status"
[ "$(cat proc/power/relay3)" = "0" ] || fail "relay off did not write 0"
[ "$(code -X POST -d maybe $BASE/sensor/3)" = "400" ] || fail "bad relay body should be 400"

# sensor out of range / unknown
[ "$(code $BASE/sensor/99)" = "404" ] || fail "sensor 99 should be 404"
[ "$(code $BASE/nope)" = "404" ] || fail "unknown path should be 404"

# led get/post
[ "$(code $BASE/led)" = "200" ] || fail "led get status"
[ "$(code -X POST -d '{"color":3}' $BASE/led)" = "200" ] || fail "led color status"
[ "$(cat proc/led/status)" = "3" ] || fail "led color did not write 3"
[ "$(code -X POST -d '{"frequency":7}' $BASE/led)" = "200" ] || fail "led freq status"
[ "$(cat proc/led/freq)" = "7" ] || fail "led freq did not write 7"
[ "$(code -X POST -d '{"color":9}' $BASE/led)" = "400" ] || fail "led color 9 should be 400"

# Server header present
curl -s -i $BASE/info | grep -qi '^Server: mfi-server/' || fail "missing Server header"

echo "PASS: all mfi-rest-server smoke checks"
