"""USB-CAN adapter for the OTA host tools.

Bridges the 禾华盛 (Hesheng) USB-CAN(FD) dongle DLL (``hcanbus.dll``) to the
simple synchronous send/recv surface that :mod:`can_ota` expects. Only classic
CAN (8-byte) frames are used — the DaMiao register-access OTA protocol is
classic CAN, never CAN-FD, so the FD code paths are intentionally absent.

``hcanbus.dll`` API (sourced from the vendor python example, mirrored in
``OTA-tool/can_tool/can_adapter.py``)::

    CAN_ScanDevice()                            -> device count
    CAN_OpenDevice(win_ch) / CAN_CloseDevice    -> 0 on success
    CAN_Init(win_ch, &CanConfig)                -> classic-CAN mode
    CAN_Transmit(win_ch, &CanMsg, count, tmo)   -> sent count
    CAN_Receive(win_ch, &CanMsg, count, tmo)    -> received count
    CAN_Reset(win_ch)                           -> reset controller

The Windows DLL folds (device, channel) into one integer: ``win_ch = dev*2 + ch``.
For a single USB-CAN device, channel 0 is ``win_ch = 0``.

This module depends only on the Python standard library + the DLL; it does NOT
require ``python-can``. ``open_can_bus()`` falls back to ``python-can`` only
when a non-``hcanbus`` interface is requested.
"""

import ctypes
import os
import struct
import threading
import time
from collections import deque
from dataclasses import dataclass


STATUS_OK = 0


@dataclass
class CanFrame:
    """Classic CAN frame (≤8 bytes). Mirrors the python-can message surface."""

    arbitration_id: int = 0
    data: bytes = b""
    is_extended_id: bool = False


# ---------------------------------------------------------------------------
# hcanbus.dll struct definitions (vendor python example)
# ---------------------------------------------------------------------------

class CanConfig(ctypes.Structure):
    _fields_ = [
        ("baudrate", ctypes.c_uint),
        ("Pres", ctypes.c_ushort),
        ("Tseg1", ctypes.c_ubyte),
        ("Tseg2", ctypes.c_ubyte),
        ("SJW", ctypes.c_ubyte),
        ("config", ctypes.c_ubyte),
        ("Model", ctypes.c_ubyte),
        ("Reserved", ctypes.c_ubyte),
    ]


class CanMsg(ctypes.Structure):
    _fields_ = [
        ("ID", ctypes.c_uint),
        ("TimeStamp", ctypes.c_uint),
        ("FrameType", ctypes.c_ubyte),
        ("DataLen", ctypes.c_ubyte),
        ("ExternFlag", ctypes.c_ubyte),
        ("RemoteFlag", ctypes.c_ubyte),
        ("BusSatus", ctypes.c_ubyte),
        ("ErrSatus", ctypes.c_ubyte),
        ("TECounter", ctypes.c_ubyte),
        ("RECounter", ctypes.c_ubyte),
        ("Data", ctypes.c_ubyte * 8),
    ]


# ---------------------------------------------------------------------------
# DLL discovery (DLL bitness must match the Python interpreter)
# ---------------------------------------------------------------------------

_DLL_SEARCH_NAMES = ["hcanbus.dll", "HCanbus.dll"]
_PY_IS_64BIT = struct.calcsize("P") == 8


def _dll_matches_python_bits(path: str) -> bool:
    """Inspect the PE header to check the DLL matches the interpreter bitness."""
    try:
        with open(path, "rb") as f:
            head = f.read(0x1000)
        pe_off = struct.unpack_from("<I", head, 0x3C)[0]
        machine = struct.unpack_from("<H", head, pe_off + 4)[0]
        return (machine == 0x8664) == _PY_IS_64BIT
    except Exception:
        return True  # parse failed — let LoadLibrary try


def find_hcanbus_dll(extra_dirs=None):
    """Locate ``hcanbus.dll`` in the script dir, cwd, or extra_dirs."""
    here = os.path.dirname(os.path.abspath(__file__))
    dirs = [here, os.getcwd()]
    if extra_dirs:
        dirs += [os.path.abspath(d) for d in extra_dirs]
    for d in dirs:
        for name in _DLL_SEARCH_NAMES:
            p = os.path.join(d, name)
            if os.path.isfile(p) and _dll_matches_python_bits(p):
                return p
    return None


# ---------------------------------------------------------------------------
# 禾华盛 USB-CAN adapter (classic CAN, synchronous send + background RX drain)
# ---------------------------------------------------------------------------

class HCanbusBus:
    """Synchronous classic-CAN bus over ``hcanbus.dll``.

    A background thread continuously drains the hardware RX FIFO into an
    in-memory queue so unsolicited frames (e.g. APP periodic feedback) cannot
    overflow the device FIFO between requests.
    """

    RX_BATCH = 256
    TX_BATCH = 512

    def __init__(self, dll_path, win_ch: int = 0, bitrate: int = 1_000_000):
        self._lock = threading.Lock()
        self._win_ch = -1
        self._is_open = False
        self._rx_buf = (CanMsg * self.RX_BATCH)()
        self._tx_buf = (CanMsg * self.TX_BATCH)()
        self._queue: deque = deque()
        self._cond = threading.Condition()
        self._rx_running = False
        self._rx_thread = None

        if not os.path.isfile(dll_path):
            raise FileNotFoundError(f"hcanbus.dll not found: {dll_path}")
        if not _dll_matches_python_bits(dll_path):
            raise RuntimeError(
                f"hcanbus.dll bitness mismatch (Python is "
                f"{'64' if _PY_IS_64BIT else '32'}-bit): {dll_path}"
            )

        dll_dir = os.path.dirname(os.path.abspath(dll_path))
        if os.path.isdir(dll_dir):
            try:
                os.add_dll_directory(dll_dir)
            except Exception:
                pass
        self._dll = ctypes.windll.LoadLibrary(dll_path)
        self._setup_prototypes(self._dll)

        n = self._dll.CAN_ScanDevice()
        if n <= 0:
            raise RuntimeError(
                "CAN_ScanDevice found 0 devices — check the USB-CAN dongle is "
                "plugged in and not opened by another app (e.g. HCANView)"
            )
        ret = self._dll.CAN_OpenDevice(win_ch)
        if ret != STATUS_OK:
            raise RuntimeError(
                f"CAN_OpenDevice(win_ch={win_ch}) failed ret={ret} — interface "
                "not present or occupied by another app"
            )
        cfg = CanConfig()
        cfg.baudrate = int(bitrate)
        # config=0x07 matches the vendor example (incl. termination enable);
        # 0x00 leaves some interfaces unable to transmit.
        cfg.config = 0x07
        ret = self._dll.CAN_Init(win_ch, ctypes.byref(cfg))
        if ret != STATUS_OK:
            self._dll.CAN_CloseDevice(win_ch)
            raise RuntimeError(
                f"CAN_Init(win_ch={win_ch}, bitrate={bitrate}) failed ret={ret}"
            )

        self._win_ch = win_ch
        self._is_open = True
        self._start_rx_thread()

    @staticmethod
    def _setup_prototypes(L):
        u, i, p = ctypes.c_uint, ctypes.c_int, ctypes.POINTER
        L.CAN_ScanDevice.argtypes = []
        L.CAN_ScanDevice.restype = i
        L.CAN_OpenDevice.argtypes = [u]
        L.CAN_OpenDevice.restype = i
        L.CAN_CloseDevice.argtypes = [u]
        L.CAN_CloseDevice.restype = i
        L.CAN_Init.argtypes = [u, p(CanConfig)]
        L.CAN_Init.restype = i
        L.CAN_Transmit.argtypes = [u, p(CanMsg), u, i]
        L.CAN_Transmit.restype = i
        L.CAN_Receive.argtypes = [u, p(CanMsg), i, i]
        L.CAN_Receive.restype = i
        L.CAN_Reset.argtypes = [u]
        L.CAN_Reset.restype = i

    # -- background RX drain -------------------------------------------------

    def _start_rx_thread(self):
        self._rx_running = True
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._rx_thread.start()

    def _stop_rx_thread(self):
        self._rx_running = False
        if self._rx_thread is not None:
            self._rx_thread.join(timeout=1.0)
            self._rx_thread = None

    def _rx_loop(self):
        buf = self._rx_buf
        while self._rx_running and self._is_open:
            got = 0
            try:
                with self._lock:
                    # Short 2ms receive timeout so the lock is released often and
                    # TX bursts on the main thread don't stall behind RX polls.
                    n = self._dll.CAN_Receive(
                        self._win_ch,
                        ctypes.cast(buf, ctypes.POINTER(CanMsg)),
                        self.RX_BATCH, 2,
                    )
                if 0 < n <= self.RX_BATCH:
                    frames = []
                    for k in range(n):
                        m = buf[k]
                        dlen = min(m.DataLen, 8)
                        frames.append(CanFrame(
                            arbitration_id=int(m.ID),
                            data=bytes(m.Data[:dlen]),
                            is_extended_id=bool(m.ExternFlag),
                        ))
                    if frames:
                        with self._cond:
                            self._queue.extend(frames)
                            self._cond.notify_all()
                        got = len(frames)
            except Exception:
                time.sleep(0.01)
                continue
            if got == 0:
                time.sleep(0.0005)

    # -- public send / recv --------------------------------------------------

    def send(self, arbitration_id: int, data, is_extended_id: bool = False) -> bool:
        if not self._is_open:
            return False
        data = bytes(data)[:8]
        msg = CanMsg()
        msg.ID = int(arbitration_id)
        msg.DataLen = len(data)
        msg.ExternFlag = 1 if is_extended_id else 0
        msg.RemoteFlag = 0
        msg.FrameType = 0
        for i, b in enumerate(data):
            msg.Data[i] = b
        with self._lock:
            try:
                ret = self._dll.CAN_Transmit(self._win_ch, ctypes.byref(msg), 1, 100)
            except Exception:
                return False
        return ret > 0 or ret == STATUS_OK

    def send_many(self, frames) -> int:
        """Queue a burst of frames for transmission.

        ``frames`` is an iterable of ``(arbitration_id, data, is_extended_id)``
        tuples (classic CAN, ≤8 bytes each). Returns the number of frames
        submitted to the driver. Designed for the OTA streaming burst where
        thousands of frames must go out back-to-back with minimal Python
        overhead.

        The batch is filled into a pre-allocated ctypes array (``_tx_buf``)
        without per-frame allocation, then submitted in chunks of up to
        TX_BATCH (512) via ``CAN_Transmit(count=N)``. TX does not take the RX
        lock, so the background RX drain keeps running during long bursts.
        """
        if not self._is_open:
            return 0

        buf = self._tx_buf
        submitted = 0
        n = 0
        for f in frames:
            arbitration_id, data, is_extended_id = f
            data = bytes(data)[:8]
            m = buf[n]
            m.ID = int(arbitration_id)
            m.DataLen = len(data)
            m.ExternFlag = 1 if is_extended_id else 0
            m.RemoteFlag = 0
            m.FrameType = 0
            for i, b in enumerate(data):
                m.Data[i] = b
            n += 1
            if n >= self.TX_BATCH:
                try:
                    ret = self._dll.CAN_Transmit(
                        self._win_ch,
                        ctypes.cast(buf, ctypes.POINTER(CanMsg)),
                        n, 1000,
                    )
                except Exception:
                    return submitted
                if ret < 0:
                    return submitted
                submitted += (ret if ret > 0 else n)
                n = 0
        if n > 0:
            try:
                ret = self._dll.CAN_Transmit(
                    self._win_ch,
                    ctypes.cast(buf, ctypes.POINTER(CanMsg)),
                    n, 1000,
                )
            except Exception:
                return submitted
            if ret >= 0:
                submitted += (ret if ret > 0 else n)
        return submitted

    def recv(self, timeout: float = 0.0):
        """Return the next queued frame, or None on timeout."""
        deadline = time.monotonic() + max(0.0, timeout)
        with self._cond:
            while True:
                if self._queue:
                    return self._queue.popleft()
                remain = deadline - time.monotonic()
                if remain <= 0:
                    return None
                self._cond.wait(timeout=remain)

    def drain_rx(self, max_frames: int = 256):
        """Drop up to ``max_frames`` received frames from the queue.

        Used by the OTA burst sender to discard the device's per-frame write
        ACKs (or APP feedback) that piled up while the host was firing a large
        TX burst, so the RX queue does not grow unbounded and the next ACK the
        host actually waits for is found fast.
        """
        dropped = 0
        with self._cond:
            while self._queue and dropped < max_frames:
                self._queue.popleft()
                dropped += 1
        return dropped

    # -- lifecycle -----------------------------------------------------------

    def shutdown(self):
        self._stop_rx_thread()
        if self._is_open:
            try:
                self._dll.CAN_CloseDevice(self._win_ch)
            except Exception:
                pass
        self._is_open = False
        self._win_ch = -1

    def close(self):
        self.shutdown()


# ---------------------------------------------------------------------------
# optional python-can passthrough for other interfaces
# ---------------------------------------------------------------------------

class _PyCanShim:
    """Adapt a python-can BusABC to the send/recv surface used here."""

    def __init__(self, bus):
        self._b = bus

    def send(self, arbitration_id, data, is_extended_id=False) -> bool:
        import can  # local import; python-can only required on this path
        try:
            msg = can.Message(
                arbitration_id=int(arbitration_id),
                data=bytes(data)[:8],
                is_extended_id=bool(is_extended_id),
            )
            self._b.send(msg)
            return True
        except Exception:
            return False

    def send_many(self, frames) -> int:
        import can  # local import
        sent = 0
        batch = []
        for arbitration_id, data, is_extended_id in frames:
            batch.append((arbitration_id, data, is_extended_id))
            if len(batch) >= 512:
                # python-can has no batch API; send one by one but keep it tight.
                for a, d, e in batch:
                    try:
                        msg = can.Message(
                            arbitration_id=int(a),
                            data=bytes(d)[:8],
                            is_extended_id=bool(e),
                        )
                        self._b.send(msg, timeout=1.0)
                        sent += 1
                    except Exception:
                        pass
                batch = []
        for a, d, e in batch:
            try:
                msg = can.Message(
                    arbitration_id=int(a),
                    data=bytes(d)[:8],
                    is_extended_id=bool(e),
                )
                self._b.send(msg, timeout=1.0)
                sent += 1
            except Exception:
                pass
        return sent

    def recv(self, timeout=0.0):
        m = self._b.recv(timeout=timeout)
        if m is None:
            return None
        return CanFrame(
            arbitration_id=int(m.arbitration_id),
            data=bytes(m.data),
            is_extended_id=bool(m.is_extended_id),
        )

    def shutdown(self):
        try:
            self._b.shutdown()
        except Exception:
            pass

    def close(self):
        self.shutdown()


def open_can_bus(interface: str, channel, bitrate: int, dll_path=None):
    """Open a CAN bus.

    - interface ``hcanbus`` (default): use the bundled HCanbusBus + hcanbus.dll.
    - anything else: lazily fall back to python-can (``pip install python-can``).
    """
    interface = (interface or "hcanbus").lower()

    if interface == "hcanbus":
        path = dll_path or find_hcanbus_dll()
        if not path:
            raise RuntimeError(
                "hcanbus.dll not found. Copy it next to can_ota.py (or pass "
                "--dll PATH), or use a python-can interface after "
                "`pip install python-can`."
            )
        try:
            win_ch = int(channel)
        except (TypeError, ValueError):
            win_ch = 0
        return HCanbusBus(path, win_ch=win_ch, bitrate=bitrate)

    try:
        import can  # noqa: F401
    except ImportError as exc:
        raise RuntimeError(
            f"python-can is required for interface '{interface}': {exc}. "
            "Install it (`pip install python-can`) or use --interface hcanbus."
        )
    return _PyCanShim(can.interface.Bus(interface=interface, channel=channel, bitrate=bitrate))
