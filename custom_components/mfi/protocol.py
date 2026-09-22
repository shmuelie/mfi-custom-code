"""Strict, transport-independent native mFi MQTT v1 contract."""

import json
import math
import re
from dataclasses import dataclass
from decimal import Decimal

ID_PATTERN = re.compile(r"[0-9a-f]{32}")
ROLES = ("power", "current", "voltage", "relay")
MAX_MEASUREMENT = {"power": Decimal("1e9"), "current": Decimal("1e6"), "voltage": Decimal("1e6")}
MAX_PAYLOAD = 65536


def identifier(value: object) -> str:
    if not isinstance(value, str) or ID_PATTERN.fullmatch(value) is None:
        raise ValueError("Expected a provisioned 32-character lowercase identifier")
    return value


def _object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def _invalid_number(value: str) -> None:
    raise ValueError(f"Invalid number: {value}")


def json_object(payload: str | bytes | bytearray) -> dict[str, object]:
    if len(payload.encode("utf-8") if isinstance(payload, str) else payload) > MAX_PAYLOAD:
        raise ValueError("Native MQTT payload exceeds the size limit")
    try:
        value = json.loads(
            payload,
            object_pairs_hook=_object,
            parse_float=Decimal,
            parse_constant=_invalid_number,
        )
    except (json.JSONDecodeError, UnicodeDecodeError, RecursionError) as error:
        raise ValueError("Invalid native MQTT JSON") from error
    if not isinstance(value, dict):
        raise ValueError("Native MQTT payload must be an object")
    return value


def _text(value: object, field: str) -> str:
    if not isinstance(value, str) or not value.strip() or len(value) > 256:
        raise ValueError(f"Invalid {field}")
    return value


@dataclass(frozen=True)
class Port:
    id: int
    name: str
    capabilities: tuple[str, ...]


@dataclass(frozen=True)
class Descriptor:
    device_id: str
    name: str
    manufacturer: str
    model_id: str
    model: str
    firmware_version: str
    publisher_version: str
    refresh_interval: int
    expire_after: int
    ports: tuple[Port, ...]

    @property
    def base_topic(self) -> str:
        return f"mfi/{self.device_id}"

    def unique_id(self, port: int, role: str) -> str:
        return f"{self.device_id}_port_{port}_{role}"

    def as_dict(self) -> dict[str, object]:
        return {
            "schema_version": 1,
            "device_id": self.device_id,
            "name": self.name,
            "manufacturer": self.manufacturer,
            "model_id": self.model_id,
            "model": self.model,
            "firmware_version": self.firmware_version,
            "publisher_version": self.publisher_version,
            "refresh_interval": self.refresh_interval,
            "expire_after": self.expire_after,
            "ports": [
                {"id": port.id, "name": port.name, "capabilities": list(port.capabilities)}
                for port in self.ports
            ],
        }


def parse_descriptor(payload: str | bytes | bytearray, topic: str | None = None) -> Descriptor:
    data = json_object(payload)
    if type(data.get("schema_version")) is not int or data["schema_version"] != 1:
        raise ValueError("Unsupported native protocol version")
    device_id = identifier(data.get("device_id"))
    if topic is not None and topic != f"mfi/{device_id}/config":
        raise ValueError("Descriptor identity does not match its discovery topic")
    refresh, expiry = data.get("refresh_interval"), data.get("expire_after")
    if (
        type(refresh) is not int
        or type(expiry) is not int
        or not 1 <= refresh <= 86400
        or not 3 * refresh <= expiry <= 259200
    ):
        raise ValueError("Invalid refresh/expiration intervals")
    raw_ports = data.get("ports")
    if not isinstance(raw_ports, list) or not 1 <= len(raw_ports) <= 32:
        raise ValueError("Descriptor requires between one and 32 ports")
    ports = []
    for raw in raw_ports:
        if not isinstance(raw, dict):
            raise ValueError("Invalid port descriptor")
        port_id, capabilities = raw.get("id"), raw.get("capabilities")
        if type(port_id) is not int or not 1 <= port_id <= 255:
            raise ValueError("Invalid physical port ID")
        if (
            not isinstance(capabilities, list)
            or not capabilities
            or any(not isinstance(role, str) or role not in ROLES for role in capabilities)
            or len(set(capabilities)) != len(capabilities)
        ):
            raise ValueError("Invalid port capabilities")
        ports.append(Port(port_id, _text(raw.get("name"), "port name"), tuple(capabilities)))
    if len({port.id for port in ports}) != len(ports):
        raise ValueError("Duplicate physical port ID")
    manufacturer = _text(data.get("manufacturer"), "manufacturer")
    if manufacturer != "Ubiquiti Networks":
        raise ValueError("Descriptor is not an mFi publisher")
    return Descriptor(
        device_id,
        _text(data.get("name"), "device name"),
        manufacturer,
        _text(data.get("model_id"), "model ID"),
        _text(data.get("model"), "model"),
        _text(data.get("firmware_version"), "firmware version"),
        _text(data.get("publisher_version"), "publisher version"),
        refresh,
        expiry,
        tuple(ports),
    )


@dataclass(frozen=True)
class Reading:
    value: Decimal | str | None
    error: str | None = None


@dataclass(frozen=True)
class Report:
    session_id: str
    sequence: int
    readings: dict[str, Reading]
    request_id: str | None


def parse_report(payload: str | bytes | bytearray, port: Port) -> Report:
    data = json_object(payload)
    session = identifier(data.get("session_id"))
    sequence = data.get("sequence")
    if type(sequence) is not int or not 1 <= sequence <= 2**63 - 1:
        raise ValueError("Invalid report sequence")
    request_id = identifier(data["request_id"]) if "request_id" in data else None
    readings = {}
    for role in port.capabilities:
        result = data.get(role)
        if not isinstance(result, dict):
            raise ValueError(f"Missing {role} result")
        if result.get("status") == "error":
            if "value" in result:
                raise ValueError("An invalid measurement must not carry a fallback value")
            readings[role] = Reading(None, _text(result.get("reason"), "measurement error"))
            continue
        if result.get("status") != "ok":
            raise ValueError("Unknown result status")
        value = result.get("value")
        if role == "relay":
            if value not in ("ON", "OFF"):
                raise ValueError("Relay state must be ON or OFF")
            readings[role] = Reading(str(value))
        else:
            if isinstance(value, bool) or not isinstance(value, (int, Decimal)):
                raise ValueError(f"Invalid numeric {role}")
            number = Decimal(value)
            if (
                not number.is_finite()
                or not 0 <= number <= MAX_MEASUREMENT[role]
                or not math.isfinite(float(number))
            ):
                raise ValueError(f"Invalid finite nonnegative {role}")
            readings[role] = Reading(number)
    return Report(session, sequence, readings, request_id)


def parse_availability(payload: str | bytes | bytearray) -> tuple[str, bool]:
    data = json_object(payload)
    session = identifier(data.get("session_id"))
    state = data.get("state")
    if state not in ("online", "offline"):
        raise ValueError("Invalid device availability")
    return session, state == "online"
