# FmwApp: Building an AUTOSAR-Style CAN App From a Signal Spec

This page documents three things:

1. **The reusable workflow** for turning a CAN signal spec document into a running app in this
   repository (config files + generator tools + SConscript), including every pitfall hit while
   building the first app this way.
2. **The implementation spec of `FmwApp`**, the sample app built with that workflow from
   [specs/sys/SYS-001-can-signal.md](../specs/sys/SYS-001-can-signal.md).
3. **How to test it**: the `lssg`/`wrsg` shell commands for introspecting signals live, and
   `webcan`, a browser-based CAN tool used to drive/observe `FmwApp` without a real bus.

Read part 1 if you are about to build a new app from a spec. Read part 2 if you need to know what
`FmwApp` actually does. Read part 3 to actually exercise it.

## Part 1: Tool Workflow

### 1.1 What the generator toolchain actually is

This repository does not hand-write the AUTOSAR BSW config C files. Three Python generators under
[tools/generator/](../src/tools/generator/) (`Com.py`, `CanIf.py`, `PduR.py`) read small JSON files plus
one DBC file, and emit `Com_Cfg.c/h`, `CanIf_Cfg.c/h`, `PduR_Cfg.c/h` into a `GEN/` subfolder next to
the JSON sources. SCons re-runs the generator automatically whenever the JSON/DBC source is newer
than the generated file — you never invoke the generator by hand, you just edit the JSON/DBC and run
`scons`.

For a signal-only app (no diagnostics, no network management) you need exactly four source files:

```
src/app/<name>/config/Com/CAN0.dbc      # message + signal layout, authoritative
src/app/<name>/config/Com/Com.json      # Com network config, reads the dbc
src/app/<name>/config/Com/CanIf.json    # CAN-ID <-> L-PDU routing table
src/app/<name>/config/Com/PduR.json     # PDU routing between CanIf and Com
```

Everything else (application C code, `SConscript`) is boilerplate you copy from an existing minimal
app such as `ApplicationOsekNm`/`ApplicationCanNm` in [app/app/SConscript](../src/app/app/SConscript), or
from [app/fmw/SConscript](../src/app/fmw/SConscript) itself.

### 1.2 Writing the DBC file

The DBC parser lives at [tools/generator/dbc/](../src/tools/generator/dbc/) (a `ply` lex/yacc grammar in
`asclex.py` / `ascyacc.py`). It accepts a normal, tool-compatible DBC (openable in `cantools`,
CANdb++, SavvyCAN, ...), so write a real DBC rather than a minimal one. The parts that matter to the
generator:

- `BO_ <id> <Name>: <dlc> <SenderNode>` — `<id>` may be written in hex (`0x120`) or decimal.
- `SG_ <name> : <start>|<size>@<endian><sign> (<factor>,<offset>) [<min>|<max>] "<unit>" <Receiver>`
  — `endian` is `1` for Intel/little (the usual case for a multi-byte value with `start` = the bit
  index of its LSB, i.e. `byte*8 + bit`), `0` for Motorola/big. `sign` is `+`/`-`.
- `BA_ "GenMsgCycleTime" BO_ <id> <ms>;` (with matching `BA_DEF_ BO_ "GenMsgCycleTime" INT 0
  100000;` / `BA_DEF_DEF_ "GenMsgCycleTime" 0;`) — this is how a message's `CycleTime` reaches
  `Com.json` without repeating it there. **Cycle time is per-message, not per-signal.**
- Whichever `BU_` node equals `Com.json`'s `"me"` is the app itself: a message whose `BO_` sender is
  `"me"` becomes a **Tx** message for this app; any other sender makes it an **Rx** message. There is
  no way to mark a message Rx/Tx directly — it is entirely derived from the sender node name.

See [app/fmw/config/Com/CAN0.dbc](../src/app/fmw/config/Com/CAN0.dbc) for a full 12-message, ~40-signal
example, including how a bit-packed status byte is modeled as one `SG_` per flag (plus one `SG_
..Reserved..` covering the unused bits, so the app can validate them — see 1.6).

### 1.3 Com.json: connecting the DBC to Com

```json
{
  "class": "Com",
  "networks": [
    {
      "name": "CAN0",
      "network": "CAN",
      "device": "simulator_v2",
      "port": 0,
      "baudrate": 500000,
      "me": "AS",
      "use_dbc": true,
      "dbc": "CAN0.dbc",
      "timeout_factor": 3,
      "enable_message_rx_notificaiton": true,
      "enable_message_rx_timeout_notificaiton": true
    }
  ]
}
```

- `"dbc"` alone is enough to pull in *every* message from the DBC as a Com IPDU — you do not need to
  restate them under a `"messages"` array (that array is only for IPDUs that have no DBC entry, e.g.
  a SecOC-wrapped PDU; see `SECOC_MSG0`/`SECOC_MSG1` in
  [app/app/config/Com/Com.json](../src/app/app/config/Com/Com.json) for that pattern).
- `"timeout_factor"` multiplies each message's `GenMsgCycleTime` to get its Rx supervision timeout.
  Set this once, and make sure every message's spec-mandated timeout really is
  `CycleTime * timeout_factor` for the same factor — `FmwApp`'s spec happened to use exactly `x3` for
  every one of its 11 Rx messages, which is what made a single network-level `timeout_factor` valid.
  If your spec's timeouts don't share one ratio, you must set a per-message `"Timeout"` override
  instead (see `Com.py`'s handling of `msg["Timeout"]`).
- `"enable_message_rx_notificaiton"` / `"enable_message_rx_timeout_notificaiton"` (note: both spelled
  with this exact typo in the generator, `notificaiton`) make the generator emit, for every Rx
  message, an `extern void <Msg>_RxNotification(void);` called on every valid reception and an
  `extern void <Msg>_RxTimeout(void);` called by Com's own supervision timer. **You implement both in
  your app's C code** — see 1.7.
- **`"port"` is not actually used to select the physical CAN channel** — see the warning in 1.5.

### 1.4 CanIf.json / PduR.json: routing, and the naming trap

For each DBC message you must add one `RxPdus` (or `TxPdus`) entry to `CanIf.json`, and one routine
to `PduR.json`, using **the same name** on both sides. `CanIf.py` and `PduR.py` do support
auto-deriving these directly from `"dbc"` the same way `Com.py` does — **do not use that shortcut**.
It has a naming mismatch with Com's own output:

- `Com.py` names each message's internal PduId macro `COM_<Network>_<MacroCasedName>`, where
  `MacroCasedName` splits the DBC message name on word boundaries and upper-cases it
  (`TacoMeter` → `TACO_METER`, `AirconCommand` → `AIRCON_COMMAND`, `DoorOpen` → `DOOR_OPEN`,
  single-word names like `Speed`/`ESC`/`ABS` are unaffected).
- `CanIf.py`'s own `"dbc"` auto-derivation does the **same** macro-casing for its Pdu names.
- `PduR.py`'s own `"dbc"` auto-derivation does **not** — it keeps the DBC's original casing
  (`CAN0_TacoMeter_RX`).

Mixing the two auto-derivations therefore produces `PduR_Cfg.c` referencing a Com symbol
(`COM_CAN0_TacoMeter_RX`) that doesn't exist (the real one is `COM_CAN0_TACO_METER_RX`) — a link
error. The fix used here is to **hand-write** `CanIf.json` and `PduR.json` with names already in the
macro-cased form, e.g. `CAN0_TACO_METER_RX`, so both files agree with what `Com.py` actually
generates:

```json
// CanIf.json
{ "name": "CAN0_TACO_METER_RX", "id": "0x121", "hoh": 0, "up": "PduR" }
```
```json
// PduR.json
{ "name": "CAN0_TACO_METER_RX", "from": "CanIf", "to": "Com" }
```

The generic recipe: for a message called `Foo` where this app receives it, use
`CAN0_<SNAKE_CASE(Foo)>_RX` everywhere in `CanIf.json`/`PduR.json`; for one this app transmits, use
`CAN0_<SNAKE_CASE(Foo)>_TX` and reverse `"from"/"to"` to `"from": "Com", "to": "CanIf"`. **Verify by
building once and grepping the generated header** rather than guessing the casing by hand:

```sh
grep '#define COM_CAN0_' src/app/<name>/config/Com/GEN/Com_Cfg.h
```

If `CanIf.json`/`PduR.json` disagree with that output, the build fails at the link step with
`undefined reference to 'COM_CAN0_...'` — that is exactly this bug, not a mystery to debug from
scratch.

### 1.5 Runtime gotcha: `Com.json`'s `"port"` does not select the CAN channel

The actual `can_open(device, port, baudrate)` call for CAN controller channel *N* comes from a
**static, shared, non-generated** table in
[app/platform/simulator/src/config/Can_Cfg.c](../src/app/platform/simulator/src/config/Can_Cfg.c):
`Can_ChannelConfigs[N].hwInstanceId == N`. `CanIf.json`'s network maps to that channel purely via its
position (`CAN0` → hoh 0 → channel 0 → port 0), regardless of what `Com.json`'s `"port"` field says.
Keep `Com.json`'s `"port"` in sync with the real channel index (0 for `CAN0`, 1 for `CAN1`, ...) so
the file stays honest, but do not expect changing it to move the app to a different UDP multicast
port — it won't. This also means **every app's `CAN0` shares the same simulated bus** (port 0) unless
you introduce a second CAN controller channel; two apps' `CAN0` networks with disjoint CAN IDs can
safely coexist on it (which is how `FmwApp` and `CanApp` were cross-tested against each other, see
2.6).

### 1.6 Runtime gotcha: log level filtering hides your own logs

`ASLOG(<TAG>, (...))` is gated by `AS_LOG_<TAG> >= AS_LOG_DEFAULT`. If you `#define AS_LOG_FMW 0`
(the usual "always show this file's debug output" idiom used across this codebase, e.g. `AS_LOG_APP
0` in [app/app/src/app.c](../src/app/app/src/app.c)) but leave the app's global default at `INFO`(1), a
custom tag at level 0 is **silently dropped** — it looks exactly like "nothing is being received",
which is not a CAN/Com bug at all. Run with `-v 0` (posix `main.c` already exposes this via `getopt`)
or set `AS_LOG_<TAG>=0` in the environment before concluding that a message isn't arriving.

### 1.7 Runtime gotcha: `Com_IpduGroupStart` resets Tx buffers *after* your init code runs

`EcuM_AL_EnterRUN()` in `main.c` calls `Com_IpduGroupStart(0, TRUE)` for `USE_COM` apps without
`USE_CANNM`. The `TRUE` (`initialize`) re-initializes every IPDU's buffer to its DBC `InitialValue`
(0 unless you set `GenSigStartValue`), and this happens **after** `App_Init()`/your own init hook has
already run. A one-shot `Com_SendSignal(...)` call from your init function is therefore silently
wiped before the first transmission goes out.

The fix has two parts, and it matters which one you pick:

- **Don't** re-assert forever on every tick. The first version of this app did exactly that (call
  `Com_SendSignal` for every field, every 10 ms, unconditionally) to survive the one-time
  `Com_IpduGroupStart` race — but it also means anything else that changes the same signal later
  (a shell `wrsg`, a future dynamic command API) gets **overwritten again on the very next tick**,
  making the write look like it silently "doesn't work".
- **Do** re-assert only for a short bounded window after boot (`FmwApp` uses 100 ticks = 1 s, see
  `Fmw_ApplyAirconCommand()`/`Fmw_MainFunction10ms()` in 2.4), then stop. `EnterRUN` only fires once
  for the lifetime of the process (as long as `App_IsIgOn()` stays `TRUE`, which is the default), so
  a 1 s window comfortably covers the race with room to spare, and after it elapses Com's own
  cyclic-Tx timer just keeps re-sending whatever is currently in the buffer — including anything a
  `wrsg` wrote afterward (see 1.12).

### 1.8 Runtime gotcha: `RxTimeout` re-fires periodically, not once

Once a message goes stale, Com's own supervision timer calls `<Msg>_RxTimeout()` again and again
(roughly every timeout period) for as long as no frame arrives — not a single edge-triggered call.
Any one-shot bookkeeping in that callback (a log line, a "reset recovery-hysteresis counter" side
effect, a confidence-score reset) must be guarded to run only on the **first** re-entrant call after
leaving `OK`, or it will keep clobbering state a later valid reception is trying to build up (see
`Fmw_OnTimeout()`'s `isNewTransition` return value in 2.3 — this exact bug was hit and fixed once
already).

### 1.9 Minimal SConscript for a Com/CanIf/PduR-only app

```python
from building import *

CWD = GetCurrentDir()
objsApp = Glob("*.c") + Glob("src/*.c")

libsForFmwApp = {
    "Com": Glob("config/Com/Com.json"),
    "PduR": Glob("config/Com/PduR.json"),
    "CanIf": Glob("config/Com/CanIf.json"),
}

@register_application
class ApplicationFmwApp(Application):
    def platform_config(self):
        self.Append(CPPDEFINES=["USE_STD_DEBUG"])
        self.LIBS.append("Simulator")  # posix/Windows CAN simulator backend

    def config(self):
        self.CPPDEFINES = []
        self.CPPPATH = ["$INFRAS", "%s/include" % (CWD), CWD, "$Com_Cfg", "$EcuM_Cfg"]
        self.LIBS = ["StdTimer", "EcuM"]
        for libName, source in libsForFmwApp.items():
            self.LIBS.append(libName)
            self.RegisterConfig(libName, source)
            self.Append(CPPDEFINES=["USE_%s" % (libName.split(":")[0].upper())])
        self.source = objsApp
        self.Append(CPPDEFINES=["USE_CAN"])
        self.platform_config()
```

`register_application` derives the buildable app name from the class name by stripping the
`Application` prefix, so this registers as `FmwApp` (`scons --app=FmwApp`). `main.c` is copied
unmodified from [app/app/main.c](../src/app/app/main.c): it is already written defensively behind
`#ifdef USE_XXX` for every optional BSW module, so a reduced `libsForApp` (here: only
`Com`/`PduR`/`CanIf`, no `Dcm`/`NvM`/`SecOC`/`Xcp`/`CanNm`/`CanSM`/`ComM`) compiles and runs correctly
without touching it — `EcuM_AL_EnterRUN()`'s `#if defined(USE_COM) && !defined(USE_CANNM):
Com_IpduGroupStart(0, TRUE)` branch is exactly the one that fires. `src/app.c` was likewise copied and
only trimmed of the old app's message-specific demo code.

### 1.10 Build and run (Docker)

Same container as every other app in this repo (see [build.md](build.md) for the full Compose setup).

```sh
docker exec autoas-dev bash -lc 'cd /workspace && scons --app=FmwApp'
docker exec autoas-dev bash -lc '/workspace/build/posix/GCC/FmwApp/FmwApp -v 0'
```

### 1.11 Keep ASLOG for events, not for every frame

The first cut of `fmw_signals.c` logged every successful `RxNotification` (`ASLOG(FMW, ("FMW:
Speed=%.2f km/h ... quality=%s\n", ...))` — one line per reception, so up to 20/s for a 50 ms
message). That is genuinely useless once more than one or two messages are flowing: it drowns the
few lines that actually matter (a `STALE` transition, an `INVALID` value, a reserved-bit violation)
in noise, and — because `AS_LOG_FMW` defaults to level 0 (see 1.6) — the only way to see any of it
at all is `-v 0`, which *also* turns on unrelated infra chatter (`PDUR_Transmit`/`PduR_RxIndication`
from `PduR.c`, `CANIF: RX CAN ID=...` from `CanIf.c`, both logged at their own level-0 tags for the
same reason).

The fix applied here: delete the per-frame success log line from every `RxNotification` (11 of
them), keep only the ones that fire on a genuine state change (`Fmw_OnTimeout`'s `STALE` edge,
`Fmw_OnInvalid`, the fail-safe activation lines, the reserved-bit `ERROR` lines). For "what is this
signal's value *right now*" there is a much better tool than logging every frame: 1.12.

### 1.12 `lssg` / `wrsg`: inspect and poke live signal values from the shell

Add shell support and you get this for free — [infras/communication/Com/Com.c](../src/infras/communication/Com/Com.c)
already implements `lssg`/`wrsg` as generic Com-level shell commands, gated only on `USE_SHELL`
being defined (no app-specific code needed):

```python
# SConscript platform_config()
self.Append(CPPDEFINES=["USE_SHELL"])
self.LIBS.append("Shell")
```

`main.c` (copied verbatim, see 1.9) already calls `Shell_Init()`/`Shell_MainFunction()` behind
`#ifdef USE_SHELL`, so this is the only change needed. Run interactively (a plain `-v` pipe/redirect
won't do — the shell reads from a real stdin) and you get a `as #` prompt:

```sh
docker exec -it autoas-dev bash -lc '/workspace/build/posix/GCC/FmwApp/FmwApp -v 1'
as # lssg
0 AirconCommand_TX
1 Speed_RX
2 TacoMeter_RX
...
as # lssg 1
R Speed_RX.VehicleSpeedRaw(SID=11): V = 0x1388(5000)
as # wrsg 11 5000
```

Three things that are not obvious from the command name alone:

- **`sid` is a global index**, not a per-message one. `lssg` (no args) lists IPDUs by index
  (`AirconCommand_TX` = 0, `Speed_RX` = 1, ...); `lssg <ipdu-index>` then lists that IPDU's signals,
  each annotated with its own SID (`Speed_RX.VehicleSpeedRaw(SID=11)`) — that SID, not the IPDU
  index, is what `wrsg <sid> <value>` takes as its first argument.
- **The value is raw, not physical.** `Com_SendSignal`/`Com_ReceiveSignal` in this codebase do not
  apply the DBC's factor/offset (that's generator/documentation metadata only, see 1.2) — `wrsg`
  writes the bit pattern directly. `wrsg <sid> <value>` parses `<value>` as decimal unless prefixed
  `0x`. For `CmdFanLevel` (factor 1, offset 0) raw happens to equal physical, so `wrsg 5 6` means
  "set to 6". For `CmdTargetTempRaw` (factor 0.5, offset 16) it does not — to get 24.0 °C you must
  write the raw register value `(24.0 - 16) / 0.5 = 16`, not `24`.
- **A `wrsg` on a Tx signal only sticks if nothing else re-asserts it.** Before the 1.7 fix landed,
  `wrsg`ing an `AirconCommand` signal would appear to work for a moment and then silently revert
  within 10 ms, because `Fmw_ApplyAirconCommand()` was still overwriting it every tick. With the
  bounded re-assert window in place, a `wrsg` issued more than ~1 s after boot survives indefinitely
  (Com's cyclic-Tx timer just keeps re-sending the buffer as-is) and shows up in the next
  transmitted frame immediately — `wrsg`ing an Rx signal always sticks either way, since nothing in
  this app writes to Rx buffers except an actual reception.

## Part 2: FmwApp Implementation Spec

### 2.1 Role

`FmwApp` implements [specs/sys/SYS-001-can-signal.md](../specs/sys/SYS-001-can-signal.md) (draft v3,
fictional OEM "FMW") as the vehicle's **Multimedia (MM) unit**: it receives the 11 vehicle-status
messages and transmits `AirconCommand` to `FMW_HVAC`. All 12 messages live on `CAN0`
(`simulator_v2`, physical channel/port 0, 500 kbit/s — see 1.5).

### 2.2 Message table

| CAN ID | Message | Dir | Cycle | Rx timeout | Notes |
|---|---|---|---:|---:|---|
| 0x120 | Speed | Rx | 100 ms | 300 ms | uint16 LE, factor 0.01, 0xFFFF=Invalid, physical value capped at 255.00 km/h |
| 0x121 | TacoMeter | Rx | 50 ms | 150 ms | uint16 LE, factor 0.25, 0xFFFF or raw>32000 = Invalid |
| 0x323 | Aircon | Rx | 100 ms | 300 ms | AC/Defrost/Mode/FanLevel/TargetTempRaw |
| 0x526 | AirconCommand | **Tx** | 100 ms | n/a | this node's command to FMW_HVAC, see 2.4 |
| 0x224 | Reverse | Rx | 50 ms | 150 ms | Stale disables reverse-linked features (fail-safe) |
| 0x225 | ParkingBrake | Rx | 100 ms | 300 ms | Stale defaults launch inhibit to conservative |
| 0x226 | ESC | Rx | 100 ms | 300 ms | Stale forces fail-safe warning ON |
| 0x227 | ABS | Rx | 100 ms | 300 ms | Stale forces fail-safe warning ON |
| 0x228 | Seatbelt | Rx | 100 ms | 300 ms | 4 seats |
| 0x229 | DoorOpen | Rx | 100 ms | 300 ms | 4 doors |
| 0x22A | Airbag | Rx | 100 ms | 300 ms | 4 seats; Stale forces fail-safe warning ON |
| 0x22B | LowFuel | Rx | 500 ms | 1500 ms | 0-100%, 0xFF=Invalid |

Every Rx timeout is exactly `CycleTime * 3`, so `Com.json` sets a single network-level
`"timeout_factor": 3` rather than a per-message override (see 1.3).

> **Spec discrepancy, resolved**: SYS-001 section 6 originally named Reverse as `0x124` and
> ParkingBrake as `0x125`, contradicting section 2's table (`0x224`/`0x225`). Section 2 was treated
> as authoritative and section 6 has since been corrected to match it.

### 2.3 Quality model (SYS-001 section 3), Rx messages only

Each of the 11 Rx messages carries an independent `Fmw_QualityCtxType` (name, quality, confidence,
consecutive-valid counter, its own timeout, elapsed-since-valid, ever-received flag):

- **States**: `UNKNOWN` (never received) → `OK` ↔ `STALE` (Com `RxTimeout` fired) / `INVALID`
  (value failed a range/sentinel check in `RxNotification`).
- **Recovery hysteresis**: leaving `STALE`/`INVALID` back to `OK` requires **2 consecutive** valid
  receptions (SYS-001: "2 consecutive valid samples"), tracked in `consecutiveValid`. A single valid sample
  arriving right after a stale period is *not* enough.
- **Confidence** (simplified 100/60/40/0 staging — SYS-001's own 300/1000, 500/3000, 1500/3000 ms
  class pairs don't share a common ratio with each row's own timeout column, so this app uses a
  single fixed rule instead): `100` while `OK`, `60` immediately on entering `STALE`, downgraded to
  `40` once elapsed-since-valid exceeds `3 * timeoutMs` (aged every 10 ms by `Fmw_Age()`), `0` while
  `INVALID` or `UNKNOWN`.
- **Reserved-bit violations** (SYS-001 3.2) are logged as `ERROR`-level "DTC candidate" lines; there
  is no `Dem`/`Dcm` linked in this app, so nothing is actually recorded as a DTC.

Two bugs were hit and fixed while implementing this (both are the generic gotchas in 1.7/1.8):
`RxTimeout` re-firing periodically was resetting `consecutiveValid` and `confidence` on every re-fire
(not just the first STALE transition), which made recovery from a long stale period effectively
impossible and undid the 60→40 confidence downgrade. `Fmw_OnTimeout()` now returns `TRUE` only on the
actual `OK/UNKNOWN → STALE` edge, and callers gate their one-shot log lines / fail-safe side effects
(e.g. forcing the ABS/ESP/Airbag lamps ON) on that.

### 2.4 AirconCommand (Tx)

`AirconCommand` is **not** received — this node originates it. The desired command is held in a
static struct (currently a fixed demo value: AC on, target 24.0 °C, fan 4, mode=Cool) and pushed into
Com's Tx signal buffer by `Fmw_ApplyAirconCommand()` (`Com_SendSignal` per field); `CanIf`/`Com`'s own
100 ms cyclic-Tx timer does the actual transmission. `Fmw_MainFunction10ms()` calls
`Fmw_ApplyAirconCommand()` for the first 100 ticks (1 s) after boot only — see 1.7 for why a bounded
window, not "once" or "forever", is the correct fix — after which the buffer is left alone and a
shell `wrsg` on one of its signals (see 1.12) persists indefinitely.

### 2.5 File map

| File | Role |
|---|---|
| [app/fmw/SConscript](../src/app/fmw/SConscript) | registers `FmwApp`, `Com`/`PduR`/`CanIf` only |
| [app/fmw/main.c](../src/app/fmw/main.c) | unmodified copy of `app/app/main.c` |
| [app/fmw/include/app.h](../src/app/fmw/include/app.h) | `App_Init`/`App_MainFunction` declarations |
| [app/fmw/src/app.c](../src/app/fmw/src/app.c) | trimmed copy of `app/app/src/app.c`; calls `Fmw_Init()`/`Fmw_MainFunction10ms()` |
| [app/fmw/src/fmw_signals.c](../src/app/fmw/src/fmw_signals.c) | all 12 messages: decode, quality model, `AirconCommand` Tx |
| [app/fmw/config/Com/CAN0.dbc](../src/app/fmw/config/Com/CAN0.dbc) | authoritative message/signal layout, standard DBC |
| [app/fmw/config/Com/Com.json](../src/app/fmw/config/Com/Com.json) | Com network config |
| [app/fmw/config/Com/CanIf.json](../src/app/fmw/config/Com/CanIf.json) | CAN-ID routing (hand-cased, see 1.4) |
| [app/fmw/config/Com/PduR.json](../src/app/fmw/config/Com/PduR.json) | CanIf↔Com PDU routing (hand-cased, see 1.4) |

### 2.6 Verified test procedure

Build and run inside the dev container (`autoas-dev`, see [build.md](build.md)):

```sh
docker exec autoas-dev bash -lc 'cd /workspace && scons --app=FmwApp'
docker exec -d autoas-dev bash -lc \
  'stdbuf -oL -eL /workspace/build/posix/GCC/FmwApp/FmwApp -v 0 > /tmp/fmwapp.log 2>&1'
```

Send an Rx message with the existing `CanSend` CLI tool and check the decode log:

```sh
docker exec autoas-dev /workspace/build/posix/GCC/one/CanSend \
  -d simulator_v2 -p 0 -i 120 -v 8813000000000000   # raw=0x1388=5000 -> 50.00 km/h
docker exec autoas-dev grep 'Speed=' /tmp/fmwapp.log
# FMW     :FMW: Speed=50.00 km/h (raw=5000) quality=...
```

Watch the Tx side with `CanDump`:

```sh
docker exec autoas-dev /workspace/build/posix/GCC/one/CanDump -d simulator_v2 -p 0 -f 7FF#526
# canid=00000526,dlc=08,data=[07,04,10,05,00,00,00,00,] ... every ~100ms
```

`07 04 10 05 00 00 00 00` decodes as `MM_AC_Cmd`/`MM_Temp_Cmd`/`MM_Fan_Cmd`=1, `CmdFanLevel`=4,
`CmdTargetTempRaw`=16 (→ 24.0 °C), `CmdAC_OnOff`=1, `CmdMode`=1 — matching the demo command in 2.4.

Send garbage to exercise `INVALID` (`0xFFFF` on Speed) and a reserved-bit violation (`Aircon`
`AirconReserved0` bits set) the same way; both produce the expected `ERROR`/`FMW` log lines
documented in 2.3.

`FmwApp` was also run at the same time as `CanApp` (both on `CAN0`/port 0, see 1.5) with no ID
collisions and no interference, confirming that per-app `CAN0` networks safely share the simulated
bus as long as their CAN IDs are disjoint.

## Part 3: Testing with webcan

Sending 12 different messages by hand with `CanSend`/`CanDump` (2.6) gets tedious fast. `webcan` is
a small standalone browser tool built alongside `FmwApp` for exactly this: open the same simulated
bus, browse the DBC as a tree, send by signal name instead of raw hex, and keep a set of messages
transmitting on a timer instead of one `CanSend` per frame.

### 3.1 What it is and how it talks to the bus

`webcan` is a separate Python process (FastAPI + a plain HTML/Vue3-via-CDN frontend, **not** an
AUTOSAR app) under [tools/webcan/](../src/tools/webcan/):

| File | Role |
|---|---|
| [tools/webcan/server.py](../src/tools/webcan/server.py) | FastAPI app: bus open/close, send-once, cyclic/periodic send, trace history, WebSocket push |
| [tools/webcan/canlib.py](../src/tools/webcan/canlib.py) | ctypes binding straight to `libAsOne.so`'s `can_open`/`can_read_v2`/`can_write` — the *same* canlib every AS-ONE app links, not a reimplementation |
| [tools/webcan/dbcparser.py](../src/tools/webcan/dbcparser.py) | small dependency-free DBC reader (message/signal metadata + signal-value <-> raw-bytes encode/decode), independent of the `tools/generator/dbc` parser used by SCons |
| [tools/webcan/dbc/CAN0.dbc](../src/tools/webcan/dbc/CAN0.dbc) | a copy of `FmwApp`'s DBC (server loads every `*.dbc` under this folder at startup) |
| [tools/webcan/static/index.html](../src/tools/webcan/static/index.html) | the whole UI: bus controls, DBC tree, send forms, live/log trace |

Because it links `libAsOne.so` directly, `webcan` opening `simulator_v2`/port 0 joins the **same**
multicast bus `FmwApp`'s `CAN0` uses (see 1.5) — it is just another process on the wire, exactly
like `CanSend`/`CanDump`, not a proxy in front of them.

### 3.2 Running it (Docker)

```sh
docker exec autoas-dev bash -lc \
  'cd /workspace/tools/webcan && python3 server.py --host 0.0.0.0 --port 8800'
```

`compose.yaml` publishes `8800:8800`, so open `http://localhost:8800/` on the host. The page has
three parts: bus/send controls on the left, a **DBC Definitions** tree and a **Trace** view on the
right.

### 3.3 DBC tree + send-by-signal

The DBC tree (one entry per message, expandable) mirrors what 1.2/1.4 describe as the DBC's role:
name, id, dlc, sender node, cycle time, and each signal's bit range/endian/factor/offset/unit. Once
a bus is open and frames are flowing, each open message's signal rows also show a live decoded
**Value** column (little/big-endian bit-unpack done client-side in JS, mirroring
`dbcparser.decode_message`) — click a row in the Trace/Live view to jump the tree open on that
message. Clicking **Send…** on a message (or picking one under "Send by Signal") switches to a form
with one input per signal name/unit instead of a raw hex string; the backend encodes it back to
bytes with `dbcparser.encode_message` before writing to the bus.

### 3.4 ECU selection and "start all" cyclic send

The "Send by Signal" panel has an **ECU** dropdown (the DBC's `BU_` node list — `AS`/`FMW_VCU`/
`FMW_HVAC` for this DBC) that filters the message list down to what that node actually sends,
default `FMW_VCU`. That is deliberate: `FmwApp` (node `AS`) already transmits `AirconCommand`
itself (2.4) — `webcan`'s job here is to play the *other* 11 messages' source (`FMW_VCU`, the
vehicle), not to fight `FmwApp` over the one it originates.

**Start All FMW_VCU Messages (cyclic)** starts one periodic sender per `FMW_VCU` message at its
DBC `cycle_time`, each signal defaulted to its DBC `min` (or 0). This also runs automatically:
once when you click **Open**, and again every time the page itself loads/reloads (so refreshing the
tab against an already-open bus still gets `FmwApp`'s 11 Rx messages flowing — before this it only
fired on the `Open` click itself, which does nothing if the bus was already open from a previous
session). Backing endpoints: `POST /api/buses/{id}/periodic/signals` (or `/periodic/frame` for raw
hex) to start, `DELETE /api/periodic/{key}` to stop, `GET /api/periodic` to list what is currently
running — all idempotent, safe to call repeatedly.

> **JS gotcha hit while building this**: the guard was originally `if (!busId) return;`. The very
> first bus opened is always id `0` (canlib allocates ids starting at 0), and in JavaScript `!0` is
> `true` — so this guard silently no-op'd on exactly the common case, discarding the whole message
> list before the loop that sends it ever ran. Same bug, same fix, was also present in a `:disabled`
> template binding (`!sigSendForm.busId`) that permanently greyed out the "Start All" button
> whenever bus 0 was open. Fix both by checking `=== null` (or `=== undefined`), never bare
> falsiness, for any id/index that can legitimately be `0`. This one was hard to spot from the
> Python side: every backend endpoint worked perfectly under direct `curl`, because the bug was
> purely in the browser's own JS never issuing the requests in the first place — confirmed by
> replaying the page load through the Chrome DevTools Protocol (`Runtime.consoleAPICalled` /
> `Network.requestWillBeSent`) and observing zero periodic-signals `POST`s despite the button
> reporting 11 sendable messages.

### 3.5 Trace: Live vs Log, and why updates are throttled

**Live** (default) keeps one row per `bus:canid`, sorted, updated in place — count, Δt since the
previous frame with that id, and the latest data, without the table growing or reordering. **Log**
is the traditional append-only scrolling history. Both read from the same WebSocket stream.

With several cyclic senders running (11 `FMW_VCU` messages plus `FmwApp`'s own `AirconCommand`,
some at 50 ms) frames can arrive well over 100/s. Committing every single one straight into Vue's
reactive state was measurably heavy on the tab. Incoming frames are instead buffered into plain
(non-reactive) JS objects and only flushed into the reactive `Live`/`Log` state on a timer
(`updateIntervalMs`, a dropdown in the toolbar: 100 ms/300 ms/500 ms/1 s/2 s, default 300 ms) — the
per-id `count`/`Δt` bookkeeping still accounts for every frame that arrived, only the on-screen
redraw rate is reduced.
