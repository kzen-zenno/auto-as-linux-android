"""
AS-ONE Web CAN Tool - lightweight browser-based replacement for the
Qt UICan panel (see tools/asone/src/ui/UICan.cpp), reusing the already
built canlib (build/<os>/GCC/one/libAsOne.so) through ctypes.

Run (inside the docker dev container, from the 'src' directory):
    python3 tools/webcan/server.py --host 0.0.0.0 --port 8800

Then open http://localhost:8800/ in a browser (see compose.yaml port mapping).
"""
import argparse
import asyncio
import itertools
import platform
import threading
import time
from collections import deque
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Dict, Optional

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from canlib import CAN_ID_EXTENDED, CanLib
from dbcparser import decode_message, encode_message, load_dbc_dir

HERE = Path(__file__).resolve().parent
STATIC_DIR = HERE / "static"
DBC_DIR = HERE / "dbc"
TRACE_MAXLEN = 5000
AIRCON_COMMAND_CANID = 0x526

dbc_files, dbc_index = load_dbc_dir(str(DBC_DIR))


@asynccontextmanager
async def lifespan(_app: FastAPI):
    global manager
    loop = asyncio.get_event_loop()
    frame_queue: asyncio.Queue = asyncio.Queue()
    canlib = CanLib()
    manager = BusManager(canlib, loop, frame_queue)
    task = asyncio.create_task(_broadcaster(frame_queue))
    try:
        yield
    finally:
        task.cancel()
        if manager is not None:
            manager.close_all()


app = FastAPI(title="AS-ONE Web CAN Tool", lifespan=lifespan)

_seq_counter = itertools.count(1)


class Bus:
    def __init__(self, busid, device, port, baudrate):
        self.id = busid
        self.device = device
        self.port = port
        self.baudrate = baudrate
        self.stop_flag = threading.Event()
        self.thread = None

    def info(self):
        return {"id": self.id, "device": self.device, "port": self.port, "baudrate": self.baudrate}


class PeriodicSender:
    def __init__(self, key, bus_id, period_ms, canid, data, label, sweep=None):
        self.key = key
        self.bus_id = bus_id
        self.period_ms = period_ms
        self.canid = canid
        self.data = data
        self.label = label
        # None for a plain fixed-value periodic send; a dict of mutable sweep
        # state (see BusManager.start_sweep) when this sender auto-ramps a
        # signal between min/max instead of holding one constant value.
        self.sweep = sweep
        self.stop_flag = threading.Event()
        self.thread = None

    def info(self):
        info = {
            "key": self.key,
            "bus": self.bus_id,
            "period_ms": self.period_ms,
            "canid": "%X" % self.canid,
            "data": self.data.hex().upper(),
            "label": self.label,
            "mode": "held" if (self.sweep and self.sweep["frozen"]) else ("sweep" if self.sweep else "fixed"),
        }
        if self.sweep:
            info.update(
                {
                    "value": round(self.sweep["value"], 3),
                    "min": self.sweep["min"],
                    "max": self.sweep["max"],
                    "step": self.sweep["step"],
                    "step_interval_ms": self.sweep["step_interval_ms"],
                }
            )
        return info


class BusManager:
    def __init__(self, canlib: CanLib, loop: asyncio.AbstractEventLoop, frame_queue: asyncio.Queue):
        self.canlib = canlib
        self.loop = loop
        self.frame_queue = frame_queue
        self.buses = {}
        self.periodic = {}
        self.lock = threading.Lock()
        # HVAC ECU emulation, see _emulate_hvac()
        self.hvac_emulation = True
        self._last_aircon_cmd = {}

    def open_bus(self, device, port, baudrate):
        busid = self.canlib.open(device, port, baudrate)
        bus = Bus(busid, device, port, baudrate)
        bus.thread = threading.Thread(target=self._reader, args=(bus,), daemon=True)
        with self.lock:
            self.buses[busid] = bus
        bus.thread.start()
        return bus

    def close_bus(self, busid):
        with self.lock:
            bus = self.buses.pop(busid, None)
            stale_keys = [k for k, s in self.periodic.items() if s.bus_id == busid]
        for key in stale_keys:
            self.stop_periodic(key, missing_ok=True)
        if bus is None:
            raise KeyError(busid)
        bus.stop_flag.set()
        bus.thread.join(timeout=2)
        self.canlib.close(busid)

    def start_periodic(self, bus_id, key, period_ms, canid, data: bytes, label=""):
        with self.lock:
            if bus_id not in self.buses:
                raise KeyError(bus_id)
        self.stop_periodic(key, missing_ok=True)
        period_ms = max(10, int(period_ms))
        sender = PeriodicSender(key, bus_id, period_ms, canid, data, label)

        def run():
            while not sender.stop_flag.is_set():
                try:
                    self.send(bus_id, canid, data)
                except KeyError:
                    break
                if sender.stop_flag.wait(period_ms / 1000.0):
                    break

        sender.thread = threading.Thread(target=run, daemon=True)
        with self.lock:
            self.periodic[key] = sender
        sender.thread.start()
        return sender

    def start_sweep(self, bus_id, key, msg, signal_name, min_v, max_v, step, step_interval_ms, period_ms):
        """Like start_periodic, but re-encodes the frame every tick from a
        value that auto-ramps min_v -> max_v -> min_v (bounces at the ends)
        instead of holding one fixed payload. Shares start_periodic's key
        scheme ("sig:<bus>:<message>"), so starting a sweep replaces a
        manual fixed send for the same message and vice versa -- only one
        active sender per message per bus, same as before.
        """
        with self.lock:
            if bus_id not in self.buses:
                raise KeyError(bus_id)
        self.stop_periodic(key, missing_ok=True)
        period_ms = max(10, int(period_ms))
        step_interval_ms = max(period_ms, int(step_interval_ms))
        ticks_per_step = max(1, round(step_interval_ms / period_ms))
        canid = msg["id"]
        sweep = {
            "msg": msg,
            "signal": signal_name,
            "value": min_v,
            "min": min_v,
            "max": max_v,
            "step": abs(step),
            "direction": 1,
            "step_interval_ms": step_interval_ms,
            "frozen": False,
        }
        initial_data = encode_message(msg, {signal_name: min_v})
        sender = PeriodicSender(key, bus_id, period_ms, canid, initial_data, label=msg["name"], sweep=sweep)

        def run():
            ticks = 0
            while not sender.stop_flag.is_set():
                data = encode_message(sweep["msg"], {sweep["signal"]: sweep["value"]})
                sender.data = data
                try:
                    self.send(bus_id, canid, data)
                except KeyError:
                    break
                if sender.stop_flag.wait(period_ms / 1000.0):
                    break
                if sweep["frozen"]:
                    continue
                ticks += 1
                if ticks >= ticks_per_step:
                    ticks = 0
                    value = sweep["value"] + sweep["direction"] * sweep["step"]
                    if value >= sweep["max"]:
                        value = sweep["max"]
                        sweep["direction"] = -1
                    elif value <= sweep["min"]:
                        value = sweep["min"]
                        sweep["direction"] = 1
                    sweep["value"] = value

        sender.thread = threading.Thread(target=run, daemon=True)
        with self.lock:
            self.periodic[key] = sender
        sender.thread.start()
        return sender

    def freeze_periodic(self, key):
        """User-facing "Stop": don't kill transmission, just stop the value
        from changing further and keep sending its current payload forever.
        A manually-set or mid-sweep value used to go silent on Stop, which
        made the receiving side (FmwApp) time out and fall back to its
        SYS-001 default/fail-safe display within a couple hundred ms --
        confusing when the whole point of stopping was to hold the value you
        just set. A no-op for an already-fixed sender (nothing to freeze).
        """
        with self.lock:
            sender = self.periodic.get(key)
        if sender is None:
            raise KeyError(key)
        if sender.sweep:
            sender.sweep["frozen"] = True
        return sender

    def stop_periodic(self, key, missing_ok=False):
        with self.lock:
            sender = self.periodic.pop(key, None)
        if sender is None:
            if missing_ok:
                return
            raise KeyError(key)
        sender.stop_flag.set()
        if sender.thread:
            sender.thread.join(timeout=2)

    def list_periodic(self):
        with self.lock:
            senders = list(self.periodic.values())
        return [s.info() for s in senders]

    def send(self, busid, canid, data: bytes):
        with self.lock:
            if busid not in self.buses:
                raise KeyError(busid)
        ok = self.canlib.write(busid, canid, data)
        if ok:
            # the simulator does not loop a bus's own TX back into its own RX
            # (see simulator_can_v2.cpp rx_notifiy's uuid check), so echo it
            # into the trace ourselves for a usable single-window test flow.
            self._emit(
                bus=busid,
                direction="tx",
                canid=canid & ~CAN_ID_EXTENDED,
                extended=bool(canid & CAN_ID_EXTENDED),
                dlc=len(data),
                data=data,
                timestamp=0,
            )
        return ok

    def list_buses(self):
        with self.lock:
            return [b.info() for b in self.buses.values()]

    def close_all(self):
        with self.lock:
            ids = list(self.buses.keys())
        for busid in ids:
            try:
                self.close_bus(busid)
            except KeyError:
                pass

    def _reader(self, bus: Bus):
        while not bus.stop_flag.is_set():
            frame = self.canlib.read(bus.id)
            if frame is None:
                time.sleep(0.001)
                continue
            if self.hvac_emulation and frame["canid"] == AIRCON_COMMAND_CANID:
                try:
                    self._emulate_hvac(bus.id, frame["data"])
                except Exception as e:  # never let the emulator kill the reader
                    print("webcan: HVAC emulation failed: %r" % (e,), flush=True)
            self._emit(
                bus=bus.id,
                direction="rx",
                canid=frame["canid"],
                extended=frame["extended"],
                dlc=frame["dlc"],
                data=frame["data"],
                timestamp=frame["timestamp"],
            )

    def _emulate_hvac(self, bus_id, data: bytes):
        """Stand-in for the FMW_HVAC ECU, which does not exist in the simulator.

        The gateway's HVAC write (SOME/IP WriteVehicleState) ends as an
        AirconCommand frame (0x526); the new state is only ever reported back
        by the HVAC ECU in the Aircon frame (0x323, SYS-001: "the Aircon status
        frame is authoritative"). Without an ECU nothing answers, so the state
        never changes. This applies a command to the Aircon periodic sender.

        A command field is applied when its value or its MM_*_Cmd gate changes
        compared with the previous AirconCommand frame, not on every frame, so
        that values set by hand in the UI are not overwritten 100 ms later by
        a command that stays asserted on the bus. CmdMode has no gate bit and
        is applied on change only.
        """
        cmd_msg = dbc_index.get("AirconCommand")
        state_msg = dbc_index.get("Aircon")
        if cmd_msg is None or state_msg is None or len(data) < cmd_msg["dlc"]:
            return
        cmd = decode_message(cmd_msg, bytes(data))
        raw_temp = data[2]  # CmdTargetTempRaw, 0xFF = NoUpdate
        last = self._last_aircon_cmd.get(bus_id)
        self._last_aircon_cmd[bus_id] = cmd

        def changed(*names):
            return last is None or any(last[n] != cmd[n] for n in names)

        key = "sig:%d:Aircon" % bus_id
        with self.lock:
            sender = self.periodic.get(key)
        if sender is not None and sender.sweep:
            return  # a swept Aircon signal is under manual control
        state = decode_message(state_msg, sender.data) if sender else {}
        new = dict(state)
        for name in ("AC_OnOff", "Defrost", "Mode", "FanLevel", "TargetTempRaw"):
            new.setdefault(name, 16.0 + 8 * 0.5 if name == "TargetTempRaw" else 0.0)

        if cmd["MM_AC_Cmd"] and changed("MM_AC_Cmd", "CmdAC_OnOff"):
            new["AC_OnOff"] = cmd["CmdAC_OnOff"]
        if cmd["MM_Def_Cmd"] and changed("MM_Def_Cmd", "CmdDefrost"):
            new["Defrost"] = cmd["CmdDefrost"]
        if cmd["MM_Fan_Cmd"] and changed("MM_Fan_Cmd", "CmdFanLevel"):
            new["FanLevel"] = cmd["CmdFanLevel"]
        if cmd["MM_Temp_Cmd"] and raw_temp != 0xFF and changed("MM_Temp_Cmd", "CmdTargetTempRaw"):
            new["TargetTempRaw"] = cmd["CmdTargetTempRaw"]
        if last is not None and last["CmdMode"] != cmd["CmdMode"]:
            new["Mode"] = cmd["CmdMode"]

        if sender is not None and new == state:
            return
        period_ms = sender.period_ms if sender else (state_msg.get("cycle_time") or 100)
        self.start_periodic(
            bus_id, key, period_ms, state_msg["id"], encode_message(state_msg, new), label="Aircon"
        )

    def _emit(self, *, bus, direction, canid, extended, dlc, data: bytes, timestamp):
        item = {
            "seq": next(_seq_counter),
            "bus": bus,
            "dir": direction,
            "canid": "%X" % canid,
            "extended": extended,
            "dlc": dlc,
            "data": data.hex().upper(),
            "ascii": "".join(chr(b) if 32 <= b < 127 else "." for b in data),
            "timestamp": timestamp,
            "time": time.time(),
        }
        try:
            self.loop.call_soon_threadsafe(self.frame_queue.put_nowait, item)
        except RuntimeError:
            # event loop already stopped (shutting down)
            pass


trace_buffer: deque = deque(maxlen=TRACE_MAXLEN)
ws_clients: set = set()
manager: BusManager = None  # set on startup


class OpenBusRequest(BaseModel):
    device: str = "simulator_v2"
    port: int = 0
    baudrate: int = 500000


class SendFrameRequest(BaseModel):
    canid: str
    data: str = ""
    extended: bool = False


class SendSignalsRequest(BaseModel):
    message: str
    signals: Dict[str, float] = {}


class PeriodicFrameRequest(BaseModel):
    canid: str
    data: str = ""
    extended: bool = False
    period_ms: int = 100


class PeriodicSignalsRequest(BaseModel):
    message: str
    signals: Dict[str, float] = {}
    period_ms: int = 100


class PeriodicSweepRequest(BaseModel):
    message: str
    signal: str
    min: float
    max: float
    step: float
    step_interval_ms: int = 200
    period_ms: Optional[int] = None  # default: the message's own DBC cycle_time


def parse_canid(s: str) -> int:
    s = s.strip()
    if s.lower().startswith("0x"):
        s = s[2:]
    if not s:
        raise ValueError("empty CAN id")
    return int(s, 16)


def parse_data(s: str) -> bytes:
    s = s.strip().replace(" ", "")
    if not s:
        return b""
    try:
        return bytes.fromhex(s)
    except ValueError:
        raise ValueError("data must be a hex string, e.g. '1122AABB'")


async def _broadcaster(frame_queue: asyncio.Queue):
    while True:
        item = await frame_queue.get()
        trace_buffer.append(item)
        dead = []
        for ws in list(ws_clients):
            try:
                await ws.send_json({"type": "frame", "data": item})
            except Exception:
                dead.append(ws)
        for ws in dead:
            ws_clients.discard(ws)


@app.get("/api/devices")
def list_devices():
    devices = ["simulator_v2", "simulator", "qs"]
    if platform.system() == "Windows":
        devices += ["peak", "peakfd", "vxl", "zlg"]
    return {"devices": devices}


@app.get("/api/buses")
def list_buses():
    return manager.list_buses()


@app.post("/api/buses", status_code=201)
def open_bus(req: OpenBusRequest):
    try:
        bus = manager.open_bus(req.device, req.port, req.baudrate)
    except RuntimeError as e:
        raise HTTPException(400, str(e))
    return bus.info()


@app.delete("/api/buses/{bus_id}")
def close_bus(bus_id: int):
    try:
        manager.close_bus(bus_id)
    except KeyError:
        raise HTTPException(404, "bus not found")
    return {"ok": True}


@app.post("/api/buses/{bus_id}/send")
def send_frame(bus_id: int, req: SendFrameRequest):
    try:
        canid = parse_canid(req.canid)
        data = parse_data(req.data)
    except ValueError as e:
        raise HTTPException(400, str(e))
    if req.extended:
        canid |= 0x80000000
    try:
        ok = manager.send(bus_id, canid, data)
    except KeyError:
        raise HTTPException(404, "bus not found")
    if not ok:
        raise HTTPException(500, "can_write failed")
    return {"ok": True}


@app.post("/api/buses/{bus_id}/send_signals")
def send_signals(bus_id: int, req: SendSignalsRequest):
    msg = dbc_index.get(req.message)
    if msg is None:
        raise HTTPException(404, "message '%s' not found in loaded DBC" % req.message)
    data = encode_message(msg, req.signals)
    try:
        ok = manager.send(bus_id, msg["id"], data)
    except KeyError:
        raise HTTPException(404, "bus not found")
    if not ok:
        raise HTTPException(500, "can_write failed")
    return {"ok": True, "canid": msg["id_hex"], "data": data.hex().upper()}


@app.get("/api/dbc")
def get_dbc():
    return {"files": dbc_files}


@app.post("/api/buses/{bus_id}/periodic/frame")
def start_periodic_frame(bus_id: int, req: PeriodicFrameRequest):
    try:
        canid = parse_canid(req.canid)
        data = parse_data(req.data)
    except ValueError as e:
        raise HTTPException(400, str(e))
    if req.extended:
        canid |= 0x80000000
    key = "frame:%d:%X" % (bus_id, canid)
    try:
        sender = manager.start_periodic(bus_id, key, req.period_ms, canid, data, label=req.canid)
    except KeyError:
        raise HTTPException(404, "bus not found")
    return sender.info()


@app.post("/api/buses/{bus_id}/periodic/signals")
def start_periodic_signals(bus_id: int, req: PeriodicSignalsRequest):
    msg = dbc_index.get(req.message)
    if msg is None:
        raise HTTPException(404, "message '%s' not found in loaded DBC" % req.message)
    data = encode_message(msg, req.signals)
    key = "sig:%d:%s" % (bus_id, req.message)
    try:
        sender = manager.start_periodic(bus_id, key, req.period_ms, msg["id"], data, label=req.message)
    except KeyError:
        raise HTTPException(404, "bus not found")
    return sender.info()


@app.post("/api/buses/{bus_id}/periodic/sweep")
def start_periodic_sweep(bus_id: int, req: PeriodicSweepRequest):
    msg = dbc_index.get(req.message)
    if msg is None:
        raise HTTPException(404, "message '%s' not found in loaded DBC" % req.message)
    sig = next((s for s in msg["signals"] if s["name"] == req.signal), None)
    if sig is None:
        raise HTTPException(404, "signal '%s' not found in message '%s'" % (req.signal, req.message))
    if req.step <= 0:
        raise HTTPException(400, "step must be positive")
    if req.max <= req.min:
        raise HTTPException(400, "max must be greater than min")
    period_ms = req.period_ms or msg.get("cycle_time") or 100
    key = "sig:%d:%s" % (bus_id, req.message)
    try:
        sender = manager.start_sweep(
            bus_id, key, msg, req.signal, req.min, req.max, req.step, req.step_interval_ms, period_ms
        )
    except KeyError:
        raise HTTPException(404, "bus not found")
    return sender.info()


@app.delete("/api/periodic/{key}")
def stop_periodic(key: str):
    # "Stop" holds the last value (freeze_periodic) rather than killing
    # transmission -- see freeze_periodic's docstring. start_periodic/
    # start_sweep still do a real kill+replace internally when a new sender
    # for the same key takes over, and close_bus still tears everything down.
    try:
        manager.freeze_periodic(key)
    except KeyError:
        raise HTTPException(404, "periodic sender not found")
    return {"ok": True}


class HvacEmulationRequest(BaseModel):
    enabled: bool


@app.get("/api/hvac_emulation")
def get_hvac_emulation():
    return {"enabled": manager.hvac_emulation}


@app.post("/api/hvac_emulation")
def set_hvac_emulation(req: HvacEmulationRequest):
    manager.hvac_emulation = req.enabled
    return {"enabled": manager.hvac_emulation}


@app.get("/api/periodic")
def get_periodic():
    return manager.list_periodic()


@app.get("/api/trace")
def get_trace(limit: int = 200):
    items = list(trace_buffer)
    return items[-limit:]


@app.delete("/api/trace")
def clear_trace():
    trace_buffer.clear()
    return {"ok": True}


@app.websocket("/ws")
async def ws_endpoint(websocket: WebSocket):
    await websocket.accept()
    ws_clients.add(websocket)
    try:
        for item in list(trace_buffer)[-100:]:
            await websocket.send_json({"type": "frame", "data": item})
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        ws_clients.discard(websocket)


app.mount("/", StaticFiles(directory=str(STATIC_DIR), html=True), name="static")


def main():
    import uvicorn

    parser = argparse.ArgumentParser(description="AS-ONE Web CAN Tool")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8800)
    args = parser.parse_args()
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
