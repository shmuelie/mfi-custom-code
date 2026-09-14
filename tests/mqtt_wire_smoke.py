#!/usr/bin/env python3
"""Isolated, stdlib-only MQTT 3.1.1 acceptance test for a host mfi-mqtt-client.

Usage: python3 tests/mqtt_wire_smoke.py /absolute/path/to/mfi-mqtt-client

The executable runs only against fake hardware in an owned TemporaryDirectory
and an ephemeral IPv4 loopback listener. Updates are disabled except for one
fail-closed test with PATH restricted to a fixture-only, non-networking wget.
Nothing connects to an installed broker. The complete run has an 80-second
deadline (plus bounded cleanup); ordinary runs should take about 35 seconds.
Linux pidfds protect emergency cleanup of the fixture downloader.

This checks the publisher's wire contract, not Home Assistant itself. A fixture
snapshot restores retained records into a new broker instance; this is not a
real broker's crash-durability or OS-restart test. It does not simulate arbitrary
transport backpressure, rejected QoS 0 enqueue, real update downloads/application,
or minute/hour-scale scheduling; those need separate tests.
"""

import base64
import errno
import json
import math
import os
from pathlib import Path
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass


OVERALL_TIMEOUT = 80.0
PACKET_TIMEOUT = 2.0
MAX_PACKET = 65536
REFRESH = 2.0
REFRESH_LIMIT = 2.8  # 2 seconds + one 100 ms poll + scheduling allowance.
PROMPT_LIMIT = 2.0
OFFLINE = b'{"availability":"offline"}'
ONLINE = b'{"availability":"online"}'
LABELS = {port: f"Wire Port {port:02d}" for port in range(1, 9)}


class SmokeFailure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise SmokeFailure(message)


def json_object(payload):
    def reject_constant(value):
        raise SmokeFailure(f"Non-JSON numeric constant {value!r}: {payload!r}")

    try:
        value = json.loads(payload, parse_constant=reject_constant)
    except (ValueError, UnicodeError) as error:
        raise SmokeFailure(f"Invalid JSON {payload!r}: {error}") from error
    require(isinstance(value, dict), f"Expected a JSON object: {payload!r}")
    return value


def availability(payload, value):
    return json_object(payload) == {"availability": value}


def watts(payload):
    value = json_object(payload)
    require(set(value) == {"value"}, f"Unexpected power payload shape: {value!r}")
    number = value["value"]
    require(
        type(number) in (int, float) and math.isfinite(number) and number >= 0,
        f"Power is not a finite, nonnegative number: {payload!r}",
    )
    return number


def mqtt_string(value):
    data = value.encode("utf-8") if isinstance(value, str) else value
    require(len(data) <= 65535, "MQTT string exceeds its length field")
    return struct.pack("!H", len(data)) + data


def frame(header, body=b""):
    require(len(body) <= MAX_PACKET, "Test MQTT packet exceeds size limit")
    remaining = len(body)
    encoded = bytearray()
    while True:
        digit = remaining % 128
        remaining //= 128
        encoded.append(digit | (0x80 if remaining else 0))
        if not remaining:
            return bytes([header]) + bytes(encoded) + body


def receive_exact(sock, count, deadline):
    result = bytearray()
    while len(result) < count:
        require(time.monotonic() < deadline, "Timed out reading a complete MQTT packet")
        try:
            chunk = sock.recv(count - len(result))
        except socket.timeout:
            continue
        if not chunk:
            raise EOFError("MQTT peer closed the connection")
        result.extend(chunk)
    return bytes(result)


def receive_packet(sock, deadline=None):
    if deadline is None:
        # Idle broker sockets may time out; once a packet starts, it must finish.
        first = sock.recv(1)
        if not first:
            raise EOFError("MQTT peer closed the connection")
        deadline = time.monotonic() + PACKET_TIMEOUT
    else:
        first = receive_exact(sock, 1, deadline)
    remaining = 0
    for index in range(4):
        digit = receive_exact(sock, 1, deadline)[0]
        remaining += (digit & 0x7F) << (7 * index)
        require(remaining <= MAX_PACKET, "MQTT remaining length exceeds test limit")
        if not digit & 0x80:
            return first[0], receive_exact(sock, remaining, deadline)
    raise SmokeFailure("Malformed MQTT remaining length")


class Cursor:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def take(self, count):
        require(self.offset + count <= len(self.data), "Truncated MQTT packet")
        result = self.data[self.offset:self.offset + count]
        self.offset += count
        return result

    def byte(self):
        return self.take(1)[0]

    def integer(self):
        return struct.unpack("!H", self.take(2))[0]

    def binary(self):
        return self.take(self.integer())

    def text(self):
        return self.binary().decode("utf-8")

    def rest(self):
        return self.take(len(self.data) - self.offset)

    def done(self):
        require(self.offset == len(self.data), "Unexpected trailing MQTT bytes")


@dataclass(frozen=True)
class Message:
    topic: str
    payload: bytes
    qos: int
    retain: bool
    mid: int = 0
    dup: bool = False


def parse_publish(header, body):
    require(header >> 4 == 3, "Expected PUBLISH")
    qos = (header >> 1) & 3
    require(qos in (0, 1), f"Unsupported PUBLISH QoS {qos}")
    cursor = Cursor(body)
    topic = cursor.text()
    require(topic and "+" not in topic and "#" not in topic, "Invalid PUBLISH topic")
    mid = cursor.integer() if qos else 0
    require(not qos or mid != 0, "QoS 1 PUBLISH has zero message ID")
    return Message(topic, cursor.rest(), qos, bool(header & 1), mid, bool(header & 8))


def topic_matches(topic_filter, topic):
    filters = topic_filter.split("/")
    levels = topic.split("/")
    if topic.startswith("$") and filters[0] in ("+", "#"):
        return False
    for index, part in enumerate(filters):
        if part == "#":
            return index == len(filters) - 1
        if index >= len(levels) or part not in ("+", levels[index]):
            return False
    return len(filters) == len(levels)


@dataclass(frozen=True)
class Event:
    seq: int
    when: float
    kind: str
    connection: int
    message: Message = None


class Peer:
    def __init__(self, number, sock):
        self.number = number
        self.sock = sock
        self.client_id = ""
        self.publisher = False
        self.will = None
        self.connected = False
        self.graceful = False
        self.closing = False
        self.closed = False
        self.subscriptions = {}
        self.next_mid = 1


def shutdown_socket(sock):
    try:
        sock.shutdown(socket.SHUT_RDWR)
    except OSError as error:
        if error.errno not in (
            errno.EBADF, errno.ENOTCONN, errno.EINVAL, errno.ECONNRESET,
        ):
            raise


class Broker:
    """Small bounded broker; all sends/retention/event ordering share one lock."""

    def __init__(self, retained_path=None):
        self.condition = threading.Condition(threading.RLock())
        self.stopping = threading.Event()
        self.retained = self.restore_retained(retained_path) if retained_path else {}
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(16)
        self.listener.settimeout(0.2)
        self.port = self.listener.getsockname()[1]
        self.peers = []
        self.threads = []
        self.events = []
        self.event_bytes = 0
        self.errors = []
        self.held = []
        self.hold_ack = lambda peer, message: False
        self.thread = threading.Thread(target=self._accept, name="wire-broker", daemon=True)
        self.thread.start()

    def _record(self, kind, peer, message=None):
        require(len(self.events) < 20000, "MQTT event limit exceeded (publication flood)")
        self.event_bytes += len(message.payload) if message else 0
        require(self.event_bytes <= 8 * 1024 * 1024, "MQTT event payload budget exceeded")
        event = Event(len(self.events), time.monotonic(), kind, peer.number, message)
        self.events.append(event)
        self.condition.notify_all()
        return event

    def _send(self, peer, packet):
        peer.sock.sendall(packet)

    def _error(self, description):
        with self.condition:
            self.errors.append(description)
            self.condition.notify_all()

    def _accept(self):
        try:
            while not self.stopping.is_set():
                try:
                    sock, address = self.listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    if self.stopping.is_set():
                        return
                    raise
                with self.condition:
                    if self.stopping.is_set():
                        sock.close()
                        return
                    if address[0] != "127.0.0.1" or len(self.peers) >= 128:
                        sock.close()
                        raise SmokeFailure("Non-loopback peer or test connection limit exceeded")
                    sock.settimeout(0.2)
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    peer = Peer(len(self.peers) + 1, sock)
                    self.peers.append(peer)
                    thread = threading.Thread(
                        target=self._serve, args=(peer,),
                        name=f"wire-peer-{peer.number}", daemon=True,
                    )
                    self.threads.append(thread)
                    thread.start()
        except (OSError, SmokeFailure) as error:
            self._error(f"Broker accept failed: {error}")

    def _connect(self, peer, header, body):
        require(header == 0x10, "First MQTT packet must be CONNECT")
        cursor = Cursor(body)
        require(cursor.text() == "MQTT" and cursor.byte() == 4, "Expected MQTT 3.1.1")
        flags = cursor.byte()
        require(not flags & 1 and flags & 2, "Test clients must use clean sessions")
        cursor.integer()  # Keepalive is serviced by PINGREQ/PINGRESP, not a test timer.
        peer.client_id = cursor.text()
        if flags & 4:
            qos = (flags >> 3) & 3
            require(qos in (0, 1), "Unsupported Last Will QoS")
            peer.will = Message(cursor.text(), cursor.binary(), qos, bool(flags & 32))
        else:
            require(not flags & 0x38, "Will flags present without a Last Will")
        username = cursor.text() if flags & 128 else None
        password = cursor.binary() if flags & 64 else None
        cursor.done()
        peer.publisher = username == "test" and password == b"test"
        require(peer.publisher or username is None, "Unexpected test credentials")
        require(peer.client_id, "Test clients must provide distinct, nonempty IDs")
        peer.connected = True
        self._record("connect", peer, peer.will)
        self._send(peer, frame(0x20, b"\x00\x00"))

    def _deliver(self, peer, message, retain, qos):
        mid = peer.next_mid if qos else 0
        if qos:
            peer.next_mid = mid % 65535 + 1
        body = mqtt_string(message.topic)
        if qos:
            body += struct.pack("!H", mid)
        try:
            self._send(peer, frame(0x30 | (qos << 1) | int(retain), body + message.payload))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            peer.closing = True
            shutdown_socket(peer.sock)

    def _route(self, peer, message, kind):
        event = self._record(kind, peer, message)
        if message.retain:
            if message.payload:
                self.retained[message.topic] = message
            else:
                self.retained.pop(message.topic, None)
        # A non-retained update deliberately does NOT remove an older record.
        for subscriber in self.peers:
            if not subscriber.connected or subscriber.closed or subscriber.closing:
                continue
            matched = [
                qos for topic_filter, qos in subscriber.subscriptions.items()
                if topic_matches(topic_filter, message.topic)
            ]
            if matched:
                self._deliver(subscriber, message, False, min(message.qos, max(matched)))
        return event

    def _ack(self, peer, event):
        self._send(peer, frame(0x40, struct.pack("!H", event.message.mid)))
        self._record("puback_sent", peer, event.message)

    def _packet(self, peer, header, body):
        if not peer.connected:
            self._connect(peer, header, body)
        elif header >> 4 == 3:
            message = parse_publish(header, body)
            event = self._route(peer, message, "publish")
            if message.qos:
                if self.hold_ack(peer, message):
                    self.held.append((peer, event))
                    self._record("ack_held", peer, message)
                else:
                    self._ack(peer, event)
        elif header == 0x82:
            cursor = Cursor(body)
            mid = cursor.integer()
            require(mid != 0, "SUBSCRIBE has zero message ID")
            subscriptions = {}
            while cursor.offset < len(body):
                topic_filter, qos = cursor.text(), cursor.byte()
                require(topic_filter and qos in (0, 1), "Invalid test subscription")
                levels = topic_filter.split("/")
                require(
                    all(("+" not in part or part == "+") and
                        ("#" not in part or (part == "#" and index == len(levels) - 1))
                        for index, part in enumerate(levels)),
                    "Invalid MQTT topic filter",
                )
                subscriptions[topic_filter] = qos
            require(subscriptions, "Empty SUBSCRIBE")
            peer.subscriptions.update(subscriptions)
            self._record("subscribe", peer)
            self._send(peer, frame(0x90, struct.pack("!H", mid) + bytes(subscriptions.values())))
            for topic, message in self.retained.items():
                requested = [
                    qos for topic_filter, qos in subscriptions.items()
                    if topic_matches(topic_filter, topic)
                ]
                if requested:
                    self._deliver(peer, message, True, min(message.qos, max(requested)))
        elif header == 0x40:
            require(len(body) == 2 and body != b"\x00\x00", "Malformed PUBACK")
            self._record("puback_received", peer)
        elif header == 0xC0:
            require(not body, "Malformed PINGREQ")
            self._record("pingreq", peer)
            self._send(peer, frame(0xD0))
        elif header == 0xE0:
            require(not body, "Malformed DISCONNECT")
            peer.graceful = True
            self._record("disconnect", peer)
        else:
            raise SmokeFailure(f"Unsupported MQTT packet 0x{header:02x}")

    def _serve(self, peer):
        try:
            while not self.stopping.is_set() and not peer.closing and not peer.graceful:
                try:
                    header, body = receive_packet(peer.sock)
                except socket.timeout:
                    continue
                with self.condition:
                    self._packet(peer, header, body)
        except (EOFError, ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
            pass
        except (OSError, SmokeFailure, UnicodeError, ValueError, struct.error) as error:
            if not self.stopping.is_set() and not peer.closing:
                self._error(f"Broker connection {peer.number} ({peer.client_id!r}): {error}")
        finally:
            with self.condition:
                peer.closed = True
                try:
                    if peer.will and not peer.graceful and not self.stopping.is_set():
                        self._route(peer, peer.will, "will")
                    self._record("closed", peer)
                except (OSError, SmokeFailure) as error:
                    self._error(f"Broker closing connection {peer.number}: {error}")
                finally:
                    peer.sock.close()

    def check(self):
        with self.condition:
            require(not self.errors, "; ".join(self.errors))

    def mark(self):
        with self.condition:
            return len(self.events)

    def snapshot(self, since=0):
        with self.condition:
            return list(self.events[since:])

    def publishers(self):
        with self.condition:
            return [peer for peer in self.peers if peer.publisher]

    def set_hold(self, predicate):
        with self.condition:
            self.hold_ack = predicate

    def release(self, predicate=lambda peer, message: True):
        with self.condition:
            remaining = []
            for peer, event in self.held:
                if peer.closed or peer.closing:
                    continue
                if predicate(peer, event.message):
                    self._ack(peer, event)
                else:
                    remaining.append((peer, event))
            self.held = remaining

    def drop(self, peer):
        with self.condition:
            peer.closing = True
            shutdown_socket(peer.sock)

    def save_retained(self, path):
        with self.condition:
            snapshot = {
                "version": 1,
                "records": [
                    {
                        "topic": topic, "qos": message.qos,
                        "payload": base64.b64encode(message.payload).decode("ascii"),
                    }
                    for topic, message in sorted(self.retained.items())
                ],
            }
        path.write_text(json.dumps(snapshot), encoding="ascii")

    @staticmethod
    def restore_retained(path):
        require(path.stat().st_size <= 8 * 1024 * 1024, "Retained snapshot exceeds fixture size limit")
        snapshot = json_object(path.read_bytes())
        require(snapshot.get("version") == 1, "Unsupported retained snapshot version")
        records = snapshot.get("records")
        require(isinstance(records, list) and len(records) <= 4096, "Invalid retained snapshot records")
        retained = {}
        for record in records:
            require(isinstance(record, dict), "Invalid retained snapshot record")
            topic, qos = record["topic"], record["qos"]
            require(
                isinstance(topic, str) and topic and "+" not in topic and "#" not in topic
                and topic not in retained,
                f"Invalid or duplicate retained snapshot topic: {topic!r}",
            )
            require(type(qos) is int and qos in (0, 1), "Invalid retained snapshot QoS")
            payload = base64.b64decode(record["payload"], validate=True)
            require(0 < len(payload) <= MAX_PACKET, "Invalid retained snapshot payload length")
            retained[topic] = Message(topic, payload, qos, True)
        return retained

    def close(self):
        self.stopping.set()
        self.listener.close()
        with self.condition:
            for peer in self.peers:
                if not peer.closed:
                    shutdown_socket(peer.sock)
        deadline = time.monotonic() + 3.0
        for thread in [self.thread] + self.threads:
            thread.join(max(0.0, deadline - time.monotonic()))
        alive = [thread.name for thread in [self.thread] + self.threads if thread.is_alive()]
        require(not alive, f"Broker threads did not stop: {alive}")


class Probe:
    """A new clean-session subscriber/publisher, never the executable's socket."""

    sequence = 0

    def __init__(self, broker, will=None):
        Probe.sequence += 1
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(0.2)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.mid = 0
        try:
            self.sock.connect(("127.0.0.1", broker.port))
            flags = 2 | (0x2C if will else 0)
            body = mqtt_string("MQTT") + bytes([4, flags]) + struct.pack("!H", 10)
            body += mqtt_string(f"wire-probe-{Probe.sequence}")
            if will:
                body += mqtt_string(will.topic) + mqtt_string(will.payload)
            self.sock.sendall(frame(0x10, body))
            require(self.packet() == (0x20, b"\x00\x00"), "Probe CONNECT was not accepted")
        except (OSError, SmokeFailure, EOFError):
            self.sock.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.close()

    def close(self, abrupt=False):
        if self.sock.fileno() == -1:
            return
        try:
            if not abrupt:
                self.sock.sendall(frame(0xE0))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
        finally:
            self.sock.close()

    def packet(self, deadline=None):
        return receive_packet(self.sock, deadline or time.monotonic() + PACKET_TIMEOUT)

    def message(self, header, body):
        message = parse_publish(header, body)
        if message.qos:
            self.sock.sendall(frame(0x40, struct.pack("!H", message.mid)))
        return message

    def subscribe(self, topics):
        require(topics and len(topics) == len(set(topics)), "Duplicate or empty probe topics")
        self.mid += 1
        body = struct.pack("!H", self.mid)
        body += b"".join(mqtt_string(topic) + b"\x01" for topic in topics)
        # The broker processes the PING only after completing retained replay.
        self.sock.sendall(frame(0x82, body) + frame(0xC0))
        messages = []
        acknowledged = False
        deadline = time.monotonic() + PACKET_TIMEOUT
        for _ in range(2000):
            header, body = self.packet(deadline)
            if header == 0x90:
                require(
                    body == struct.pack("!H", self.mid) + b"\x01" * len(topics),
                    f"Unexpected SUBACK: {body!r}",
                )
                acknowledged = True
            elif header >> 4 == 3:
                messages.append(self.message(header, body))
            elif header == 0xD0:
                require(acknowledged and not body, "PINGRESP before successful SUBACK")
                return messages
            else:
                raise SmokeFailure(f"Unexpected probe packet 0x{header:02x}")
        raise SmokeFailure("Probe packet limit exceeded")

    def next_message(self, topic, timeout=REFRESH_LIMIT):
        deadline = time.monotonic() + timeout
        for _ in range(2000):
            header, body = self.packet(deadline)
            require(header >> 4 == 3, f"Unexpected subscriber packet 0x{header:02x}")
            message = self.message(header, body)
            if message.topic == topic and not message.retain:
                return message
        raise SmokeFailure("Live subscriber packet limit exceeded")

    def publish(self, topic, payload, retain=True, qos=1):
        self.mid += 1
        body = mqtt_string(topic)
        if qos:
            body += struct.pack("!H", self.mid)
        self.sock.sendall(frame(0x30 | (qos << 1) | int(retain), body + payload))
        if qos:
            require(
                self.packet() == (0x40, struct.pack("!H", self.mid)),
                "Probe PUBLISH was not acknowledged",
            )


def replay(broker, topics):
    with Probe(broker) as subscriber:
        messages = subscriber.subscribe(topics)
    retained = {}
    for message in messages:
        if message.retain:
            require(message.topic not in retained, f"Duplicate retained replay: {message.topic}")
            retained[message.topic] = message
    broker.check()
    return retained


def broker_self_check(broker):
    """Wire-level checks of the test broker, independent of the C++ publisher."""
    first, second = "wire-smoke/self/first", "wire-smoke/self/second"
    will_topic = "wire-smoke/self/will"
    try:
        with Probe(broker) as seed:
            seed.publish(first, b"old")
            seed.publish(second, b"unrelated")
            seed.publish(first, b"live", retain=False)
            seed.publish(second, b"", retain=False)
        retained = replay(broker, [first, second])
        require(retained[first].payload == b"old", "Broker erased retained data on non-retained update")
        require(retained[second].payload == b"unrelated", "Non-retained empty payload erased data")
        with Probe(broker) as seed:
            seed.publish(first, b"")
        retained = replay(broker, [first, second])
        require(set(retained) == {second}, "Retained deletion did not affect exactly one topic")
        with Probe(broker) as subscriber:
            subscriber.subscribe([will_topic])
            with Probe(broker, Message(will_topic, OFFLINE, 1, True)) as doomed:
                doomed.close(abrupt=True)
            message = subscriber.next_message(will_topic, PACKET_TIMEOUT)
            require(message.payload == OFFLINE and message.qos == 1, "Last Will was not delivered")
        require(replay(broker, [will_topic])[will_topic].payload == OFFLINE, "Last Will was not retained")
        with Probe(broker) as seed:
            seed.publish(second, b"")
            seed.publish(will_topic, b"")
        require(not replay(broker, [first, second, will_topic]), "Broker self-check left retained records")
        broker.check()
    except (KeyError, EOFError, OSError) as error:
        raise SmokeFailure(f"Broker self-check failed: {error}") from error


def client_command(binary, port, polling=100, refresh=2, expiry=6, updates=False, system=False):
    command = [
        str(binary), "--server", "127.0.0.1", "--port", str(port),
        "--username", "test", "--password", "test", "--update" if updates else "--no-update",
        "--polling-rate", str(polling), "--power-refresh-interval", str(refresh),
        "--power-expire-after", str(expiry),
    ]
    if updates:
        command += ["--update-interval", "1"]
    if not system:
        command += ["--no-system-metrics"]
    return command


def cli_rejections(binary, root):
    # No broker or fake hardware exists here: validation must precede startup.
    directory = root / "cli-rejections"
    directory.mkdir()
    cases = [
        ("polling-zero", ["--polling-rate", "0"], ("--polling-rate", "range")),
        ("refresh-zero", ["--power-refresh-interval", "0"], ("--power-refresh-interval", "range")),
        ("expiry-too-short", ["--power-expire-after", "5"], ("freshness", "expiry", "refresh")),
        ("polling-too-slow", ["--polling-rate", "2001"], ("freshness", "polling", "refresh")),
        ("system-polling-zero", ["--system-polling-interval", "0"], ("--system-polling-interval", "range")),
        ("system-refresh-zero", ["--system-refresh-interval", "0"], ("--system-refresh-interval", "range")),
        ("system-expiry-short", ["--system-expire-after", "179"], ("system", "expiry", "refresh")),
        ("system-polling-slow", ["--system-polling-interval", "61"], ("system", "sampling", "refresh")),
        ("system-empty-root", ["--system-proc-root", ""], ("system", "proc", "empty")),
    ]
    for name, overrides, expected in cases:
        command = client_command(binary, 0)
        for option, value in zip(overrides[::2], overrides[1::2]):
            if option in command:
                command[command.index(option) + 1] = value
            else:
                command += [option, value]
        path = directory / f"{name}.log"
        with path.open("wb") as output:
            process = subprocess.Popen(
                command,
                cwd=directory, env=dict(os.environ, HOME=str(directory), TMPDIR=str(directory)),
                stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
            )
            try:
                try:
                    result = process.wait(timeout=2.0)
                except subprocess.TimeoutExpired as error:
                    raise SmokeFailure(f"CLI {name} did not reject within two seconds") from error
            finally:
                if process.poll() is None:
                    process.kill()
                    try:
                        process.wait(timeout=1.0)
                    except subprocess.TimeoutExpired as error:
                        raise SmokeFailure(f"CLI {name} child did not stop after SIGKILL") from error
        with path.open("rb") as output:
            log = output.read(16384).decode("utf-8", errors="replace")
        require(result > 0, f"CLI {name} did not reject cleanly (exit {result}): {log}")
        require(
            all(word in log.lower() for word in expected),
            f"CLI {name} did not report the expected validation error: {log}",
        )
        require("Starting MQTT client" not in log, f"CLI {name} reached hardware/network startup: {log}")
    print("PASS: no-broker CLI rejection of invalid power/system freshness and empty proc root", flush=True)


@dataclass(frozen=True)
class ProcessStatus:
    parent: int
    started: int
    state: str


def process_status(pid):
    try:
        stat = Path(f"/proc/{pid}/stat").read_text(encoding="ascii")
    except FileNotFoundError:
        return None
    # The command name may contain spaces or parentheses; field 3 follows its final ')'.
    fields = stat.rsplit(")", 1)[1].split()
    return ProcessStatus(int(fields[1]), int(fields[19]), fields[0])


class Harness:
    def __init__(self, binary, root, broker):
        self.binary = binary
        self.root = root
        self.broker = broker
        self.deadline = time.monotonic() + OVERALL_TIMEOUT
        self.process = None
        self.log_file = None
        self.log_paths = []
        self.configs = {}
        self.power = {}
        self.channels = {}
        self.shared = ""
        self.epoch = None
        self.completed = []
        self.update_marker = None
        self.downloader = None
        (root / "etc/persistent/cfg").mkdir(parents=True)
        (root / "proc/power").mkdir(parents=True)
        (root / "etc/board.info").write_text(
            "board.name=MQTT Wire Smoke Board\nboard.shortname=wire-smoke\nboard.sysid=e648\n",
            encoding="ascii",
        )
        (root / "etc/version").write_text("MF.wire-smoke\n", encoding="ascii")
        config = "".join(
            f"port.{port - 1}.sensorId=wire-sensor-{port}\n"
            f"port.{port - 1}.label={label}\n"
            for port, label in LABELS.items()
        )
        (root / "etc/persistent/cfg/config_file").write_text(config, encoding="ascii")
        for port in LABELS:
            self.write("active_pwr", port, "0" if port == 2 else "100")
            self.write("i_rms", port, "0" if port == 2 else "0.8333")
            self.write("v_rms", port, "120")
            self.write("relay", port, "0")

    def write(self, measurement, port, value):
        path = self.root / "proc/power" / f"{measurement}{port}"
        if value is None:
            path.unlink()
        else:
            staging = path.with_suffix(".new")
            staging.write_text(value + "\n", encoding="ascii")
            staging.replace(path)

    def start(self, update_bin=None):
        require(self.process is None or self.process.poll() is not None, "Publisher already running")
        if self.log_file:
            self.log_file.close()
        path = self.root / f"publisher-{len(self.log_paths) + 1}.log"
        self.log_paths.append(path)
        self.log_file = path.open("wb")
        self.broker.set_hold(
            lambda peer, message: peer.publisher and availability(message.payload, "offline")
        )
        environment = dict(os.environ, HOME=str(self.root), TMPDIR=str(self.root))
        if update_bin is not None:
            require(self.update_marker is not None, "Update fixture PID marker is not configured")
            environment.update(PATH=str(update_bin), MFI_UPDATE_TEST_PID=str(self.update_marker))
        self.process = subprocess.Popen(
            self.command(updates=update_bin is not None),
            cwd=self.root, env=environment, stdin=subprocess.DEVNULL,
            stdout=self.log_file, stderr=subprocess.STDOUT,
        )

    def command(self, updates=False):
        return client_command(self.binary, self.broker.port, updates=updates)

    def check(self, allow_exit=False):
        self.broker.check()
        require(time.monotonic() < self.deadline, "Overall 80-second test deadline exceeded")
        if self.process and not allow_exit:
            require(
                self.process.poll() is None,
                f"Publisher exited unexpectedly with code {self.process.returncode}; "
                "check the executable's CLI flags and log below",
            )

    def wait(self, predicate, description, timeout=3.0, allow_exit=False):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while True:
            self.check(allow_exit)
            result = predicate()
            if result:
                return result
            remaining = deadline - time.monotonic()
            require(remaining > 0, f"Timed out waiting for {description} ({timeout:g}s)")
            with self.broker.condition:
                self.broker.condition.wait(min(0.05, remaining))

    def observe(self, duration, allow_exit=False):
        deadline = min(self.deadline, time.monotonic() + duration)
        while time.monotonic() < deadline:
            self.check(allow_exit)
            with self.broker.condition:
                self.broker.condition.wait(min(0.05, max(0, deadline - time.monotonic())))

    def publications(self, topic=None, since=0, epoch=None):
        connection = (epoch or self.epoch).number
        return [
            event for event in self.broker.snapshot(since)
            if event.kind == "publish" and event.connection == connection
            and (topic is None or event.message.topic == topic)
        ]

    def state(self, topic, value, since=0, timeout=PROMPT_LIMIT):
        return self.wait(
            lambda: next(
                (event for event in self.publications(topic, since)
                 if json_object(event.message.payload) == {"value": value}), None,
            ),
            f"{topic} = {value!r}", timeout,
        )

    def gate(self, topic, value, since=0, timeout=3.0, allow_exit=False):
        return self.wait(
            lambda: next(
                (event for event in self.publications(topic, since)
                 if availability(event.message.payload, value)), None,
            ),
            f"{topic} {value}", timeout, allow_exit,
        )

    def ack(self, publication):
        return next(
            (
                event for event in self.broker.snapshot(publication.seq + 1)
                if event.kind == "puback_sent" and event.connection == publication.connection
                and event.message.mid == publication.message.mid
                and event.message.topic == publication.message.topic
                and event.message.payload == publication.message.payload
            ),
            None,
        )

    def discover(self):
        expected = {
            f"{label} {kind}" for label in LABELS.values()
            for kind in ("Power", "Current", "Voltage", "Relay")
        }

        def received():
            found = {}
            for event in self.publications():
                if event.message.topic.startswith("homeassistant/") and event.message.topic.endswith("/config"):
                    config = json_object(event.message.payload)
                    name = config.get("name")
                    require(name in expected, f"Unexpected discovery name {name!r}")
                    require(name not in found, f"Duplicate discovery publication for {name}")
                    require(event.message.qos == 1 and event.message.retain, "Discovery must be retained QoS 1")
                    found[name] = (event.message.topic, config)
            return found if set(found) == expected else None

        discovered = self.wait(received, "all 32 fake-port discovery messages")
        if self.configs:
            require(discovered == self.configs, "Discovery identities or payloads changed on reconnect")
        self.configs = discovered
        require(len({value[0] for value in discovered.values()}) == 32, "Duplicate discovery topics")
        require(len({value[1]["unique_id"] for value in discovered.values()}) == 32, "Duplicate unique IDs")
        state_topics = [value[1]["state_topic"] for value in discovered.values()]
        require(len(set(state_topics)) == 32, "Duplicate state topics")
        will = self.epoch.will
        require(will is not None and will.qos == 1 and will.retain, "Missing retained QoS 1 Last Will")
        require(availability(will.payload, "offline"), "Last Will is not JSON offline")
        self.shared = will.topic
        for port, label in LABELS.items():
            for kind, unit, device_class in (
                ("Power", "W", "power"), ("Current", "A", "current"),
                ("Voltage", "V", "voltage"), ("Relay", None, None),
            ):
                config = discovered[f"{label} {kind}"][1]
                require(config.get("unique_id"), f"Missing unique_id: {label} {kind}")
                state_topic = config.get("state_topic")
                require(
                    isinstance(state_topic, str) and state_topic
                    and "+" not in state_topic and "#" not in state_topic,
                    f"Invalid state topic: {state_topic!r}",
                )
                if unit:
                    for field, value in (
                        ("unit_of_measurement", unit), ("device_class", device_class),
                        ("state_class", "measurement"), ("suggested_display_precision", 4),
                        ("value_template", "{{ value_json.value }}"),
                    ):
                        require(config.get(field) == value, f"{label} {kind}: {field} != {value!r}")
                if kind == "Power":
                    require(config.get("expire_after") == 6, "Power expire_after is not 6 seconds")
                    require(config.get("availability_mode") == "all", "Power availability_mode is not all")
                    require(
                        "availability_topic" not in config and "availability_template" not in config,
                        "Power discovery mixes list and single-topic availability",
                    )
                    entries = config.get("availability")
                    require(isinstance(entries, list) and len(entries) == 2, "Power needs two availability gates")
                    topics = []
                    for entry in entries:
                        require(isinstance(entry, dict), "Availability entry is not an object")
                        require(
                            entry.get("value_template") == "{{ value_json.availability }}",
                            "Availability entry does not decode JSON availability",
                        )
                        require(entry.get("payload_available", "online") == "online", "Incorrect available payload")
                        require(entry.get("payload_not_available", "offline") == "offline", "Incorrect offline payload")
                        topics.append(entry["topic"])
                    require(self.shared in topics and len(set(topics)) == 2, "Missing shared or unique channel gate")
                    self.channels[port] = next(topic for topic in topics if topic != self.shared)
                    self.power[port] = state_topic
                else:
                    require("expire_after" not in config and "availability" not in config, "Non-power policy changed")
                    require(config.get("availability_topic") == self.shared, "Non-power shared availability changed")
                    require(
                        config.get("availability_template") == "{{ value_json.availability }}",
                        "Non-power availability template changed",
                    )
                    if kind == "Relay":
                        require(config.get("command_topic"), "Relay command_topic missing")
                        require(json_object(config["payload_on"]) == {"value": "ON"}, "Relay ON payload changed")
                        require(json_object(config["payload_off"]) == {"value": "OFF"}, "Relay OFF payload changed")
        require(len(set(self.channels.values())) == 8, "Power channels do not have independent gates")
        require(not set(self.channels.values()) & set(state_topics), "A validity gate reuses a numeric topic")

    def settle_epoch(self, previous=0, failed=(), expected=None):
        self.epoch = self.wait(
            lambda: next((peer for peer in self.broker.publishers() if peer.number > previous), None),
            "publisher CONNECT", timeout=7.0,
        )
        require(self.epoch.will is not None, "Publisher CONNECT did not configure a Last Will")
        self.shared = self.epoch.will.topic
        shared_offline = self.gate(self.shared, "offline")
        self.observe(0.25)
        require(
            not any(availability(event.message.payload, "online") for event in self.publications(self.shared)),
            "Shared online escaped unacknowledged shared offline",
        )
        # Permit implementations that serialize shared-offline and channel setup.
        self.broker.set_hold(
            lambda peer, message: peer.publisher and message.topic != self.shared
            and availability(message.payload, "offline")
        )
        self.broker.release(lambda peer, message: message.topic == self.shared)
        self.discover()
        offline = {topic: self.gate(topic, "offline") for topic in [self.shared, *self.channels.values()]}
        require(offline[self.shared] == shared_offline, "Shared offline initialization changed unexpectedly")
        self.observe(0.25)
        require(
            not any(self.publications(topic) for topic in self.power.values()),
            "Power published before its initial offline gate was acknowledged",
        )
        require(
            not any(availability(event.message.payload, "online") for event in self.publications(self.shared)),
            "Shared online escaped unacknowledged initial offline gates",
        )
        self.broker.set_hold(lambda peer, message: False)
        self.broker.release()
        samples, online = {}, {}
        expected = expected or {port: 0 if port == 2 else 100 for port in LABELS}
        for port in LABELS:
            if port in failed:
                require(not self.publications(self.power[port]), f"Failed port {port} replayed cached power")
                continue
            samples[port] = self.state(self.power[port], expected[port])
            online[port] = self.gate(self.channels[port], "online", samples[port].seq)
            self.wait(lambda port=port: self.ack(online[port]), f"port {port} online PUBACK")
        shared_online = self.gate(self.shared, "online")
        for topic, event in offline.items():
            require(event.message.qos == 1 and event.message.retain, f"Initial offline is not retained QoS 1: {topic}")
            acknowledged = self.ack(event)
            require(acknowledged and acknowledged.seq < shared_online.seq, "Shared online precedes offline PUBACK")
        for port, sample in samples.items():
            require(self.ack(offline[self.channels[port]]).seq < sample.seq, "Sample preceded its offline PUBACK")
            require(sample.seq < online[port].seq, "Channel online preceded a new numeric sample")
            require(self.ack(online[port]).seq < shared_online.seq, "Shared online preceded channel online PUBACK")
        for port in failed:
            require(not self.publications(self.power[port]), f"Failed port {port} replayed cached power on reconnect")
        self.wait(lambda: self.ack(shared_online), "shared online PUBACK")
        return shared_online

    def passed(self, description):
        self.completed.append(description)
        print(f"PASS: {description}", flush=True)

    def refresh_and_retention(self):
        for port in (1, 2):
            self.wait(
                lambda port=port: len(self.publications(self.power[port])) >= 3,
                f"three unchanged port {port} samples", timeout=2 * REFRESH_LIMIT,
            )
            samples = self.publications(self.power[port])
            expected = 100 if port == 1 else 0
            for sample in samples:
                require(watts(sample.message.payload) == expected, "Unchanged power unexpectedly changed")
            for before, after in zip(samples, samples[1:]):
                gap = after.when - before.when
                require(
                    REFRESH - 0.2 <= gap <= REFRESH_LIMIT,
                    f"Port {port} unchanged refresh gap {gap:.3f}s; expected 1.8-{REFRESH_LIMIT}s",
                )
        all_topics = [topic for topic, _ in self.configs.values()]
        all_topics += [config["state_topic"] for _, config in self.configs.values()]
        all_topics += [self.shared, *self.channels.values()]
        retained = replay(self.broker, all_topics)
        for port, label in LABELS.items():
            require(self.power[port] not in retained, "Fresh power state was retained")
            require(availability(retained[self.channels[port]].payload, "online"), "Healthy channel not retained online")
            for kind, value in (("Current", 0 if port == 2 else 0.8333), ("Voltage", 120), ("Relay", "OFF")):
                topic = self.configs[f"{label} {kind}"][1]["state_topic"]
                event = self.state(topic, value)
                require(event.message.qos == 0 and event.message.retain, f"{kind} lost retained QoS 0 policy")
                require(json_object(retained[topic].payload) == {"value": value}, f"{kind} retained state missing")
        for topic, config in self.configs.values():
            require(json_object(retained[topic].payload) == config, "Discovery retained replay differs")
            require(retained[topic].qos == 1, "Discovery replay lost QoS 1")
        self.passed("100 W and 0 W refresh at 2 seconds; discovery and non-power retention")

    def migration(self):
        sentinels = {
            "homeassistant/switch/wire-smoke-unrelated/config": b'{"name":"unrelated relay"}',
            "wire-smoke/unrelated/relay/state": b'{"value":"ON"}',
        }
        preserved = [topic for topic, _ in self.configs.values()]
        preserved += [
            config["state_topic"] for _, config in self.configs.values()
            if config["state_topic"] not in self.power.values()
        ]
        preserved += [self.shared, *self.channels.values(), *sentinels]
        legacy = {self.power[1]: b'{"value":777}', self.power[2]: b'{"value":888}'}
        with Probe(self.broker) as operator:
            for topic, payload in {**sentinels, **legacy}.items():
                operator.publish(topic, payload)
        before = replay(self.broker, preserved)
        mark = self.broker.mark()
        self.state(self.power[1], 100, mark, REFRESH_LIMIT)
        self.state(self.power[2], 0, mark, REFRESH_LIMIT)
        retained = replay(self.broker, list(legacy))
        require(
            {topic: message.payload for topic, message in retained.items()} == legacy,
            "Non-retained telemetry erased legacy retained records (or client auto-purged them)",
        )
        with Probe(self.broker) as operator:
            # Only these exact, discovered numeric topics are in the cleanup allowlist.
            for topic in legacy:
                operator.publish(topic, b"")
        after = replay(self.broker, [*preserved, *legacy])
        require(not set(legacy) & set(after), "Exact-topic migration left retained numeric state")
        require(
            {topic: message.payload for topic, message in before.items()}
            == {topic: message.payload for topic, message in after.items()},
            "Numeric migration changed unrelated discovery, relay, measurement, or availability records",
        )
        self.passed("isolated exact-topic migration; non-retained updates preserve old records until explicit deletion")

    def transition(self, port, value):
        mark, started = self.broker.mark(), time.monotonic()
        self.write("active_pwr", port, str(value))
        event = self.state(self.power[port], value, mark)
        require(event.when - started <= PROMPT_LIMIT, "Power transition exceeded two seconds")
        return event

    def recover(self, port, value):
        mark = self.broker.mark()
        sample = self.transition(port, value)
        online = self.gate(self.channels[port], "online", mark)
        require(sample.seq < online.seq, "Recovery online preceded accepted current sample")
        self.wait(lambda: self.ack(online), "recovery online PUBACK")
        return sample

    def assert_invalid_interval(self, port, offline, end=None):
        events = self.publications(self.power[port], offline.seq + 1)
        if end is not None:
            events = [event for event in events if event.seq < end]
        require(not events, f"Invalid port {port} emitted numeric state after offline: {events[:2]}")
        gates = self.publications(self.channels[port], offline.seq + 1)
        if end is not None:
            gates = [event for event in gates if event.seq < end]
        require(not gates, f"Invalid channel {port} emitted repeated or online validity transitions")

    def invalid_readings(self):
        self.transition(1, 0)
        self.transition(1, 100)
        faults = (
            ("missing", None), ("empty", ""), ("malformed", "not-a-number"),
            ("trailing junk", "100 watts"), ("negative", "-1"),
            ("negative below quantization", "-0.00001"), ("NaN", "nan"),
            ("infinity", "inf"), ("parser overflow", "1e9999"),
            ("finite quantization overflow", "1e305"),
        )
        healthy_value = 100
        for name, text in faults:
            mark = self.broker.mark()
            self.write("active_pwr", 1, text)
            healthy_value = 0 if healthy_value else 100
            self.write("active_pwr", 8, str(healthy_value))
            offline = self.gate(self.channels[1], "offline", mark, PROMPT_LIMIT)
            self.state(self.power[8], healthy_value, mark)
            self.wait(lambda: self.ack(offline), f"{name} offline PUBACK")
            if name == "missing":
                self.write("i_rms", 1, "0.7")
                self.write("v_rms", 1, "119")
                self.state(self.configs[f"{LABELS[1]} Current"][1]["state_topic"], 0.7, mark)
                self.state(self.configs[f"{LABELS[1]} Voltage"][1]["state_topic"], 119, mark)
                self.observe(REFRESH + 0.3)
            else:
                self.observe(0.3)
            for event in self.publications(self.power[1], mark):
                require(
                    event.seq < offline.seq and watts(event.message.payload) == 100,
                    f"{name} became a numeric substitute: {event.message.payload!r}",
                )
            self.assert_invalid_interval(1, offline)
            retained = replay(self.broker, [self.shared, self.channels[1], self.power[1]])
            require(availability(retained[self.shared].payload, "online"), f"{name} took healthy transport offline")
            require(availability(retained[self.channels[1]].payload, "offline"), f"{name} did not retain invalidity")
            require(self.power[1] not in retained, f"{name} retained numeric power")
            recovery_start = self.broker.mark()
            self.recover(1, 100)
            self.assert_invalid_interval(1, offline, recovery_start)
        self.write("i_rms", 1, "0.8333")
        self.write("v_rms", 1, "120")
        self.passed("prompt zero; ten invalid inputs stay nonnumeric, isolate port 8, and recover at the same value")

    def other_measurements_and_relay(self):
        for measurement, invalid, value in (("i_rms", None, 0), ("v_rms", "bad", 100)):
            mark = self.broker.mark()
            self.write(measurement, 1, invalid)
            self.transition(1, value)
            self.observe(0.25)
            require(
                not any(availability(event.message.payload, "offline")
                        for event in self.publications(self.channels[1], mark)),
                f"{measurement} failure invalidated valid power",
            )
            self.write(measurement, 1, "0.8333" if measurement == "i_rms" else "120")
        self.relay_command(8, True)
        self.relay_command(8, False)
        self.passed("current/voltage failures do not block power; discovered relay commands still control fake files")

    def relay_command(self, port, on):
        config = self.configs[f"{LABELS[port]} Relay"][1]
        value, raw, field = ("ON", "1", "payload_on") if on else ("OFF", "0", "payload_off")
        mark, started = self.broker.mark(), time.monotonic()
        with Probe(self.broker) as controller:
            controller.publish(config["command_topic"], config[field].encode("utf-8"), retain=False, qos=0)
        deadline = started + PROMPT_LIMIT
        self.wait(
            lambda: (self.root / f"proc/power/relay{port}").read_text(encoding="ascii").strip() == raw,
            f"fake relay {port} file = {raw}", max(0, deadline - time.monotonic()),
        )
        event = self.state(config["state_topic"], value, mark, max(0, deadline - time.monotonic()))
        require(event.when <= deadline, f"Relay {port} command exceeded two seconds")
        require(event.message.qos == 0 and event.message.retain, "Relay control changed state publication policy")

    def late_subscriber_and_reconnect(self):
        with Probe(self.broker) as receiver:
            receiver.subscribe([self.power[1], self.channels[1], self.shared])
            previous = receiver.next_message(self.power[1])
            require(watts(previous.payload) == 100, "Late-subscriber fixture lacks a prior numeric state")
        mark = self.broker.mark()
        self.write("active_pwr", 1, None)
        offline = self.gate(self.channels[1], "offline", mark)
        self.wait(lambda: self.ack(offline), "missed channel invalidation PUBACK")

        def check_restored_receiver():
            retained = replay(self.broker, [self.power[1], self.channels[1], self.shared])
            require(self.power[1] not in retained, "New subscriber received retained numeric power")
            require(availability(retained[self.shared].payload, "online"), "Shared transport is not online")
            require(availability(retained[self.channels[1]].payload, "offline"), "Late subscriber missed durable offline")
            restored_value = watts(previous.payload)
            combined_online = all(
                availability(retained[topic].payload, "online")
                for topic in (self.shared, self.channels[1])
            )
            require(restored_value == 100 and not combined_online, "Shared online resurrected restored failed-channel state")

        check_restored_receiver()
        old = self.epoch
        self.broker.set_hold(
            lambda peer, message: peer.publisher and availability(message.payload, "offline")
        )
        self.broker.drop(old)
        self.write("active_pwr", 8, "0")
        self.wait(
            lambda: any(event.kind == "will" and event.connection == old.number
                        for event in self.broker.snapshot(mark)),
            "publisher Last Will after network loss",
        )
        expected = {port: 0 if port in (2, 8) else 100 for port in LABELS}
        self.settle_epoch(previous=old.number, failed=(1,), expected=expected)
        self.observe(REFRESH + 0.3)
        require(not self.publications(self.power[1]), "Reconnect replayed cached failed-channel power")
        for event in self.publications(self.power[8]):
            require(watts(event.message.payload) == 0, "Reconnect replayed an old healthy-channel sample")
        check_restored_receiver()
        self.recover(1, 100)
        self.passed("late subscriber retains offline over saved watts; reconnect gates a new epoch without cached power")

    def broker_restart(self):
        mark = self.broker.mark()
        self.write("active_pwr", 1, None)
        offline = self.gate(self.channels[1], "offline", mark)
        self.wait(lambda: self.ack(offline), "pre-restart failed-channel offline PUBACK")
        self.shutdown(acknowledge=True)
        with self.broker.condition:
            topics = list(self.broker.retained)
        require(not set(self.power.values()) & set(topics), "Migration left numeric topics before broker restart")
        before = replay(self.broker, topics)
        expected = {topic: (message.payload, message.qos) for topic, message in before.items()}
        snapshot = self.root / "broker-retained.json"
        self.broker.save_retained(snapshot)
        self.audit_wire()
        self.broker.close()
        self.broker = Broker(retained_path=snapshot)

        # The executable is still stopped: these records cannot be recreated by it.
        absent = replay(self.broker, [*topics, *self.power.values()])
        require(
            {topic: (message.payload, message.qos) for topic, message in absent.items()} == expected,
            "Broker restart did not preserve exact retained payloads/QoS or resurrected deleted numeric topics",
        )
        require(not self.broker.publishers(), "Publisher connected before persistence-only replay check")
        require(availability(absent[self.channels[1]].payload, "offline"), "Restart lost retained channel offline")
        require(availability(absent[self.shared].payload, "offline"), "Restart lost retained shared shutdown offline")

        self.start()
        self.settle_epoch(
            failed=(1,),
            expected={port: 0 if port in (2, 8) else 100 for port in LABELS},
        )
        self.observe(0.3)
        late = replay(self.broker, [*topics, *self.power.values()])
        require(not set(self.power.values()) & set(late), "Post-restart late receiver got retained numeric power")
        require(availability(late[self.shared].payload, "online"), "Post-restart transport did not become online")
        require(availability(late[self.channels[1]].payload, "offline"), "Shared online resurrected the failed channel")
        require(not self.publications(self.power[1]), "Post-restart publisher emitted failed-channel numeric power")
        gates = {self.shared, *self.channels.values()}
        for topic in set(topics) - gates:
            require(
                (late[topic].payload, late[topic].qos) == expected[topic],
                f"Broker restart changed preserved discovery/relay/measurement record: {topic}",
            )
        self.recover(1, 100)
        self.passed("broker snapshot/restart preserves retained records and deleted numeric topics; late failed channel stays offline")

    def shutdown(self, acknowledge, update_pending=False):
        current = self.epoch
        mark = self.broker.mark()
        self.broker.set_hold(
            lambda peer, message: peer.number == current.number
            and message.topic == self.shared and availability(message.payload, "offline")
        )
        started = time.monotonic()
        self.process.send_signal(signal.SIGTERM)
        offline = self.gate(
            self.shared, "offline", mark, 6.0 if update_pending else PROMPT_LIMIT, allow_exit=True,
        )
        require(offline.message.qos == 1 and offline.message.retain, "Shutdown offline is not retained QoS 1")
        self.observe(0.35, allow_exit=True)
        require(self.process.poll() is None, "SIGTERM exited without waiting for offline PUBACK")
        require(
            not any(event.kind == "disconnect" and event.connection == current.number
                    for event in self.broker.snapshot(mark)),
            "DISCONNECT preceded withheld offline PUBACK",
        )
        if acknowledge:
            self.broker.set_hold(lambda peer, message: False)
            self.broker.release()
        # Update termination permits separate 5s offline and 5s child-cleanup budgets.
        exit_timeout = (
            max(0, started + 10.8 - time.monotonic()) if update_pending
            else 2.5 if acknowledge else 6.5
        )
        self.wait(
            lambda: self.process.poll() is not None, "bounded SIGTERM exit",
            timeout=exit_timeout, allow_exit=True,
        )
        elapsed = time.monotonic() - started
        require(not update_pending or elapsed <= 10.8, "Update cancellation exceeded separate shutdown/cleanup budgets")
        self.wait(
            lambda: current.closed, "publisher socket close", timeout=1.0, allow_exit=True,
        )
        events = [event for event in self.broker.snapshot(mark) if event.connection == current.number]
        disconnects = [event for event in events if event.kind == "disconnect"]
        if acknowledge:
            ack = self.ack(offline)
            require(ack and disconnects and ack.seq < disconnects[0].seq, "No offline PUBACK before DISCONNECT")
            require(not any(event.kind == "will" for event in events), "Orderly shutdown unexpectedly fired Last Will")
            require(self.process.returncode == 0, f"Orderly shutdown returned {self.process.returncode}")
        else:
            require(4.5 <= elapsed <= 6.8, f"Unacknowledged shutdown took {elapsed:.3f}s, expected about five seconds")
            require(self.ack(offline) is None, "Test accidentally acknowledged withheld shutdown offline")
            require(not disconnects or disconnects[0].when - started >= 4.5, "Failed shutdown disconnected too early")
            log = self.log_paths[-1].read_text(encoding="utf-8", errors="replace")
            reported = re.search(r"offline[^\n]*(?:not acknowledged|fail|timeout|timed out|deadline)", log, re.I)
            require(self.process.returncode != 0 or reported, "Failed offline delivery was silently reported as success")
        require(
            not any(peer.number > current.number for peer in self.broker.publishers()),
            "Publisher reconnected after SIGTERM",
        )
        retained = replay(self.broker, [self.shared, *self.power.values()])
        require(availability(retained[self.shared].payload, "offline"), "Shutdown did not retain shared offline")
        require(not set(self.power.values()) & set(retained), "Shutdown left retained power")
        self.passed(
            "SIGTERM waits for retained offline PUBACK before DISCONNECT"
            if acknowledge else f"withheld shutdown PUBACK fails explicitly within {elapsed:.2f}s"
        )
        return elapsed

    def capture_downloader(self):
        if self.downloader is not None:
            return True
        if self.update_marker is None or not self.update_marker.exists():
            return False
        marker = self.update_marker.read_text(encoding="ascii").strip()
        if not marker:
            return False
        require(marker.isdecimal() and int(marker) > 1, "Invalid owned downloader PID marker")
        pid = int(marker)
        before = process_status(pid)
        if before is None:
            return False
        require(before.parent == self.process.pid, "Downloader PID is not a direct child of the owned client")
        try:
            handle = os.pidfd_open(pid)
        except ProcessLookupError:
            return False
        claimed = False
        try:
            after = process_status(pid)
            if after is None:
                return False
            require(
                after.parent == before.parent and after.started == before.started,
                "Downloader identity changed while opening its pidfd",
            )
            self.downloader = (pid, before.started, handle)
            claimed = True
            return True
        finally:
            if not claimed:
                os.close(handle)

    def downloader_status(self):
        require(self.downloader is not None, "Downloader identity was not captured")
        pid, started, _ = self.downloader
        status = process_status(pid)
        return status if status is not None and status.started == started else None

    def blocked_update(self):
        require(
            hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal"),
            "Blocked-update acceptance requires Linux pidfd support for identity-safe cleanup",
        )
        # Fail before starting the updater if this kernel cannot provide safe process handles.
        handle = os.pidfd_open(os.getpid())
        os.close(handle)
        directory = self.root / "blocked-update"
        bin_directory = directory / "bin"
        bin_directory.mkdir(parents=True)
        self.update_marker = directory / "downloader.pid"
        shim = bin_directory / "wget"
        script = '#!/bin/sh\nprintf "%s\\n" "$$" > "$MFI_UPDATE_TEST_PID"\nexec /bin/sleep 300\n'
        shim.write_text(script, encoding="ascii")
        shim.chmod(0o700)
        previous = self.epoch.number
        self.start(update_bin=bin_directory)
        self.wait(self.capture_downloader, "fixture wget PID marker", timeout=4.0)
        pid, _, _ = self.downloader

        def sleeping():
            status = self.downloader_status()
            require(status is not None and status.state != "Z", "Blocking downloader exited prematurely")
            require(status.parent == self.process.pid, "Blocking downloader escaped its owned parent")
            return Path(f"/proc/{pid}/cmdline").read_bytes() == b"/bin/sleep\x00300\x00"

        self.wait(sleeping, "fixture wget exec of non-networking sleep")
        ready = self.settle_epoch(
            previous=previous,
            expected={port: 0 if port in (2, 8) else 100 for port in LABELS},
        )
        mark, started = self.broker.mark(), time.monotonic()
        self.relay_command(8, True)
        self.relay_command(8, False)
        for port, expected in ((1, 100), (2, 0)):
            self.wait(
                lambda port=port: len(self.publications(self.power[port], mark)) >= 2,
                f"unchanged port {port} refreshes while update preparation blocks",
                timeout=max(0, started + 2 * REFRESH_LIMIT - time.monotonic()),
            )
            samples = self.publications(self.power[port], mark)
            require(samples[0].when - started <= REFRESH_LIMIT, "Blocked updater delayed first power refresh")
            for event in samples:
                require(watts(event.message.payload) == expected, "Power changed during blocked preparation")
            for before, after in zip(samples, samples[1:]):
                gap = after.when - before.when
                require(
                    REFRESH - 0.2 <= gap <= REFRESH_LIMIT,
                    f"Blocked updater changed port {port} refresh gap to {gap:.3f}s",
                )
        require(sleeping(), "Downloader was not still blocked throughout the responsiveness checks")
        require(
            not any(availability(event.message.payload, "offline")
                    for event in self.publications(self.shared, ready.seq + 1)),
            "Update preparation took shared availability offline",
        )
        require(self.epoch == self.broker.publishers()[-1], "Client reconnected during blocked preparation")
        retained = replay(self.broker, [self.shared])
        require(availability(retained[self.shared].payload, "online"), "Blocked preparation did not retain shared online")
        elapsed = self.shutdown(acknowledge=True, update_pending=True)
        self.wait(
            lambda: self.downloader_status() is None, "owned downloader reaped after cancellation",
            timeout=1.0, allow_exit=True,
        )
        require(shim.read_text(encoding="ascii") == script, "Downloader shim changed before client exit")
        self.passed(
            f"actual client refreshes power and serves relays during blocked update; "
            f"SIGTERM cancels/reaps downloader in {elapsed:.2f}s"
        )

    def audit_wire(self):
        publisher_ids = {peer.number for peer in self.broker.publishers()}
        power_topics = set(self.power.values())
        config_topics = {topic for topic, _ in self.configs.values()}
        nonpower = {
            config["state_topic"] for _, config in self.configs.values()
            if config["state_topic"] not in power_topics
        }
        gates = {self.shared, *self.channels.values()}
        for event in self.broker.snapshot():
            if event.kind != "publish" or event.connection not in publisher_ids:
                continue
            message = event.message
            require(not message.dup, "Unexpected retransmission in promptly acknowledged test traffic")
            if message.topic in power_topics:
                watts(message.payload)
                require(message.qos == 0 and not message.retain, f"Power is not non-retained QoS 0: {message.topic}")
            elif message.topic in gates:
                require(message.qos == 1 and message.retain, f"Availability is not retained QoS 1: {message.topic}")
                require(
                    json_object(message.payload) in ({"availability": "online"}, {"availability": "offline"}),
                    f"Unexpected availability JSON: {message.payload!r}",
                )
            elif message.topic in config_topics:
                require(message.qos == 1 and message.retain, "Discovery is not retained QoS 1")
            elif message.topic in nonpower:
                require(message.qos == 0 and message.retain, "Non-power state policy changed")
            else:
                raise SmokeFailure(f"Unexpected publisher topic {message.topic!r}")

    def run(self):
        self.start()
        self.settle_epoch()
        self.passed("32 discovery documents and startup offline/sample/online PUBACK ordering")
        self.refresh_and_retention()
        self.migration()
        self.invalid_readings()
        self.other_measurements_and_relay()
        self.late_subscriber_and_reconnect()
        self.broker_restart()
        self.shutdown(acknowledge=False)
        self.blocked_update()
        self.audit_wire()
        self.broker.check()

    def diagnostics(self):
        print("--- recent publisher wire events ---", file=sys.stderr)
        publisher_ids = {peer.number for peer in self.broker.publishers()}
        events = [event for event in self.broker.snapshot() if event.connection in publisher_ids]
        for event in events[-24:]:
            message = event.message
            detail = (
                f" {message.topic} qos={message.qos} retain={int(message.retain)}"
                f" mid={message.mid} payload={message.payload[:200]!r}"
                if message else ""
            )
            print(f"{event.seq} {event.when:.3f} conn={event.connection} {event.kind}{detail}", file=sys.stderr)
        for path in self.log_paths:
            print(f"--- {path.name} (last 16 KiB) ---", file=sys.stderr)
            with path.open("rb") as stream:
                stream.seek(max(0, path.stat().st_size - 16384))
                print(stream.read().decode("utf-8", errors="replace"), file=sys.stderr)

    def close(self):
        try:
            if self.update_marker is not None:
                # Signal only the owned client; use a verified pidfd if failure cleanup
                # must kill the shim, keeping the parent alive long enough to reap it.
                if self.process and self.process.poll() is None:
                    self.process.send_signal(signal.SIGTERM)
                try:
                    if self.downloader is None:
                        self.capture_downloader()
                    if self.downloader is not None and self.downloader_status() is not None:
                        try:
                            signal.pidfd_send_signal(self.downloader[2], signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                finally:
                    if self.process and self.process.poll() is None:
                        try:
                            self.process.wait(timeout=2.0)
                        except subprocess.TimeoutExpired:
                            pass
        finally:
            self.close_process()

    def close_process(self):
        try:
            if self.process and self.process.poll() is None:
                self.process.kill()
                try:
                    self.process.wait(timeout=3.0)
                except subprocess.TimeoutExpired as error:
                    raise SmokeFailure("Owned publisher did not exit after SIGKILL") from error
        finally:
            if self.downloader is not None:
                os.close(self.downloader[2])
                self.downloader = None
            if self.log_file:
                self.log_file.close()


def main():
    if len(sys.argv) != 2:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    binary = Path(sys.argv[1])
    if not binary.is_absolute() or not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"FAIL: provide an absolute path to an executable local host binary: {binary}", file=sys.stderr)
        return 2
    started = time.monotonic()
    broker, harness = None, None
    status = 0

    def expired(signum, frame_info):
        raise SmokeFailure("Overall 80-second MQTT wire smoke deadline exceeded")

    old_handler = signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, OVERALL_TIMEOUT)
    try:
        with tempfile.TemporaryDirectory(prefix="mfi-mqtt-wire-") as directory:
            try:
                cli_rejections(binary, Path(directory))
                broker = Broker()
                broker_self_check(broker)
                harness = Harness(binary, Path(directory), broker)
                harness.run()
            except (SmokeFailure, OSError, EOFError, KeyError, TypeError, ValueError) as error:
                status = 1
                print(f"FAIL: {error}", file=sys.stderr)
                if harness:
                    harness.diagnostics()
            finally:
                signal.setitimer(signal.ITIMER_REAL, 0)
                try:
                    if harness:
                        harness.close()
                finally:
                    active_broker = harness.broker if harness else broker
                    if active_broker:
                        active_broker.close()
    except (SmokeFailure, OSError) as error:
        status = 1
        print(f"FAIL: cleanup/setup: {error}", file=sys.stderr)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old_handler)
    if status == 0:
        print(f"PASS: MQTT wire smoke completed in {time.monotonic() - started:.2f}s")
    return status


if __name__ == "__main__":
    sys.exit(main())
