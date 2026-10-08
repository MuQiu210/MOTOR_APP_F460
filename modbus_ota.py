import logging
import hashlib
import argparse
import sys
import threading
from enum import Enum
from pymodbus.client import ModbusSerialClient
from pymodbus.logging import Log
import time
from pathlib import Path
from serial.tools import list_ports

from firmware_identity import (
    FirmwareIdentityError,
    fixed_ascii_bytes,
    modbus_ascii,
    modbus_words,
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


MAX_FIRMWARE_BYTES = 96 * 1024


class ModbusOta(ModbusSerialClient):
    REG_CONTROL = 0x0000
    REG_MD5_START = 0x0005
    REG_MD5_WORDS = 8
    REG_FW_SIZE_H = 0x000D
    REG_FW_SIZE_L = 0x000E
    REG_FW_WRITE_OFFSET = 0x000F
    REG_FW_DATA_START = 0x0010
    REG_FW_DATA_END = 0x004F
    REG_FW_WRITE_CONTROL = 0x0050
    REG_COMPAT_ID_START = 0x0051
    REG_MODEL_START = 0x00F0
    REG_MODEL_WORDS = 8
    FW_BLOCK_BYTES = 128
    FW_BLOCK_REGS = 64
    CMD_ENTER_UPGRADE = 1
    CMD_SET_FIRMWARE_SIZE = 2
    CMD_ERASE_FIRMWARE = 3
    CMD_WRITE_FIRMWARE_DATA = 4
    CMD_UPDATE_MD5 = 5
    CMD_SAVE_AND_JUMP_APP = 6
    CMD_SET_COMPATIBILITY_ID = 8
    APP_RUNNING_FLAG = 7
    def __init__(self, port: str, baudrate: int = 921600):
        super().__init__(
            port=port,
            baudrate=baudrate,
            bytesize=8,
            parity="N",
            stopbits=1,
            retries=0,
        )
        self.__progress = 0.0
        self.__result = DownloadResult.IDLE
        self.__transfer_start_ts = None
        self.timeout(50)

    def __load(self, bin_path: str) -> bytes:
        with open(bin_path, "rb") as f:
            return f.read()

    def __download_enter_upgrade(self, id: int, timeout: float | None = None) -> bool:
        """发送进入升级命令。

        参数:
            id: Modbus 从站地址。
            timeout: 超时时间（秒），小于0表示不限时。
        返回:
            成功进入升级模式返回 True；超时抛出 TimeoutError。
        """
        deadline = None if timeout is None or timeout < 0 else (time.monotonic() + timeout)
        retry_delay_s = 0.01
        retry_delay_max_s = 0.05
        while True:
            if deadline is not None and time.monotonic() > deadline:
                raise TimeoutError("enter upgrade mode timeout")

            retry_failed = False
            if self.socket is None:
                try:
                    connected = self.connect()
                except Exception:
                    connected = False
                if not connected:
                    retry_failed = True

            if not retry_failed:
                try:
                    rw_res = self.readwrite_registers(
                        read_address=self.REG_CONTROL,
                        read_count=1,
                        write_address=self.REG_CONTROL,
                        values=[self.CMD_ENTER_UPGRADE],
                        device_id=id,
                    )
                    if (
                        rw_res is not None
                        and not rw_res.isError()
                        and len(rw_res.registers) == 1
                        and rw_res.registers[0] == self.CMD_ENTER_UPGRADE
                    ):
                        return True
                    retry_failed = True
                except Exception:
                    retry_failed = True

            if retry_failed:
                # 进入升级命令重试使用指数退避，降低串口反复打开/请求频率。
                if deadline is None:
                    time.sleep(retry_delay_s)
                else:
                    remain_s = deadline - time.monotonic()
                    if remain_s <= 0:
                        raise TimeoutError("enter upgrade mode timeout")
                    time.sleep(min(retry_delay_s, remain_s))
                retry_delay_s = min(retry_delay_s * 2.0, retry_delay_max_s)

    def __read_device_model(self, id: int) -> str | None:
        try:
            if self.socket is None and not self.connect():
                return None
            response = self.read_holding_registers(
                address=self.REG_MODEL_START,
                count=self.REG_MODEL_WORDS,
                device_id=id,
            )
            if response is None or response.isError() or len(response.registers) != self.REG_MODEL_WORDS:
                return None
            return modbus_ascii(list(response.registers))
        except Exception:
            return None

    def __set_compatibility_id(self, id: int, model: str) -> bool:
        try:
            words = modbus_words(fixed_ascii_bytes(model))
            if self.socket is None and not self.connect():
                return False
            write_response = self.readwrite_registers(
                read_address=self.REG_CONTROL,
                read_count=1,
                write_address=self.REG_COMPAT_ID_START,
                values=words,
                device_id=id,
            )
            if write_response is None or write_response.isError():
                return False
            status_response = self.readwrite_registers(
                read_address=self.REG_CONTROL,
                read_count=1,
                write_address=self.REG_CONTROL,
                values=[self.CMD_SET_COMPATIBILITY_ID],
                device_id=id,
            )
            return (status_response is not None and not status_response.isError()
                    and len(status_response.registers) == 1
                    and status_response.registers[0] == self.CMD_SET_COMPATIBILITY_ID)
        except Exception:
            return False

    def __download_write_size(self, id: int, size: int) -> bool:
        value = int(size)
        if value < 0 or value > 0xFFFFFFFF:
            raise ValueError("size must be in range [0, 0xFFFFFFFF]")

        size_hi = (value >> 16) & 0xFFFF
        size_lo = value & 0xFFFF
        try:
            if self.socket is None:
                self.connect()

            rw_res = self.readwrite_registers(
                read_address=self.REG_FW_SIZE_H,
                read_count=2,
                write_address=self.REG_FW_SIZE_H,
                values=[size_hi, size_lo],
                device_id=id,
            )
            if (
                rw_res is None
                or rw_res.isError()
                or len(rw_res.registers) != 2
                or rw_res.registers[0] != size_hi
                or rw_res.registers[1] != size_lo
            ):
                return False

            status_res = self.readwrite_registers(
                read_address=self.REG_CONTROL,
                read_count=1,
                write_address=self.REG_CONTROL,
                values=[self.CMD_SET_FIRMWARE_SIZE],
                device_id=id,
            )
            if (
                status_res is not None
                and not status_res.isError()
                and len(status_res.registers) == 1
                and status_res.registers[0] == self.CMD_SET_FIRMWARE_SIZE
            ):
                return True
        except Exception:
            pass
        return False

    def __download_erase_flash(self, id: int) -> bool:
        
        try:
            if self.socket is None:
                self.connect()
            self.timeout(500)
            rw_res = self.readwrite_registers(
                read_address=self.REG_CONTROL,
                read_count=1,
                write_address=self.REG_CONTROL,
                values=[self.CMD_ERASE_FIRMWARE],
                device_id=id,
            )
            if (
                rw_res is not None
                and not rw_res.isError()
                and len(rw_res.registers) == 1
                and rw_res.registers[0] == self.CMD_ERASE_FIRMWARE
            ):
                return True
        except Exception:
            return False
        finally:
            self.timeout(50)


    def __download_flash_write(self, id: int, data_stream: bytes) -> bool:

        if not isinstance(data_stream, (bytes, bytearray, memoryview)):
            raise ValueError("data_stream must be bytes-like")

        payload = bytes(data_stream)
        if len(payload) == 0:
            raise ValueError("data_stream is empty")

        try:
            if self.socket is None:
                self.connect()

            total_blocks = (len(payload) + self.FW_BLOCK_BYTES - 1) // self.FW_BLOCK_BYTES
            if total_blocks > 0x10000:
                return False
            self.__progress = 0.0
            for i in range(total_blocks):
                block_offset = i

                start = i * self.FW_BLOCK_BYTES
                end = start + self.FW_BLOCK_BYTES
                block = payload[start:end]
                block_padded = block.ljust(self.FW_BLOCK_BYTES, b"\xFF")
                block_regs = [
                    (block_padded[j] << 8) | block_padded[j + 1]
                    for j in range(0, self.FW_BLOCK_BYTES, 2)
                ]

                # One FC0x17 transaction:
                # write offset + data + write command, then read control status.
                rw_res = self.readwrite_registers(
                    read_address=self.REG_CONTROL,
                    read_count=1,
                    write_address=self.REG_FW_WRITE_OFFSET,
                    values=[
                        block_offset,
                        *block_regs,
                        self.CMD_WRITE_FIRMWARE_DATA,
                    ],
                    device_id=id,
                )
                if (
                    rw_res is None
                    or rw_res.isError()
                    or len(rw_res.registers) != 1
                    or rw_res.registers[0] != self.CMD_WRITE_FIRMWARE_DATA
                ):
                    return False
                self.__progress = ((i + 1) * 100.0) / total_blocks

            return True
        except Exception:
            return False
        

    def __download_md5_update(self, id: int) -> bytes | None:
        try:
            if self.socket is None:
                self.connect()
            self.timeout(200)
            rw_res = self.readwrite_registers(
                read_address=self.REG_MD5_START,
                read_count=self.REG_MD5_WORDS,
                write_address=self.REG_CONTROL,
                values=[self.CMD_UPDATE_MD5],
                device_id=id,
            )
            if (
                rw_res is None
                or rw_res.isError()
                or len(rw_res.registers) != self.REG_MD5_WORDS
            ):
                return None
            md5_bytes = bytearray()
            for reg in rw_res.registers:
                md5_bytes.append((reg >> 8) & 0xFF)
                md5_bytes.append(reg & 0xFF)
            return bytes(md5_bytes)
        except Exception:
            return None
        finally:
            self.timeout(50)
        

    def __download_md5_read(self, id: int) -> bytes | None:
        try:
            if self.socket is None:
                self.connect()
            read_res = self.read_holding_registers(
                address=self.REG_MD5_START,
                count=self.REG_MD5_WORDS,
                device_id=id,
            )
            if (
                read_res is None
                or read_res.isError()
                or len(read_res.registers) != self.REG_MD5_WORDS
            ):
                return None

            md5_bytes = bytearray()
            for reg in read_res.registers:
                md5_bytes.append((reg >> 8) & 0xFF)
                md5_bytes.append(reg & 0xFF)
            return bytes(md5_bytes)
        except Exception:
            return None
        

    def __download_jump_app(self, id: int) -> bool:
        try:
            if self.socket is None:
                self.connect()
            self.timeout(200)
            rw_res = self.readwrite_registers(
                read_address=self.REG_CONTROL,
                read_count=1,
                write_address=self.REG_CONTROL,
                values=[self.CMD_SAVE_AND_JUMP_APP],
                device_id=id,
            )
            if (
                rw_res is not None
                and not rw_res.isError()
                and len(rw_res.registers) == 1
                and (rw_res.registers[0] == self.CMD_SAVE_AND_JUMP_APP 
                     or rw_res.registers[0] == self.APP_RUNNING_FLAG)
            ):
                self.timeout(50)
                return True
        except Exception:
            return False
        finally:
            self.timeout(50)
        

    def download(
        self, id: int, data_stream: bytes, timeout: float | None = None
    ) -> DownloadResult:
        device_id = int(id)
        if device_id < 1 or device_id > 0xF7:
            raise ValueError("id must be in range [1, 247]")
        if not isinstance(data_stream, (bytes, bytearray, memoryview)):
            raise ValueError("data_stream must be bytes-like")

        payload = bytes(data_stream)
        if len(payload) == 0:
            raise ValueError("data_stream is empty")
        if len(payload) > MAX_FIRMWARE_BYTES:
            self.__result = DownloadResult.FIRMWARE_TOO_LARGE
            return self.__result

        try:
            identity = parse_app_identity(payload)
        except FirmwareIdentityError:
            self.__result = DownloadResult.IDENTITY_INVALID
            return self.__result

        device_model = self.__read_device_model(device_id)
        if device_model is None:
            self.__result = DownloadResult.DEVICE_ID_READ_FAILED
            return self.__result
        if device_model != identity.model:
            self.__result = DownloadResult.MOTOR_ID_MISMATCH
            return self.__result

        local_md5 = hashlib.md5(payload).digest()
        remote_md5 = self.__download_md5_read(device_id)
        if remote_md5 is not None and remote_md5 == local_md5:
            if not self.__download_jump_app(device_id):
                self.__result = DownloadResult.JUMP_APP_FAILED
                return self.__result
            self.__result = DownloadResult.FIRMWARE_MATCHED
            return self.__result

        self.__result = DownloadResult.UPGRADING
        if not self.__download_enter_upgrade(device_id, timeout=timeout):
            self.__result = DownloadResult.ENTER_UPGRADE_FAILED
            return self.__result

        if not self.__set_compatibility_id(device_id, identity.model):
            self.__result = DownloadResult.COMPATIBILITY_REJECTED
            return self.__result

        self.__transfer_start_ts = time.monotonic()

        if not self.__download_write_size(device_id, len(payload)):
            self.__result = DownloadResult.WRITE_SIZE_FAILED
            return self.__result

        if not self.__download_erase_flash(device_id):
            self.__result = DownloadResult.ERASE_FAILED
            return self.__result

        if not self.__download_flash_write(device_id, payload):
            self.__result = DownloadResult.WRITE_DATA_FAILED
            return self.__result

        updated_md5 = self.__download_md5_update(device_id)
        if updated_md5 is None:
            self.__result = DownloadResult.MD5_UPDATE_FAILED
            return self.__result

        if updated_md5 != local_md5:
            self.__result = DownloadResult.MD5_MISMATCH
            return self.__result

        if not self.__download_jump_app(device_id):
            self.__result = DownloadResult.JUMP_APP_FAILED
            return self.__result

        self.__result = DownloadResult.UPGRADE_COMPLETED
        return self.__result

    def progress(self) -> float:
        return self.__progress

    def transfer_start_time(self) -> float | None:
        return self.__transfer_start_ts

    def result(self) -> DownloadResult:
        return self.__result

    def timeout(self, ms: float):
        seconds = float(ms) / 1000.0
        if seconds <= 0:
            raise ValueError("timeout must be > 0 ms")

        self.comm_params.timeout_connect = seconds

        if self.socket is not None:
            self.socket.timeout = seconds

        return self

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Modbus OTA downloader")
    parser.add_argument("--port", help="serial port, e.g. COM3")
    parser.add_argument(
        "--firmware",
        default=str(Path(__file__).resolve().parent / "build" / "App" / "App.bin"),
        help="firmware binary path",
    )
    parser.add_argument("--bps", type=int, default=921600, help="serial baudrate")
    parser.add_argument("--id", type=int, default=1, help="modbus device id (1~247)")
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="enter-upgrade timeout in seconds (set <0 to wait forever)",
    )
    args = parser.parse_args()

    # 完全禁用pymodbus日志落盘，避免生成或刷新modbus_ota.log。
    pymodbus_logger = logging.getLogger("pymodbus.logging")
    pymodbus_logger.setLevel(logging.ERROR)
    pymodbus_logger.propagate = False
    pymodbus_logger.handlers.clear()
    pymodbus_logger.disabled = True

    def choose_serial_port() -> str:
        ports = sorted(list_ports.comports(), key=lambda p: p.device)
        if len(ports) == 0:
            raise SystemExit("No serial ports found.")

        print("Available serial ports:")
        for idx, port in enumerate(ports, start=1):
            desc = port.description or ""
            hwid = port.hwid or ""
            print(f"  [{idx}] {port.device}  {desc}  {hwid}")

        while True:
            selected = input(f"Select serial port index (1-{len(ports)}): ").strip()
            try:
                index = int(selected)
            except ValueError:
                print("Invalid input, please enter a number.")
                continue

            if index < 1 or index > len(ports):
                print("Index out of range, try again.")
                continue

            return ports[index - 1].device

    if args.port:
        selected_port = args.port
    else:
        selected_port = choose_serial_port()
        print(f"Selected port: {selected_port}")

    firmware_path = Path(args.firmware).resolve()
    with open(firmware_path, "rb") as f:
        firmware = f.read()

    print(f"Loading {firmware_path} [{len(firmware)} bytes]")

    ota = ModbusOta(port=selected_port, baudrate=args.bps)
    worker_result = {"result": DownloadResult.IDLE, "error": None}

    def render_progress(progress_pct: float) -> None:
        width = 50
        p = max(0.0, min(100.0, progress_pct))
        filled = int((p / 100.0) * width)
        bar = "=" * filled + " " * (width - filled)
        sys.stdout.write(f"\r[{bar}] {p:6.2f}%")
        sys.stdout.flush()

    def worker() -> None:
        try:
            worker_result["result"] = ota.download(args.id, firmware, timeout=args.timeout)
        except Exception as exc:
            worker_result["error"] = exc

    try:
        ota.timeout(50.0)
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
            # Clear transient progress line and print accurate status.
            sys.stdout.write("\r" + (" " * 80) + "\r")
            sys.stdout.flush()
            print("Firmware already up-to-date (md5 matched), jump command sent [ota]")
        elif result == DownloadResult.UPGRADE_COMPLETED:
            render_progress(100.0)
            sys.stdout.write("\n")
            transfer_start = ota.transfer_start_time() or time.monotonic()
            elapsed = max(0.001, time.monotonic() - transfer_start)
            speed_kb_s = (len(firmware) / 1024.0) / elapsed
            print(f"[INFO] Programmed {len(firmware)} bytes at {speed_kb_s:.2f} kB/s [ota]")
        else:
            render_progress(ota.progress())
            sys.stdout.write("\n")
            print(f"[INFO] Upgrade failed after writing progress {ota.progress():.2f}% [ota]")
        print(f"[INFO] Result: {result.value}")

        raise SystemExit(
            0
            if result in (DownloadResult.FIRMWARE_MATCHED, DownloadResult.UPGRADE_COMPLETED)
            else 1
        )
    finally:
        try:
            ota.close()
        except Exception:
            pass
