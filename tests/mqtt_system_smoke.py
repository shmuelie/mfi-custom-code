"""System telemetry wire contract using owned fake proc files and the existing broker."""

import json
from pathlib import Path
import signal
import sys
import tempfile
import time

from mqtt_wire_smoke import (
    Broker, Harness, LABELS, SmokeFailure, availability, client_command, json_object,
    replay, require,
)


METRICS = {
    "CPU Utilization": ("%", None),
    "Memory Total": ("MiB", "data_size"),
    "Memory Available": ("MiB", "data_size"),
    "Memory Used": ("MiB", "data_size"),
    "Memory Utilization": ("%", None),
}


class SystemHarness(Harness):
    def __init__(self, binary, root, broker):
        super().__init__(binary, root, broker)
        self.metric_topics = {}
        self.metric_gates = {}
        self.cpu_ticks = 0
        self.config_file = None
        self.enabled = True
        self.proc("meminfo", "MemTotal: 65536 kB\nMemAvailable: 49152 kB\n")

    def proc(self, name, text):
        path = self.root / "proc" / name
        if text is None:
            path.unlink(missing_ok=True)
        else:
            staging = path.with_suffix(".new")
            staging.write_text(text, encoding="ascii")
            staging.replace(path)

    def command(self, updates=False):
        # No enable flag: the first process must report metrics by default.
        command = client_command(
            self.binary, self.broker.port, updates=updates, system=True,
            polling=5000 if self.enabled else 100, refresh=5, expiry=15,
        )
        if self.config_file:
            return command + ["--config", str(self.config_file)]
        return command + [
            "--system-polling-interval", "1", "--system-refresh-interval", "2",
            "--system-expire-after", "6", "--system-proc-root", str(self.root / "proc"),
        ]

    def connect_epoch(self, previous=0):
        self.epoch = self.wait(
            lambda: next((peer for peer in self.broker.publishers() if peer.number > previous), None),
            "system publisher CONNECT", timeout=7.0,
        )
        self.shared = self.epoch.will.topic
        self.gate(self.shared, "offline")
        expected = {f"{label} {kind}" for label in LABELS.values()
                    for kind in ("Power", "Current", "Voltage", "Relay")}
        if self.enabled:
            expected.update(METRICS)

        def discovered():
            found = {}
            for event in self.publications():
                if event.message.topic.startswith("homeassistant/"):
                    config = json_object(event.message.payload)
                    name = config.get("name")
                    require(name in expected and name not in found, f"Unexpected/duplicate entity {name!r}")
                    require(event.message.qos == 1 and event.message.retain, "Discovery retention changed")
                    found[name] = (event.message.topic, config)
            return found if set(found) == expected else None

        self.configs = self.wait(discovered, "system and port discovery")
        require(len({config["unique_id"] for _, config in self.configs.values()}) == len(expected),
                "Discovery unique IDs collide")
        for name, (unit, device_class) in METRICS.items():
            if not self.enabled:
                continue
            config = self.configs[name][1]
            for key, value in (
                ("entity_category", "diagnostic"), ("state_class", "measurement"),
                ("unit_of_measurement", unit), ("suggested_display_precision", 1),
                ("value_template", "{{ value_json.value }}"), ("expire_after", 6),
                ("availability_mode", "all"),
            ):
                require(config.get(key) == value, f"{name}: incorrect {key}")
            if device_class:
                require(config.get("device_class") == device_class, f"{name}: incorrect device class")
            else:
                require("device_class" not in config, f"{name}: invented percentage device class")
            entries = config.get("availability", [])
            topics = {entry["topic"] for entry in entries}
            require(len(entries) == 2 and self.shared in topics, f"{name}: missing availability gates")
            self.metric_topics[name] = config["state_topic"]
            self.metric_gates[name] = next(topic for topic in topics if topic != self.shared)
        if self.enabled:
            for name in METRICS:
                self.gate(self.metric_gates[name], "offline")
            self.observe(0.2)
            require(not any(self.publications(topic) for topic in self.metric_topics.values()),
                    "Telemetry escaped unacknowledged initial offline gates")
        self.broker.set_hold(lambda peer, message: False)
        self.broker.release()
        self.gate(self.shared, "online", timeout=7.0)
        if self.enabled:
            for name, value in (("Memory Total", 64), ("Memory Available", 48),
                                ("Memory Used", 16), ("Memory Utilization", 25)):
                self.state(self.metric_topics[name], value, timeout=3.0)

    def pump_cpu(self, predicate, description, timeout=4.0):
        def advance():
            result = predicate()
            if result:
                return result
            self.cpu_ticks += 1
            self.proc("stat", f"cpu {self.cpu_ticks * 25} 0 0 {self.cpu_ticks * 75}\n")
            return None
        return self.wait(advance, description, timeout=timeout)

    def numeric(self, name, value, since=0):
        return next(
            (event for event in self.publications(self.metric_topics[name], since)
             if json_object(event.message.payload) == {"value": value}), None,
        )

    def run(self):
        self.start()
        self.connect_epoch()
        memory = self.publications(self.metric_topics["Memory Total"])[0]
        power_topic = self.configs[f"{LABELS[1]} Power"][1]["state_topic"]
        require(memory.when < self.publications(power_topic)[0].when,
                "System sampling waited for the five-second hardware poll")
        require(not self.publications(self.metric_topics["CPU Utilization"]),
                "Missing CPU file produced a numeric substitute")
        self.pump_cpu(lambda: len(self.publications(self.metric_topics["CPU Utilization"])) >= 3,
                      "CPU first sample and two unchanged refreshes", timeout=8.0)
        for name in METRICS:
            events = self.publications(self.metric_topics[name])
            require(len(events) >= 3, f"{name}: missing successful unchanged refreshes")
            for event in events:
                require(event.message.qos == 0 and not event.message.retain, f"{name}: retained telemetry")
            for before, after in zip(events, events[1:]):
                require(1.8 <= after.when - before.when <= 3.5, f"{name}: incorrect refresh interval")
        retained = replay(self.broker, list(self.metric_topics.values()) + list(self.metric_gates.values()))
        require(not set(self.metric_topics.values()) & set(retained), "System numeric state retained")
        for topic in self.metric_gates.values():
            require(availability(retained[topic].payload, "online"), "System availability not retained online")
        self.passed("five default-enabled diagnostic entities; fresh non-retained state and periodic refresh")

        mark = self.broker.mark()
        self.proc("meminfo", "MemTotal: invalid kB\n")
        for topic in self.metric_gates.values():
            if topic != self.metric_gates["CPU Utilization"]:
                self.gate(topic, "offline", mark, timeout=3.0)
        invalid_start = self.broker.mark()
        self.pump_cpu(lambda: self.numeric("CPU Utilization", 25, invalid_start),
                      "CPU reporting independently of memory failure")
        for name in METRICS:
            if name != "CPU Utilization":
                require(not self.publications(self.metric_topics[name], invalid_start),
                        f"{name}: malformed memory produced numeric state")
        self.proc("meminfo", "MemTotal: 65536 kB\nMemFree: 8192 kB\nBuffers: 8192 kB\nCached: 16384 kB\n")
        self.state(self.metric_topics["Memory Available"], 32, invalid_start, timeout=3.0)
        self.state(self.metric_topics["Memory Utilization"], 50, invalid_start, timeout=3.0)
        self.passed("memory read failure isolation and legacy availability estimate recovery")

        recovery_mark = self.broker.mark()
        sample = self.pump_cpu(lambda: self.numeric("CPU Utilization", 25, recovery_mark),
                               "healthy CPU before removing its proc file")
        self.gate(self.metric_gates["CPU Utilization"], "online", sample.seq, timeout=3.0)
        mark = self.broker.mark()
        self.proc("stat", None)
        offline = self.gate(self.metric_gates["CPU Utilization"], "offline", mark, timeout=3.0)
        self.proc("meminfo", "MemTotal: 65536 kB\nMemAvailable: 49152 kB\n")
        self.state(self.metric_topics["Memory Utilization"], 25, mark, timeout=3.0)
        require(not self.publications(self.metric_topics["CPU Utilization"], offline.seq + 1),
                "Failed CPU kept publishing")
        self.pump_cpu(lambda: self.numeric("CPU Utilization", 25, offline.seq + 1), "CPU recovery")

        old_configs = self.configs
        previous = self.epoch.number
        self.broker.set_hold(lambda peer, message: peer.publisher and availability(message.payload, "offline"))
        self.broker.drop(self.epoch)
        self.connect_epoch(previous)
        require(self.configs == old_configs, "System discovery changed on reconnect")
        # A new epoch requires two proc reads; cached CPU cannot be replayed.
        require(not self.publications(self.metric_topics["CPU Utilization"]), "Reconnect replayed cached CPU")
        self.pump_cpu(lambda: self.numeric("CPU Utilization", 25), "fresh CPU after reconnect")
        self.passed("CPU failure isolation, stable discovery and reconnect without cached CPU replay")

        self.process.send_signal(signal.SIGTERM)
        self.wait(lambda: self.process.poll() is not None, "system publisher orderly shutdown", allow_exit=True)
        require(self.process.returncode == 0, "System publisher shutdown failed")
        previous = self.epoch.number
        self.enabled = False
        self.config_file = self.root / "system-disabled.toml"
        self.config_file.write_text(
            "system-metrics = false\nsystem-polling-interval = 1\n"
            "system-refresh-interval = 2\nsystem-expire-after = 6\n"
            f"system-proc-root = {json.dumps(str(self.root / 'proc'))}\n",
            encoding="ascii",
        )
        self.start()
        self.connect_epoch(previous)
        self.observe(1.2)
        system_topics = set(self.metric_topics.values()) | set(self.metric_gates.values())
        require(not any(event.message.topic in system_topics for event in self.publications()),
                "Config opt-out still published system telemetry")
        self.passed("configuration-file opt-out preserves all 32 outlet entities")


class DefaultTimingHarness(SystemHarness):
    def command(self, updates=False):
        return client_command(
            self.binary, self.broker.port, polling=1000, refresh=60, expiry=180,
            updates=updates, system=True,
        ) + ["--system-proc-root", str(self.root / "proc")]

    def connect_epoch(self, previous=0):
        self.epoch = self.wait(
            lambda: next((peer for peer in self.broker.publishers() if peer.number > previous), None),
            "default-timing publisher CONNECT", timeout=7.0,
        )
        self.shared = self.epoch.will.topic

        def discovered():
            found = {}
            for event in self.publications():
                if event.message.topic.startswith("homeassistant/"):
                    config = json_object(event.message.payload)
                    found[config["name"]] = config
            return found if len(found) == 37 else None

        configs = self.wait(discovered, "default-timing discovery")
        system_states = [configs[name]["state_topic"] for name in METRICS]
        channel_topics = {
            entry["topic"] for config in configs.values()
            for entry in config.get("availability", [])
        }
        for name in METRICS:
            require(configs[name]["expire_after"] == 180, "System expiration does not use the default")
        offline = {topic: self.gate(topic, "offline") for topic in channel_topics}
        self.observe(0.4)
        require(not any(self.publications(topic) for topic in system_states),
                "Default-timing diagnostics escaped initial offline gates")
        require(not any(availability(event.message.payload, "online")
                        for event in self.publications(self.shared)),
                "Shared online escaped unacknowledged offline gates")
        released = time.monotonic()
        self.broker.set_hold(lambda peer, message: False)
        self.broker.release()
        shared_online = self.gate(self.shared, "online", timeout=3.0)
        require(shared_online.when - released < 3.0,
                "Shared readiness waited for the ten-second system sampling interval")
        for event in offline.values():
            acknowledged = self.ack(event)
            require(acknowledged and acknowledged.seq < shared_online.seq,
                    "Shared online preceded an initial offline PUBACK")
        for label in LABELS.values():
            config = configs[f"{label} Power"]
            channel = next(entry["topic"] for entry in config["availability"] if entry["topic"] != self.shared)
            online = self.gate(channel, "online")
            acknowledged = self.ack(online)
            require(acknowledged and acknowledged.seq < shared_online.seq,
                    "Shared readiness bypassed an outlet power online PUBACK")
        require(not any(self.publications(topic) for topic in system_states),
                "Default-timing diagnostics replayed a pre-ACK sample")
        self.passed(
            f"default-timing {'reconnect' if previous else 'startup'} becomes shared online "
            f"{shared_online.when - released:.3f}s after delayed offline PUBACKs"
        )

    def run(self):
        self.proc("stat", "cpu 1 0 0 9\n")
        self.start()
        self.connect_epoch()
        previous = self.epoch.number
        self.broker.set_hold(lambda peer, message: peer.publisher and availability(message.payload, "offline"))
        self.broker.drop(self.epoch)
        self.connect_epoch(previous)


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="mfi-system-wire-") as directory:
        for harness_type in (DefaultTimingHarness, SystemHarness):
            root = Path(directory) / harness_type.__name__
            root.mkdir()
            broker = Broker()
            harness = harness_type(binary, root, broker)
            try:
                harness.run()
                broker.check()
            except (SmokeFailure, OSError, EOFError, KeyError) as error:
                print(f"FAIL: {error}", file=sys.stderr)
                harness.diagnostics()
                return 1
            finally:
                harness.close()
                broker.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
