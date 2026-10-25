"""
Minimal DBC (CAN database) reader: just enough of the Vector DBC dialect to
list messages/signals for display and to encode a signal-value map into raw
CAN bytes for sending. Not a full grammar (see tools/generator/dbc for that,
used by the SCons build) - this is a small, dependency-free reader scoped to
what the web UI needs.
"""
import glob
import os
import re

_BU_RE = re.compile(r"^BU_\s*:\s*(.*)$")
_BO_RE = re.compile(r"^BO_\s+(\S+)\s+(\w+)\s*:\s*(\d+)\s+(\S+)")
_SG_RE = re.compile(
    r'^\s*SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*'
    r'\(([^,]+),([^)]+)\)\s*\[([^|]*)\|([^\]]*)\]\s*"([^"]*)"\s*(.*)$'
)
_BA_CYCLE_RE = re.compile(r'^BA_\s+"GenMsgCycleTime"\s+BO_\s+(\S+)\s+(\d+)\s*;')
_CM_BO_RE = re.compile(r'^CM_\s+BO_\s+(\S+)\s+"(.*)"\s*;')


def _parse_int(s):
    s = s.strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def _parse_float(s):
    s = s.strip()
    return float(s) if s else None


def parse_dbc(path):
    messages = {}
    order = []
    nodes = []
    current = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw_line in f:
            stripped = raw_line.strip()
            if not stripped:
                current = None
                continue

            m = _BU_RE.match(stripped)
            if m:
                nodes = m.group(1).split()
                continue

            m = _BO_RE.match(stripped)
            if m:
                msg_id = _parse_int(m.group(1))
                msg = {
                    "id": msg_id,
                    "id_hex": "0x%X" % msg_id,
                    "name": m.group(2),
                    "dlc": int(m.group(3)),
                    "node": m.group(4),
                    "cycle_time": None,
                    "comment": None,
                    "signals": [],
                }
                messages[msg_id] = msg
                order.append(msg_id)
                current = msg
                continue

            m = _SG_RE.match(raw_line.rstrip("\n"))
            if m and current is not None:
                receivers = [r.strip() for r in m.group(11).split(",") if r.strip()]
                current["signals"].append({
                    "name": m.group(1),
                    "start": int(m.group(2)),
                    "size": int(m.group(3)),
                    "endian": "little" if m.group(4) == "1" else "big",
                    "sign": m.group(5),
                    "factor": float(m.group(6)),
                    "offset": float(m.group(7)),
                    "min": _parse_float(m.group(8)),
                    "max": _parse_float(m.group(9)),
                    "unit": m.group(10),
                    "receivers": receivers,
                })
                continue

            m = _BA_CYCLE_RE.match(stripped)
            if m:
                mid = _parse_int(m.group(1))
                if mid in messages:
                    messages[mid]["cycle_time"] = int(m.group(2))
                continue

            m = _CM_BO_RE.match(stripped)
            if m:
                mid = _parse_int(m.group(1))
                if mid in messages:
                    messages[mid]["comment"] = m.group(2)
                continue

    return {
        "nodes": nodes,
        "messages": [messages[mid] for mid in order],
    }


def load_dbc_dir(dir_path):
    """Parse every *.dbc file in dir_path. Returns (files, message_index)."""
    files = []
    index = {}
    for path in sorted(glob.glob(os.path.join(dir_path, "*.dbc"))):
        parsed = parse_dbc(path)
        entry = {"filename": os.path.basename(path), **parsed}
        files.append(entry)
        for msg in parsed["messages"]:
            index[msg["name"]] = msg
    return files, index


def _signal_bit_positions(signal):
    """overall bit positions (byte*8+bit, LSB-of-byte=0) this signal occupies,
    ordered from the signal's LSB to its MSB."""
    size = signal["size"]
    if signal["endian"] == "little":
        return [signal["start"] + i for i in range(size)]

    # Motorola/big-endian DBC bit numbering: start is the MSB, bit index
    # counts down within a byte then wraps into the next byte.
    positions = []
    byte_idx, bit_idx = divmod(signal["start"], 8)
    for _ in range(size):
        positions.append(byte_idx * 8 + bit_idx)
        if bit_idx == 0:
            byte_idx += 1
            bit_idx = 7
        else:
            bit_idx -= 1
    positions.reverse()  # want LSB..MSB like the little-endian case
    return positions


def encode_message(msg, values):
    """values: {signal_name: physical_value}. Missing signals encode as 0."""
    frame_bits = 0
    for sig in msg["signals"]:
        if sig["name"] not in values or values[sig["name"]] is None:
            continue
        phys = float(values[sig["name"]])
        raw = round((phys - sig["offset"]) / sig["factor"]) if sig["factor"] else int(phys)
        size = sig["size"]
        raw &= (1 << size) - 1
        for i, pos in enumerate(_signal_bit_positions(sig)):
            if (raw >> i) & 1:
                frame_bits |= 1 << pos
    dlc = msg["dlc"]
    return bytes((frame_bits >> (i * 8)) & 0xFF for i in range(dlc))


def decode_message(msg, data: bytes):
    """Inverse of encode_message: {signal_name: physical_value} from raw bytes."""
    frame_bits = 0
    for i, b in enumerate(data):
        frame_bits |= b << (i * 8)
    values = {}
    for sig in msg["signals"]:
        raw = 0
        for i, pos in enumerate(_signal_bit_positions(sig)):
            if (frame_bits >> pos) & 1:
                raw |= 1 << i
        values[sig["name"]] = raw * sig["factor"] + sig["offset"]
    return values
