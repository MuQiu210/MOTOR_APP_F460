"""Firmware identity layout used by the standalone OTA scripts."""

from __future__ import annotations

from dataclasses import dataclass


IDENTITY_BIN_OFFSET = 0x1000
IDENTITY_MAGIC_OFFSET = 0x08
IDENTITY_MODEL_OFFSET = 0x0C
IDENTITY_VERSION_OFFSET = 0x1C
IDENTITY_MAGIC = b"FID1"
IDENTITY_FIELD_BYTES = 16
IDENTITY_TOTAL_BYTES = 48

MODBUS_MODEL_START = 0x00F0
MODBUS_ID_WORDS = IDENTITY_FIELD_BYTES // 2

CAN_MODEL_BASE = 0x0A


class FirmwareIdentityError(ValueError):
    """Raised when a firmware image has no valid identity section."""


@dataclass(frozen=True)
class FirmwareIdentity:
    model: str
    version: str


def _decode_fixed_ascii(raw: bytes, field_name: str) -> str:
    if b"\x00" in raw:
        value, padding = raw.split(b"\x00", 1)
        if any(padding):
            raise FirmwareIdentityError(f"{field_name} has non-zero padding")
    else:
        value = raw
    if not value or any(byte < 0x20 or byte > 0x7E for byte in value):
        raise FirmwareIdentityError(f"{field_name} is not printable ASCII")
    return value.decode("ascii")


def parse_app_identity(firmware: bytes) -> FirmwareIdentity:
    """Parse and validate the fixed identity section from an APP .bin image."""
    start = IDENTITY_BIN_OFFSET
    end = start + IDENTITY_TOTAL_BYTES
    if len(firmware) < end:
        raise FirmwareIdentityError(
            f"firmware is too short for identity section at 0x{start:X}"
        )

    section = firmware[start:end]
    if section[IDENTITY_MAGIC_OFFSET:IDENTITY_MAGIC_OFFSET + len(IDENTITY_MAGIC)] != IDENTITY_MAGIC:
        raise FirmwareIdentityError("identity magic is missing or invalid")

    model = _decode_fixed_ascii(
        section[IDENTITY_MODEL_OFFSET:IDENTITY_MODEL_OFFSET + IDENTITY_FIELD_BYTES],
        "model",
    )
    version = _decode_fixed_ascii(
        section[IDENTITY_VERSION_OFFSET:IDENTITY_VERSION_OFFSET + IDENTITY_FIELD_BYTES],
        "version",
    )
    if not version.startswith("APP-"):
        raise FirmwareIdentityError("APP version prefix is invalid")
    return FirmwareIdentity(model=model, version=version)


def fixed_ascii_bytes(value: str) -> bytes:
    """Encode one identity string into its zero-padded fixed-size field."""
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise FirmwareIdentityError("identity string is not ASCII") from exc
    if not raw or len(raw) > IDENTITY_FIELD_BYTES:
        raise FirmwareIdentityError("identity string length is out of range")
    if any(byte < 0x20 or byte > 0x7E for byte in raw):
        raise FirmwareIdentityError("identity string is not printable ASCII")
    return raw.ljust(IDENTITY_FIELD_BYTES, b"\x00")


def modbus_words(raw: bytes) -> list[int]:
    """Pack 16 bytes into high-byte-first Modbus registers."""
    if len(raw) != IDENTITY_FIELD_BYTES:
        raise ValueError("identity field must be 16 bytes")
    return [(raw[i] << 8) | raw[i + 1] for i in range(0, len(raw), 2)]


def modbus_ascii(registers: list[int]) -> str:
    """Decode high-byte-first Modbus registers as a zero-terminated string."""
    raw = bytearray()
    for register in registers:
        raw.extend(((register >> 8) & 0xFF, register & 0xFF))
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
