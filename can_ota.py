"""CAN-OTA host downloader.

Ports the Modbus-OTA upgrade flow onto a CAN bus using the DaMiao (DM)
register-access frame format documented in DM-J4310P-2EC V1.0:

  - Read request : ID=0x7FF STD, D[0..1]=CANID_L/H, D[2]=0x33, D[3]=RID
  - Read response: ID=MST_ID  STD, D[0..1]=CANID_L/H, D[2]=0x33, D[3]=RID,
                   D[4..7]=32-bit little-endian data
  - Write request: ID=0x7FF STD, D[0..1]=CANID_L/H, D[2]=0x55, D[3]=RID,
                   D[4..7]=32-bit little-endian data
  - Write response: ID=MST_ID STD, D[0..1]=CANID_L/H, D[2]=0x55, D[3]=RID

The RID table mirrors the bootloader register map (can_slave.c). The DM motor
register semantics are intentionally NOT replicated.
"""

import hashlib
import argparse
import sys
import threading
import struct
import time
from enum import Enum
from pathlib import Path

from can_adapter import open_can_bus  # USB-CAN transport (hcanbus.dll, no python-can needed)

from firmware_identity import (
    FirmwareIdentityError,
    fixed_ascii_bytes,
    parse_app_identity,
)


class DownloadResult(Enum):
    IDLE = "Idle"
    UPGRADING = "Upgrading"
    FIRMWARE_MATCHED = "Firmware Matched"
    ENTER_UPGRADE_FAILED = "Enter Upgrade Failed"
    WRITE_SIZE_FAILED = "Write Size Failed"
    ERASE_FAILED = "Erase Failed"
    WRITE_DATA_FAILED = "Write Data Failed"
    MD5_UPDATE_FAILED = "MD5 Update Failed"
    MD5_MISMATCH = "MD5 Mismatch"
    JUMP_APP_FAILED = "Jump App Failed"
    UPGRADE_COMPLETED = "Upgrade Completed"
    IDENTITY_INVALID = "Invalid Firmware Identity"
    DEVICE_ID_READ_FAILED = "Device Identity Read Failed"
    MOTOR_ID_MISMATCH = "Motor ID Mismatch"
    COMPATIBILITY_REJECTED = "Compatibility Rejected"
    FIRMWARE_TOO_LARGE = "Firmware Too Large"


# DM frame opcodes (DM manual L435..467).
OP_READ = 0x33
OP_WRITE = 0x55

# OTA register RID table — matches bootloader can_slave.c.
RID_CONTROL = 0x00
RID_FW_SIZE = 0x01
RID_BLOCK_OFFSET = 0x02
RID_SUB_OFFSET = 0x03
RID_BLOCK_DATA = 0x04
RID_MD5_0 = 0x05
RID_MD5_1 = 0x06
RID_MD5_2 = 0x07
RID_MD5_3 = 0x08
RID_STATUS = 0x09
RID_MODEL_BASE = 0x0A
RID_VERSION_BASE = 0x30
# Fast streaming RIDs: 0x10..0x2F map to 32 4-byte slots inside a 128B block.
# The host burst-sends all 32 frames with no per-frame ACK, then commits.
RID_BLOCK_STREAM = 0x10

# OTA control command values.
CMD_ENTER_UPGRADE = 1
CMD_SET_FIRMWARE_SIZE = 2
CMD_ERASE_FIRMWARE = 3
CMD_WRITE_FIRMWARE_DATA = 4
CMD_UPDATE_MD5 = 5
CMD_SAVE_AND_JUMP_APP = 6
CMD_SET_COMPATIBILITY_ID = 8
APP_RUNNING_FLAG = 7

FW_BLOCK_BYTES = 128
FW_BLOCK_WORDS = FW_BLOCK_BYTES // 4  # 32 u32 sub-words per block
STREAM_RIDS = [RID_BLOCK_STREAM + i for i in range(FW_BLOCK_WORDS)]  # 0x10..0x2F
REGISTER_BROADCAST_ID = 0x7FF

# CONTROL value that asks the running APP to reset into the bootloader.
# Mirrors User/can_slave.c CAN_SLAVE_CMD_ENTER_BOOT (1) and the Modbus
# 0x0000 write=1 -> NVIC_SystemReset() path.
CMD_ENTER_BOOT = 1

# Full 32-bit mask = all 32 stream slots received. Matches bootloader s_stream_mask.
STREAM_MASK_COMPLETE = (1 << FW_BLOCK_WORDS) - 1
MAX_FIRMWARE_BYTES = 96 * 1024


class CanOta:
    """DaMiao-style CAN register-access OTA downloader."""

    def __init__(self, bus, can_id: int = 1, master_id: int = 0):
        self._bus = bus
        self._can_id = int(can_id) & 0x7FF
        self._master_id = int(master_id) & 0x7FF
        self._progress = 0.0
        self._result = DownloadResult.IDLE
        self._transfer_start_ts: float | None = None

    # ------------------------------------------------------------------ low-level
    def _canid_bytes(self) -> tuple[int, int]:
        return (self._can_id & 0xFF), ((self._can_id >> 8) & 0xFF)

    def _send_request(self, opcode: int, rid: int, value: int | None = None) -> None:
        lo, hi = self._canid_bytes()
        if value is None:
            data = bytes([lo, hi, opcode & 0xFF, rid & 0xFF])
        else:
            data = bytes([lo, hi, opcode & 0xFF, rid & 0xFF]) + struct.pack("<I", value & 0xFFFFFFFF)
        self._bus.send(REGISTER_BROADCAST_ID, data, is_extended_id=False)

    def _wait_response(self, opcode: int, rid: int, timeout: float) -> bytes | None:
        lo, hi = self._canid_bytes()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            msg = self._bus.recv(timeout=max(0.0, deadline - time.monotonic()))
            if msg is None:
                continue
            if msg.is_extended_id:
                continue
            if msg.arbitration_id != self._master_id:
                continue
            if len(msg.data) < 4:
                continue
            if msg.data[0] != lo or msg.data[1] != hi:
                continue
            if msg.data[2] != opcode or msg.data[3] != rid:
                continue
            return bytes(msg.data)
        return None

    def write_reg(self, rid: int, value: int, timeout: float = 0.5, retries: int = 3) -> bool:
        """Write one 32-bit register and wait for the DM write-ack frame."""
        for _ in range(max(1, retries)):
            self._send_request(OP_WRITE, rid, value)
            resp = self._wait_response(OP_WRITE, rid, timeout)
            if resp is not None:
                return True
        return False

    def read_reg(self, rid: int, timeout: float = 0.5, retries: int = 3) -> int | None:
        """Read one 32-bit register and return its value, or None on timeout."""
        for _ in range(max(1, retries)):
            self._send_request(OP_READ, rid)
            resp = self._wait_response(OP_READ, rid, timeout)
            if resp is not None and len(resp) >= 8:
                return struct.unpack("<I", resp[4:8])[0]
        return None

    def _read_device_model(self) -> str | None:
        words = [self.read_reg(RID_MODEL_BASE + i) for i in range(4)]
        if any(word is None for word in words):
            return None
        raw = b"".join(struct.pack("<I", int(word)) for word in words)
        return raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")

    def _set_compatibility_id(self, model: str) -> bool:
        raw = fixed_ascii_bytes(model)
        for i in range(4):
            value = struct.unpack("<I", raw[i * 4:(i + 1) * 4])[0]
            if not self.write_reg(RID_MODEL_BASE + i, value):
                return False
        return self.write_reg(RID_CONTROL, CMD_SET_COMPATIBILITY_ID)

    # ------------------------------------------------------------------ OTA stages
    def _wait_write_ack(self, rid: int, timeout: float, retries: int) -> bool:
        """Wait for a write-ack frame matching ``rid`` (CONTROL commit).

        Does NOT send anything — the caller already sent the write. Re-sends
        are the caller's responsibility (or the final MD5 catches a miss).
        Used to pace per-block commits without a full write_reg round-trip.
        """
        for _ in range(max(1, retries)):
            resp = self._wait_response(OP_WRITE, rid, timeout)
            if resp is not None:
                return True
        return False

    def _md5_read(self) -> bytes | None:
        words = [self.read_reg(rid, timeout=0.5) for rid in
                  (RID_MD5_0, RID_MD5_1, RID_MD5_2, RID_MD5_3)]
        if any(w is None for w in words):
            return None
        return struct.pack("<IIII", *[int(w) for w in words])

    def _enter_upgrade(self, timeout: float | None) -> bool:
        """Pound CONTROL=ENTER_UPGRADE at the bootloader until it sticks.

        The bootloader's startup window is only ~100ms (200 SysTick ticks @
        0.5ms) after a reset. A read-probe-then-write is too slow: by the time
        one write_reg round-trip (~3-5ms) completes and we re-check, the window
        can close. Instead we fire ENTER write frames in a tight loop and rely
        on the device entering ENTER state as soon as the first one lands; we
        confirm by reading CONTROL back (non-zero => no longer WAIT).
        """
        deadline = None if timeout is None or timeout < 0 else time.monotonic() + timeout
        lo, hi = self._canid_bytes()
        enter_payload = bytes([lo, hi, OP_WRITE, RID_CONTROL]) + struct.pack("<I", CMD_ENTER_UPGRADE)
        # Burst several ENTER writes per poll so we cover the whole boot window.
        ENTER_BURST = 8
        while True:
            if deadline is not None and time.monotonic() > deadline:
                return False
            # Fire ENTER write burst without waiting for per-frame ACKs.
            frames = [(REGISTER_BROADCAST_ID, enter_payload, False)] * ENTER_BURST
            self._bus.send_many(frames)
            # Confirm: read CONTROL. Anything non-zero (and not APP_RUNNING_FLAG)
            # means the bootloader left WAIT and is in ENTER state.
            echoed = self.read_reg(RID_CONTROL, timeout=0.05, retries=1)
            if echoed is not None and echoed != APP_RUNNING_FLAG and echoed != 0:
                return True
            # Short backoff so we don't hammer during the reset settling time.
            time.sleep(0.005)

    def _ensure_bootloader(self, timeout: float | None) -> bool:
        """Get the device into the bootloader, resetting the APP if needed.

        Strategy that survives the ~100ms boot window:
          1. Read CONTROL once. If it's already non-APP (bootloader up): done.
          2. If APP is running (CONTROL==7): fire the ENTER_BOOT reset write
             (no ACK wait), then immediately start the ENTER_UPGRADE burst
             in _enter_upgrade — this overlaps the chip's reboot with our
             retry loop so the first ENTER_UPGRADE that lands after boot is
             caught in-window.
        Returns True if the bootloader is confirmed in ENTER state.
        """
        deadline = None if timeout is None or timeout < 0 else time.monotonic() + timeout
        # Quick single probe: maybe bootloader already up.
        ctrl = self.read_reg(RID_CONTROL, timeout=0.1, retries=1)
        if ctrl is not None and ctrl != APP_RUNNING_FLAG:
            # Bootloader answering (could be WAIT or ENTER). _enter_upgrade will
            # either catch WAIT-in-window or confirm ENTER.
            if self._enter_upgrade(timeout=_remaining(deadline)):
                return True
        if ctrl == APP_RUNNING_FLAG:
            # Fire reset write, no ACK wait.
            lo, hi = self._canid_bytes()
            reset_payload = bytes([lo, hi, OP_WRITE, RID_CONTROL]) + struct.pack("<I", CMD_ENTER_BOOT)
            self._bus.send(REGISTER_BROADCAST_ID, reset_payload, is_extended_id=False)
            # Immediately begin ENTER_UPGRADE burst to cover the boot window.
            if self._enter_upgrade(timeout=_remaining(deadline)):
                return True
        # Last resort: just try ENTER_UPGRADE for the remainder.
        return self._enter_upgrade(timeout=_remaining(deadline))

    def _write_size(self, size: int) -> bool:
        if not self.write_reg(RID_FW_SIZE, size):
            return False
        return self.write_reg(RID_CONTROL, CMD_SET_FIRMWARE_SIZE)

    def _erase_flash(self) -> bool:
        return self.write_reg(RID_CONTROL, CMD_ERASE_FIRMWARE, timeout=2.0)

    def _send_stream_frame(self, rid: int, word_bytes: bytes) -> None:
        """Send one streaming data frame: DM 0x55 write, RID encodes the slot.

        No ACK is expected — the host burst-sends all 32 frames back-to-back
        and the device stays silent. CAN TX backpressure is handled by the bus
        driver (it blocks on send until the frame is accepted).
        """
        lo, hi = self._canid_bytes()
        data = bytes([lo, hi, OP_WRITE, rid & 0xFF]) + bytes(word_bytes)
        self._bus.send(REGISTER_BROADCAST_ID, data, is_extended_id=False)

    def _send_stream_burst(self, block: bytes) -> int:
        """Burst-submit all 32 stream frames for one 128B block in one DLL call.

        Pre-builds the whole frame tuple list and hands it to send_many() so the
        USB-CAN driver pipelines them instead of making 32 separate
        ``CAN_Transmit`` calls (each of which has tens of microseconds of
        Python+ctext overhead, which is what capped throughput at ~0.5% load).
        Returns the number of frames the driver accepted.
        """
        lo, hi = self._canid_bytes()
        header = bytes([lo, hi, OP_WRITE])
        frames = [
            (REGISTER_BROADCAST_ID, header + bytes([RID_BLOCK_STREAM + j]) + block[j * 4:(j + 1) * 4], False)
            for j in range(FW_BLOCK_WORDS)
        ]
        return self._bus.send_many(frames)

    def _build_block_frames(self, block_index: int, block: bytes):
        """Build the BLOCK_OFFSET-select write + 32 stream + commit for one block.

        All 34 frames go out in one send_many() burst. The device processes them
        in arrival order: select clears the mask, 32 stream frames fill the
        single 128B buffer, commit writes that buffer to flash and sends one
        ACK. We then wait for that ACK as the pacing signal (it tells us the
        flash write finished and the buffer is free for the next block) — this
        is the one unavoidable round-trip per block, and it's gated by flash
        write time (~2ms), not by Python.
        """
        lo, hi = self._canid_bytes()
        header = bytes([lo, hi, OP_WRITE])
        frames = [
            (REGISTER_BROADCAST_ID,
             header + bytes([RID_BLOCK_OFFSET]) + struct.pack("<I", block_index),
             False)
        ]
        for j in range(FW_BLOCK_WORDS):
            frames.append(
                (REGISTER_BROADCAST_ID,
                 header + bytes([RID_BLOCK_STREAM + j]) + block[j * 4:(j + 1) * 4],
                 False)
            )
        frames.append(
            (REGISTER_BROADCAST_ID,
             header + bytes([RID_CONTROL]) + struct.pack("<I", CMD_WRITE_FIRMWARE_DATA),
             False)
        )
        return frames

    def _flash_write(self, payload: bytes) -> bool:
        """Stream firmware with deep pipelining to saturate the CAN bus.

        The single-buffer bootloader can only hold one 128B block at a time, so
        we can't fire the whole image blindly — but we can keep the bus full by
        pipelining: fire several blocks' worth of (select+32 stream+commit)
        frames back-to-back in one send_many(), then drain the commit ACKs as
        they come back. The device processes frames in arrival order; while it
        is busy writing block N to flash (~2ms), it can still drain block N+1's
        33 setup frames into its 32-deep RX FIFO. So we keep the TX queue full
        and let the device self-pace on its RX FIFO. A per-block ACK wait would
        kill this, so we don't wait per block — we batch.

        Pipeline depth (PIPELINE_BLOCKS) is sized so that the frames in flight
        fit the device RX FIFO across one flash-write window:
          flash write ≈ 2ms, one frame ≈ 130us on wire => ~15 frames fit in
          one flash window. One block = 33 frames, so 2 blocks in flight keeps
          the device continuously fed during a flash write without FIFO spill.
          We use 4 for safety margin; the device simply backpressures via its
          RX FIFO and we drain ACKs between batches.
        Integrity is verified by the final MD5; a dropped commit yields
        MD5_MISMATCH and a clean retry, not silent corruption.
        """
        total_blocks = (len(payload) + FW_BLOCK_BYTES - 1) // FW_BLOCK_BYTES
        self._progress = 0.0

        lo, hi = self._canid_bytes()
        commit_payload = bytes([lo, hi, OP_WRITE, RID_CONTROL]) + struct.pack("<I", CMD_WRITE_FIRMWARE_DATA)

        PIPELINE_BLOCKS = 4
        i = 0
        while i < total_blocks:
            end = min(i + PIPELINE_BLOCKS, total_blocks)
            n = end - i

            # Build select+32 stream+commit frames for n blocks, concatenate.
            frames = []
            for k in range(n):
                start = (i + k) * FW_BLOCK_BYTES
                block = payload[start:start + FW_BLOCK_BYTES].ljust(FW_BLOCK_BYTES, b"\xFF")
                frames.extend(self._build_block_frames(i + k, block))

            # One send_many fires the whole batch — the USB-CAN driver queues
            # them and pumps onto the wire back-to-back with no Python between.
            self._bus.send_many(frames)

            # Drain the n commit ACKs (and any stray feedback) so the RX queue
            # doesn't bloat. Don't *wait* on each ACK — just sweep what's
            # arrived. Short grace for the last flash write to finish.
            time.sleep(0.0005 * n)
            self._bus.drain_rx(max_frames=8 * n)

            i = end
            self._progress = (i * 100.0) / total_blocks
        return True

    def _md5_update(self) -> bytes | None:
        if not self.write_reg(RID_CONTROL, CMD_UPDATE_MD5, timeout=1.0):
            return None
        return self._md5_read()

    def _jump_app(self) -> bool:
        ok = self.write_reg(RID_CONTROL, CMD_SAVE_AND_JUMP_APP, timeout=1.0)
        if not ok:
            return False
        # Device may already be executing the APP running flag; accept either.
        echoed = self.read_reg(RID_CONTROL, retries=1)
        return echoed in (CMD_SAVE_AND_JUMP_APP, APP_RUNNING_FLAG, None)

    # ------------------------------------------------------------------ public API
    def download(self, data_stream: bytes, timeout: float | None = None) -> DownloadResult:
        payload = bytes(data_stream)
        if len(payload) == 0:
            raise ValueError("data_stream is empty")
        if len(payload) > MAX_FIRMWARE_BYTES:
            self._result = DownloadResult.FIRMWARE_TOO_LARGE
            return self._result

        try:
            identity = parse_app_identity(payload)
        except FirmwareIdentityError:
            self._result = DownloadResult.IDENTITY_INVALID
            return self._result

        device_model = self._read_device_model()
        if device_model is None:
            self._result = DownloadResult.DEVICE_ID_READ_FAILED
            return self._result
        if device_model != identity.model:
            self._result = DownloadResult.MOTOR_ID_MISMATCH
            return self._result

        local_md5 = hashlib.md5(payload).digest()

        # The device may be running the APP. If so, send the reset command over
        # CAN so it drops back into the bootloader before we enter upgrade mode.
        # _ensure_bootloader already drives the device into ENTER state.
        if not self._ensure_bootloader(timeout=timeout):
            self._result = DownloadResult.ENTER_UPGRADE_FAILED
            return self._result

        if not self._set_compatibility_id(identity.model):
            self._result = DownloadResult.COMPATIBILITY_REJECTED
            return self._result

        self._result = DownloadResult.UPGRADING

        self._transfer_start_ts = time.monotonic()

        if not self._write_size(len(payload)):
            self._result = DownloadResult.WRITE_SIZE_FAILED
            return self._result

        if not self._erase_flash():
            self._result = DownloadResult.ERASE_FAILED
            return self._result

        if not self._flash_write(payload):
            self._result = DownloadResult.WRITE_DATA_FAILED
            return self._result

        updated_md5 = self._md5_update()
        if updated_md5 is None:
            self._result = DownloadResult.MD5_UPDATE_FAILED
            return self._result

        if updated_md5 != local_md5:
            self._result = DownloadResult.MD5_MISMATCH
            return self._result

        if not self._jump_app():
            self._result = DownloadResult.JUMP_APP_FAILED
            return self._result

        self._result = DownloadResult.UPGRADE_COMPLETED
        return self._result

    def progress(self) -> float:
        return self._progress

    def transfer_start_time(self) -> float | None:
        return self._transfer_start_ts

    def result(self) -> DownloadResult:
        return self._result

    def close(self) -> None:
        try:
            self._bus.shutdown()
        except Exception:
            pass


def _remaining(deadline: float | None) -> float | None:
    """Seconds left until ``deadline`` (or None if no deadline)."""
    if deadline is None:
        return None
    return max(0.0, deadline - time.monotonic())


def open_bus(interface: str, channel, bitrate: int, dll_path: str | None = None):
    """Open the CAN bus used for the OTA register exchange.

    interface ``hcanbus`` (default): 禾华盛 USB-CAN(FD) via the bundled
    ``hcanbus.dll`` adapter (no python-can dependency). Any other interface
    name falls back to python-can; in that case ``channel`` is passed through
    unchanged to python-can and may be a string.
    """
    return open_can_bus(interface, channel, bitrate, dll_path=dll_path)


def render_progress(progress_pct: float) -> None:
    width = 50
    p = max(0.0, min(100.0, progress_pct))
    filled = int((p / 100.0) * width)
    bar = "=" * filled + " " * (width - filled)
    sys.stdout.write(f"\r[{bar}] {p:6.2f}%")
    sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description="CAN-OTA downloader (DaMiao register frame)")
    parser.add_argument("--interface", default="hcanbus",
                        help="CAN interface: 'hcanbus' (禾华盛 USB-CAN, default) or a python-can "
                             "interface name (canalystii, vector, pcan, socketcan, ...) — the latter "
                             "requires `pip install python-can`")
    parser.add_argument("--channel", default="0",
                        help="hcanbus: win_ch index (default 0 = device0 channel0). "
                             "python-can: interface-specific channel")
    parser.add_argument("--bitrate", type=int, default=1_000_000, help="CAN bus bitrate")
    parser.add_argument("--dll", default=None,
                        help="path to hcanbus.dll (default: auto-search next to this script / cwd)")
    parser.add_argument("--can-id", type=int, default=1, help="loader CAN node id (11-bit)")
    parser.add_argument("--master-id", type=int, default=0, help="master/response CAN id")
    parser.add_argument(
        "--firmware",
        default=str(Path(__file__).resolve().parent / "build" / "App" / "App.bin"),
        help="firmware binary path",
    )
    parser.add_argument("--timeout", type=float, default=10.0,
                        help="enter-upgrade timeout in seconds (<0 = wait forever)")
    args = parser.parse_args()

    firmware_path = Path(args.firmware).resolve()
    with open(firmware_path, "rb") as f:
        firmware = f.read()
    print(f"Loading {firmware_path} [{len(firmware)} bytes]")

    bus = open_bus(args.interface, args.channel, args.bitrate, dll_path=args.dll)
    ota = CanOta(bus, can_id=args.can_id, master_id=args.master_id)

    worker_result = {"result": DownloadResult.IDLE, "error": None}

    def worker() -> None:
        try:
            worker_result["result"] = ota.download(firmware, timeout=args.timeout)
        except Exception as exc:
            worker_result["error"] = exc

    try:
        t = threading.Thread(target=worker, daemon=True)
        t.start()
        while t.is_alive():
            render_progress(ota.progress())
            time.sleep(0.1)
        t.join()

        if worker_result["error"] is not None:
            raise worker_result["error"]

        result = worker_result["result"]
        if result == DownloadResult.FIRMWARE_MATCHED:
            sys.stdout.write("\r" + (" " * 80) + "\r")
            sys.stdout.flush()
            print("Firmware already up-to-date (md5 matched), jump command sent [can-ota]")
        elif result == DownloadResult.UPGRADE_COMPLETED:
            render_progress(100.0)
            sys.stdout.write("\n")
            transfer_start = ota.transfer_start_time() or time.monotonic()
            elapsed = max(0.001, time.monotonic() - transfer_start)
            speed_kb_s = (len(firmware) / 1024.0) / elapsed
            print(f"[INFO] Programmed {len(firmware)} bytes at {speed_kb_s:.2f} kB/s [can-ota]")
        else:
            render_progress(ota.progress())
            sys.stdout.write("\n")
            print(f"[INFO] Upgrade failed after writing progress {ota.progress():.2f}% [can-ota]")
        print(f"[INFO] Result: {result.value}")

        return 0 if result in (DownloadResult.FIRMWARE_MATCHED, DownloadResult.UPGRADE_COMPLETED) else 1
    finally:
        try:
            ota.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
