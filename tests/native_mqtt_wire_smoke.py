#!/usr/bin/env python3
"""Native publisher CLI and MQTT 3.1.1 smoke, using only isolated fake hardware.

Usage: python3 tests/native_mqtt_wire_smoke.py /absolute/path/to/mfi-mqtt-client

Reuses the bounded stdlib loopback broker from mqtt_wire_smoke.py. No installed
broker, device, HA, flash utility or updater is used. The 80-second deadline
includes reconnect and withheld-PUBACK recovery, but not real network congestion
or MIPS hardware/flash durability. MQTT 3.1.1 live delivery clears RETAIN; retained
command rejection is also tested by explicitly delivering the retained flag.
"""

import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

from mqtt_wire_smoke import (
    Broker, Harness, LABELS, Message, Probe, SmokeFailure, broker_self_check,
    client_command, json_object, replay, require,
)


UUID = re.compile(r"[0-9a-f]{32}\Z")
ROLES = {"power", "current", "voltage", "relay"}


class NativeHarness(Harness):
    def __init__(self, binary, root, broker):
        super().__init__(binary, root, broker)
        self.config_path = root / "publisher.conf"
        self.device_id = None
        self.session = None
        self.native_connections = set()
        self.requests = set()

    def cli(self, *arguments, success=True):
        result = subprocess.run(
            [str(self.binary), *map(str, arguments)], cwd=self.root,
            env=dict(os.environ, HOME=str(self.root), TMPDIR=str(self.root)),
            stdin=subprocess.DEVNULL, capture_output=True, timeout=3,
        )
        require((result.returncode == 0) == success,
                f"CLI {arguments!r}: exit {result.returncode}: {result.stderr.decode(errors='replace')}")
        return result

    def provisioning(self):
        original = '# preserve fixture settings\nserver = "127.0.0.1"\nusername = "test"\npassword = "test"\n'
        self.config_path.write_text(original, encoding="ascii")
        self.config_path.chmod(0o640)
        result = self.cli("--initialize-device-id", self.config_path)
        self.device_id = result.stdout.decode().strip().removeprefix("device_id=")
        require(UUID.fullmatch(self.device_id), "Provisioning did not return a canonical ID")
        expected = f'device_id = "{self.device_id}"\n' + original
        require(self.config_path.read_text() == expected, "Provisioning changed unrelated configuration")
        before = self.config_path.stat()
        repeated = self.cli("--initialize-device-id", self.config_path)
        require(repeated.stdout == result.stdout, "Provisioning regenerated identity")
        after = self.config_path.stat()
        require((before.st_ino, before.st_mtime_ns, before.st_mode) ==
                (after.st_ino, after.st_mtime_ns, after.st_mode), "Repeated provisioning rewrote the file")
        require(after.st_mode & 0o777 == 0o640, "Provisioning changed file permissions")
        require(not self.broker.publishers(), "Provisioning connected to the broker")

        cases = (
            ("--ha-mode", "native", "--device-id", self.device_id, "--no-update"),
            ("--ha-mode", "native", "--config", self.config_path, "--device-id", "f" * 32, "--no-update"),
            ("--ha-mode", "native", "--config", self.config_path, "--export-migration-map"),
            ("--initialize-device-id", self.config_path, "--server", "127.0.0.1"),
            ("--initialize-device-id", "relative.conf"),
            ("--ha-mode", "invalid", "--no-update"),
            ("--config", self.config_path, "--config", self.config_path, "--ha-mode", "native", "--no-update"),
        )
        for arguments in cases:
            result = self.cli(*arguments, success=False)
            require(b"Starting MQTT client" not in result.stdout, "Rejected configuration reached startup")
        alias_path = self.root / "alias.conf"
        alias_path.write_text(f'device-id = "{self.device_id}"\n')
        require(self.cli("--initialize-device-id", alias_path).stdout == repeated.stdout,
                "Hyphenated persisted identity alias was not reused")
        for contents in (
            'device_id = "bad"\n',
            f'device_id = "{self.device_id}"\ndevice-id = "{self.device_id}"\n',
            f'[nested]\ndevice_id = "{self.device_id}"\n',
        ):
            alias_path.write_text(contents)
            self.cli("--initialize-device-id", alias_path, success=False)
            self.cli("--config", alias_path, "--ha-mode", "native", "--no-update", success=False)
            require(alias_path.read_text() == contents, "Invalid identity was silently replaced")

        result = self.cli("--config", self.config_path, "--export-migration-map")
        mapping = json_object(result.stdout)
        require(mapping["device_id"] == self.device_id and mapping["schema_version"] == 1,
                "Migration export envelope is invalid")
        require(set(mapping) == {"schema_version", "device_id", "legacy_device_id",
                                 "legacy_full_id", "legacy_availability_topic", "ports"},
                "Migration export leaked unrelated configuration")
        require(not self.broker.publishers(), "Migration export connected to the broker")
        self.start(native=False)
        self.settle_epoch()
        for port in mapping["ports"]:
            require(set(port["roles"]) == ROLES, "Migration export is missing roles")
            for role, exported in port["roles"].items():
                topic, discovery = self.configs[f'{LABELS[port["id"]]} {role.title()}']
                require(exported["discovery_topic"] == topic, "Export discovery topic differs from legacy wire")
                for field in ("unique_id", "state_topic", "command_topic"):
                    require(exported.get(field) == discovery.get(field), f"Export {field} differs from wire")
        require(len(mapping["ports"]) == 8, "Migration export missed physical ports")
        require(mapping["legacy_availability_topic"] == self.shared, "Export availability topic differs")
        self.stop(native=False)
        self.passed("idempotent provisioning, CLI rejection, and export compared with unchanged default legacy wire")

    @property
    def base(self):
        return f"mfi/{self.device_id}"

    def start(self, native=True):
        require(self.process is None or self.process.poll() is not None, "Publisher already running")
        if self.log_file:
            self.log_file.close()
        path = self.root / f"publisher-{len(self.log_paths) + 1}.log"
        self.log_paths.append(path)
        self.log_file = path.open("wb")
        arguments = client_command(self.binary, self.broker.port) + ["--config", str(self.config_path)]
        if native:
            # Exercise configuration-file selection, not just the CLI mode switch.
            text = self.config_path.read_text()
            if "ha_mode" not in text:
                self.config_path.write_text('ha_mode = "native"\n' + text)
        else:
            self.broker.set_hold(
                lambda peer, message: peer.publisher and
                json_object(message.payload).get("availability") == "offline"
            )
        self.process = subprocess.Popen(
            arguments, cwd=self.root, env=dict(os.environ, HOME=str(self.root), TMPDIR=str(self.root)),
            stdin=subprocess.DEVNULL, stdout=self.log_file, stderr=subprocess.STDOUT,
        )

    def find(self, topic, predicate, since=0, timeout=3):
        return self.wait(
            lambda: next((event for event in self.publications(topic, since)
                          if predicate(json_object(event.message.payload))), None),
            topic, timeout=timeout,
        )

    def settle_native(self, previous, hold=False):
        self.epoch = self.wait(
            lambda: next((peer for peer in self.broker.publishers() if peer.number > previous), None),
            "native CONNECT", timeout=8,
        )
        self.native_connections.add(self.epoch.number)
        require(self.epoch.client_id == self.device_id, "MQTT client identity is not provisioned UUID")
        will = self.epoch.will
        require(will and will.topic == self.base + "/availability" and will.qos == 1 and will.retain,
                "Native Last Will policy is wrong")
        envelope = json_object(will.payload)
        require(set(envelope) == {"session_id", "state"} and envelope["state"] == "offline"
                and UUID.fullmatch(envelope["session_id"]), "Invalid native Last Will envelope")
        require(envelope["session_id"] != self.session, "Reconnect reused publisher session")
        self.session = envelope["session_id"]
        self.shared = will.topic
        self.find(self.shared, lambda data: data == {"session_id": self.session, "state": "offline"})
        descriptor_event = self.find(self.base + "/config", lambda data: data.get("device_id") == self.device_id)
        descriptor = json_object(descriptor_event.message.payload)
        require(descriptor_event.message.qos == 1 and descriptor_event.message.retain, "Descriptor policy")
        require(descriptor["schema_version"] == 1 and descriptor["model_id"] == "58952", "Descriptor identity")
        require(descriptor["refresh_interval"] == 2 and descriptor["expire_after"] == 6, "Descriptor intervals")
        require(len(descriptor["ports"]) == 8, "Descriptor missing ports")
        for port in descriptor["ports"]:
            require(port["name"] == LABELS[port["id"]] and set(port["capabilities"]) == ROLES,
                    "Descriptor port metadata/capabilities")
            first = self.find(self.base + f'/port/{port["id"]}/state', lambda data: data["sequence"] == 1)
            require(json_object(first.message.payload)["session_id"] == self.session, "Wrong initial session")
        if hold:
            self.observe(0.25)
            require(not any(json_object(event.message.payload).get("state") == "online"
                            for event in self.publications(self.shared)),
                    "Native online escaped unacknowledged descriptor")
            self.broker.set_hold(lambda peer, message: False)
            self.broker.release()
        self.find(self.shared, lambda data: data == {"session_id": self.session, "state": "online"})
        with self.broker.condition:
            require(self.epoch.subscriptions == {
                self.base + f"/port/{port}/set": 0 for port in LABELS
            }, "Native client subscribes outside native QoS 0 command topics")
        return descriptor

    def command(self, value, request):
        return json.dumps({"session_id": self.session, "request_id": request, "value": value}).encode()

    def send_command(self, value, number):
        request = f"{number:032x}"
        self.requests.add(request)
        since = self.broker.mark()
        with Probe(self.broker) as probe:
            probe.publish(self.base + "/port/1/set", self.command(value, request), retain=False, qos=0)
        event = self.find(self.base + "/port/1/state", lambda data: data.get("request_id") == request, since)
        require(event.message.qos == 0 and not event.message.retain, "Confirmation policy changed")
        return json_object(event.message.payload)

    def refresh_commands_errors(self):
        state = self.base + "/port/1/state"
        last = self.publications(state)[-1]
        newer = self.find(state, lambda data: data["sequence"] > json_object(last.message.payload)["sequence"],
                          last.seq + 1, timeout=2.8)
        require(1.8 <= newer.when - last.when <= 2.8, "Unchanged report missed refresh bounds")
        retained = replay(self.broker, [self.base + "/#"])
        require(set(retained) == {self.base + "/config", self.shared}, "Numeric state was retained")
        with Probe(self.broker) as probe:
            cached = probe.subscribe([state])
            require(not any(message.retain for message in cached), "Late subscriber received retained state")
            require(not probe.next_message(state).retain, "Late subscriber did not receive fresh state")
        require(self.send_command("OFF", 1)["relay"] == {"status": "ok", "value": "OFF"},
                "Unchanged OFF was not confirmed")
        require(self.send_command("ON", 2)["relay"] == {"status": "ok", "value": "ON"}, "ON not confirmed")
        since = self.broker.mark()
        invalid = [
            b"{}", b"[]", b"not-json", b'{"value":"OFF"}', b"x" * 513,
            self.command(False, "f" * 32), self.command("off", "f" * 32),
            self.command("OFF", "bad"),
            self.command("OFF", "f" * 32).replace(self.session.encode(), b"0" * 32),
            self.command("ON", "f" * 32)[:-1] + b', "value":"OFF"}',
        ]
        with Probe(self.broker) as probe:
            for payload in invalid:
                probe.publish(self.base + "/port/1/set", payload, retain=False, qos=0)
            probe.publish(self.base + "/port/9/set", self.command("OFF", "f" * 32), retain=False, qos=0)
            probe.publish(self.configs[f"{LABELS[1]} Relay"][1]["command_topic"],
                          b'{"value":"OFF"}', retain=False, qos=0)
        with self.broker.condition:
            self.broker._deliver(self.epoch, Message(self.base + "/port/1/set", self.command("OFF", "f" * 32), 0, True),
                                 retain=True, qos=0)
        self.observe(0.4)
        require((self.root / "proc/power/relay1").read_text() == "1", "Rejected command changed relay")
        require(not any("request_id" in json_object(event.message.payload)
                        for event in self.publications(state, since)), "Rejected command got a confirmation")
        require(self.send_command("OFF", 3)["relay"]["value"] == "OFF", "OFF not confirmed")
        for measurement, role, value, reason in (
            ("active_pwr", "power", "NaN", "invalid_data"),
            ("i_rms", "current", "-1", "invalid_data"),
            ("v_rms", "voltage", None, "open_failed"),
            ("relay", "relay", "broken", "invalid_data"),
        ):
            since = self.broker.mark()
            self.write(measurement, 1, value)
            self.find(state, lambda data: data[role] == {"status": "error", "reason": reason}, since)
        other = self.find(self.base + "/port/2/state", lambda data: data["power"]["status"] == "ok",
                          self.broker.mark(), timeout=2.8)
        require(json_object(other.message.payload)["power"]["value"] == 0, "One failed port affected another")
        self.write("relay", 1, None)
        require(self.send_command("ON", 4)["relay"] == {"status": "error", "reason": "write_failed"},
                "Failed relay write was falsely confirmed")
        require(not (self.root / "proc/power/relay1").exists(), "Missing relay file was recreated")
        since = self.broker.mark()
        for measurement, value in (("active_pwr", "100"), ("i_rms", "0.8333"), ("v_rms", "120"), ("relay", "0")):
            self.write(measurement, 1, value)
        self.find(state, lambda data: all(data[role]["status"] == "ok" for role in ROLES), since)
        self.passed("native retention/refresh, unchanged ON/OFF confirmation, malformed/retained rejection, read/write errors")

    def stop(self, native=True, acknowledge=True):
        since = self.broker.mark()
        self.broker.set_hold(lambda peer, message: not acknowledge and peer.publisher
                             and message.topic == self.shared
                             and json_object(message.payload).get("state") == "offline")
        self.process.send_signal(signal.SIGTERM)
        self.wait(lambda: self.process.poll() is not None, "publisher shutdown", timeout=7, allow_exit=True)
        require(self.process.returncode == (0 if acknowledge else 1), "Unexpected shutdown exit code")
        events = [event for event in self.broker.snapshot(since) if event.connection == self.epoch.number]
        require(any(event.kind == "publish" and event.message.topic == self.shared
                    and json_object(event.message.payload).get("state" if native else "availability") == "offline"
                    for event in events), "Shutdown omitted offline")
        self.wait(lambda: self.epoch.closed, "publisher socket close", allow_exit=True)
        events = [event for event in self.broker.snapshot(since) if event.connection == self.epoch.number]
        require(any(event.kind == "disconnect" for event in events) == acknowledge, "Shutdown DISCONNECT gate")
        if not acknowledge:
            require(any(event.kind == "will" for event in events), "Unacknowledged shutdown suppressed Last Will")
        self.broker.set_hold(lambda peer, message: False)

    def reconnects(self):
        previous = self.epoch.number
        old_session = self.session
        self.broker.drop(self.epoch)
        self.settle_native(previous)
        since = self.broker.mark()
        with Probe(self.broker) as probe:
            probe.publish(self.base + "/port/1/set",
                          self.command("ON", "e" * 32).replace(self.session.encode(), old_session.encode()),
                          retain=False, qos=0)
        self.observe(0.3)
        require((self.root / "proc/power/relay1").read_text().strip() == "0", "Old session command replayed")
        require(not any("request_id" in json_object(event.message.payload)
                        for event in self.publications(self.base + "/port/1/state", since)), "Old request confirmed")
        previous = self.epoch.number
        self.broker.drop(self.epoch)
        self.broker.set_hold(lambda peer, message: peer.publisher and peer.number > previous
                             and message.topic == self.base + "/config")
        stalled = self.wait(lambda: next((peer for peer in self.broker.publishers() if peer.number > previous), None),
                            "stalled native connection", timeout=8)
        self.native_connections.add(stalled.number)
        self.wait(lambda: stalled.closed, "five-second descriptor PUBACK deadline", timeout=7)
        stalled_events = [event for event in self.broker.snapshot() if event.connection == stalled.number]
        require(any(event.kind == "will" for event in stalled_events), "Delivery timeout suppressed Last Will")
        require(not any(event.kind == "publish" and event.message.topic == self.shared
                        and json_object(event.message.payload).get("state") == "online"
                        for event in stalled_events), "Stalled descriptor allowed online")
        self.broker.set_hold(lambda peer, message: False)
        self.settle_native(stalled.number)
        self.stop()
        previous = self.epoch.number
        self.start()
        self.settle_native(previous)
        self.passed("new sessions before Last Will, clean reconnect without command replay, delivery deadline and process restart")

    def audit_native(self):
        sessions = set()
        for peer in self.broker.publishers():
            if peer.number not in self.native_connections:
                continue
            session = json_object(peer.will.payload)["session_id"]
            require(session not in sessions, "A connection reused a session")
            sessions.add(session)
            sequences = {}
            for event in self.publications(epoch=peer):
                message = event.message
                data = json_object(message.payload)
                if message.topic in (self.base + "/config", self.shared):
                    require(message.qos == 1 and message.retain, "Retained control-plane policy")
                    if message.topic == self.shared:
                        require(data == {"session_id": session, "state": data["state"]}
                                and data["state"] in ("online", "offline"), "Availability envelope")
                    continue
                require(re.fullmatch(re.escape(self.base) + r"/port/[1-8]/state", message.topic),
                        f"Unexpected native publisher topic {message.topic}")
                require(message.qos == 0 and not message.retain and not message.dup, "State policy")
                require(set(data) in (ROLES | {"session_id", "sequence"},
                                      ROLES | {"session_id", "sequence", "request_id"}), "State envelope")
                require(data["session_id"] == session, "State belongs to another session")
                sequence = data["sequence"]
                require(type(sequence) is int and sequences.get(message.topic, 0) < sequence <= 2**63 - 1,
                        "State sequence is not a positive monotonic int64")
                sequences[message.topic] = sequence
                for role in ROLES:
                    result = data[role]
                    if result["status"] == "error":
                        require(set(result) == {"status", "reason"} and result["reason"], "Error has fake numeric data")
                    else:
                        require(set(result) == {"status", "value"} and result["status"] == "ok", "Role result shape")
                        value = result["value"]
                        require(value in ("ON", "OFF") if role == "relay" else
                                type(value) in (int, float) and math.isfinite(value) and value >= 0,
                                "Invalid successful role value")
                if "request_id" in data:
                    require(UUID.fullmatch(data["request_id"]), "Invalid request ID")
        for request in self.requests:
            confirmations = [event for event in self.broker.snapshot()
                             if event.connection in self.native_connections and event.kind == "publish"
                             and json_object(event.message.payload).get("request_id") == request]
            require(len(confirmations) == 1, "Confirmation was omitted or replayed across connections")
        self.broker.check()

    def run(self):
        self.provisioning()
        previous = self.epoch.number
        self.broker.set_hold(lambda peer, message: peer.publisher and message.topic == self.base + "/config")
        self.start()
        self.settle_native(previous, hold=True)
        self.refresh_commands_errors()
        self.reconnects()
        self.stop(acknowledge=False)
        self.audit_native()
        self.passed("bounded offline shutdown preserves Last Will without PUBACK; complete native wire audit")


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    binary = Path(sys.argv[1])
    require(binary.is_absolute() and binary.is_file() and os.access(binary, os.X_OK),
            "Provide an absolute path to an executable local host binary")

    def expired(signum, frame):
        raise SmokeFailure("Native MQTT wire smoke exceeded 80 seconds")

    previous_handler = signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, 80)
    started = time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix="mfi-native-wire-") as directory:
            broker = Broker()
            harness = None
            try:
                broker_self_check(broker)
                harness = NativeHarness(binary, Path(directory), broker)
                harness.run()
            except (SmokeFailure, OSError, EOFError, KeyError, TypeError, ValueError, subprocess.TimeoutExpired) as error:
                print(f"FAIL: {error}", file=sys.stderr)
                if harness:
                    harness.diagnostics()
                return 1
            finally:
                signal.setitimer(signal.ITIMER_REAL, 0)
                try:
                    if harness:
                        harness.close()
                finally:
                    broker.close()
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous_handler)
    print(f"PASS: native MQTT wire smoke completed in {time.monotonic() - started:.2f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
