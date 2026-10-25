"""
ctypes binding to the AS-ONE canlib C API (see tools/libraries/Can/include/canlib.h),
loaded from the already-built build/<os>/GCC/one/libAsOne.so shared library.
"""
import ctypes
import os

CAN_MAX_MTU = 64
CAN_ID_EXTENDED = 0x80000000
# sentinel used by can_read_v2 to mean "any received message, in arrival order"
CAN_ID_ANY = 0xFFFFFFFF


class CanFrame(ctypes.Structure):
    _fields_ = [
        ("canid", ctypes.c_uint32),
        ("dlc", ctypes.c_uint8),
        ("data", ctypes.c_uint8 * CAN_MAX_MTU),
        ("timestamp", ctypes.c_uint64),
    ]


def find_asone_lib(explicit_path=None):
    if explicit_path:
        if os.path.exists(explicit_path):
            return explicit_path
        raise FileNotFoundError(explicit_path)

    env_path = os.environ.get("ASONE_LIB")
    if env_path:
        if os.path.exists(env_path):
            return env_path
        raise FileNotFoundError(env_path)

    here = os.path.dirname(os.path.abspath(__file__))
    src_root = os.path.abspath(os.path.join(here, "..", ".."))  # .../src
    osdir = "posix" if os.name == "posix" else "nt"
    libname = "libAsOne.so" if os.name == "posix" else "AsOne.dll"
    candidate = os.path.join(src_root, "build", osdir, "GCC", "one", libname)
    if os.path.exists(candidate):
        return candidate

    raise FileNotFoundError(
        "AsOne shared library not found at '%s'. Build it first, e.g.:\n"
        "  scons --lib=AsOne\n"
        "(run inside the docker dev container, from the 'src' directory)" % candidate
    )


class CanLib:
    def __init__(self, lib_path=None):
        self.lib_path = find_asone_lib(lib_path)
        self._lib = ctypes.CDLL(self.lib_path)
        lib = self._lib

        lib.can_open.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]
        lib.can_open.restype = ctypes.c_int

        lib.can_write.argtypes = [
            ctypes.c_int,
            ctypes.c_uint32,
            ctypes.c_uint8,
            ctypes.POINTER(ctypes.c_uint8),
        ]
        lib.can_write.restype = ctypes.c_bool

        lib.can_read_v2.argtypes = [ctypes.c_int, ctypes.POINTER(CanFrame)]
        lib.can_read_v2.restype = ctypes.c_bool

        lib.can_close.argtypes = [ctypes.c_int]
        lib.can_close.restype = ctypes.c_bool

    def open(self, device, port, baudrate):
        busid = self._lib.can_open(device.encode(), int(port), int(baudrate))
        if busid < 0:
            raise RuntimeError(
                "failed to open CAN device=%s port=%s baudrate=%s" % (device, port, baudrate)
            )
        return busid

    def close(self, busid):
        return bool(self._lib.can_close(busid))

    def write(self, busid, canid, data: bytes):
        if len(data) > CAN_MAX_MTU:
            raise ValueError("data too long (max %d bytes)" % CAN_MAX_MTU)
        buf = (ctypes.c_uint8 * len(data))(*data)
        return bool(self._lib.can_write(busid, ctypes.c_uint32(canid), len(data), buf))

    def read(self, busid):
        """Non-blocking: returns the next received frame (any CAN id, arrival order) or None."""
        frame = CanFrame()
        frame.canid = CAN_ID_ANY
        ok = bool(self._lib.can_read_v2(busid, ctypes.byref(frame)))
        if not ok:
            return None
        canid = frame.canid
        return {
            "canid": canid & ~CAN_ID_EXTENDED,
            "extended": bool(canid & CAN_ID_EXTENDED),
            "dlc": frame.dlc,
            "data": bytes(frame.data[: frame.dlc]),
            "timestamp": frame.timestamp,
        }
