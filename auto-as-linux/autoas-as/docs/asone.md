# asone Manual

`asone` is a Qt-based GUI tool that integrates all diagnostic and communication panel plugins into one window. Each panel is a shared library (`libUI*.so` / `UI*.dll`) that `asone` discovers at startup and presents as a tab.

## Architecture

```
asone (main.cpp)
  └─ Window::loadUIs()  ... scans ./*.so|.dll for create() + name() exports
       ├─ libUICan.so   → UICan   tab
       ├─ libUICom.so   → UICom   tab
       ├─ libUIDcm.so   → UIDcm   tab
       ├─ libUIFBL.so   → FBL     tab
       ├─ libUIXcp.so   → XCP     tab
       ├─ libUITrace.so → Trace   tab
       └─ libUIVIC.so   → VIC     tab (optional)
```

Key source files:

| File | Role |
|------|------|
| [tools/asone/src/main.cpp](../tools/asone/src/main.cpp) | Application entry, plugin loader, tab manager |
| [tools/asone/src/window.hpp](../tools/asone/src/window.hpp) | Window and Action declarations |
| [tools/asone/src/ui/UICan.cpp](../tools/asone/src/ui/UICan.cpp) | Raw CAN send/receive panel |
| [tools/asone/src/ui/UICom.cpp](../tools/asone/src/ui/UICom.cpp) | COM signal viewer / sender |
| [tools/asone/src/ui/UIDcm.cpp](../tools/asone/src/ui/UIDcm.cpp) | UDS diagnostic panel |
| [tools/asone/src/ui/UIFBL.cpp](../tools/asone/src/ui/UIFBL.cpp) | Flash bootloader panel |
| [tools/asone/src/ui/UIXcp.cpp](../tools/asone/src/ui/UIXcp.cpp) | XCP calibration/programming panel |
| [tools/asone/src/ui/UITrace.cpp](../tools/asone/src/ui/UITrace.cpp) | Trace/log viewer |

## Building

See the comment at the top of [tools/asone/SConscript](../tools/asone/SConscript). `asone` is a Windows-primary Qt application; only the AsOne shared library is practical to build on Linux.

```sh
# Build the core shared library (Linux)
cd /home/<user>/work/auto-as/as
scons --lib=AsOne

# Resulting .so is placed here:
build/posix/GCC/prebuilt/libAsOne.so
build/posix/GCC/one/libAsOne.so
```

The full GUI (`asone` binary + UI plugins) currently requires Qt and a complete Windows or Qt-capable Linux toolchain.

## Running

Place `asone` and all UI `.so` / `.dll` files in the same directory, then run:

```sh
cd build/posix/GCC/one
./asone
```

To open specific tabs only:

```sh
./asone -u UICan -u UIDcm
```

Command-line options:

| Option | Description |
|--------|-------------|
| `-u <name>` | Open the named tab on startup (repeatable) |
| `-v` | Enable debug log level |
| `-s` | Set Qt scale factor to 0.75 for high-DPI screens |
| `-d` | Disable named log output |

## UICan — Raw CAN Panel

Source: [tools/asone/src/ui/UICan.cpp](../tools/asone/src/ui/UICan.cpp)

Provides direct CAN bus access without any protocol stack.

**Controls:**

| Control | Description |
|---------|-------------|
| CAN0–CAN3 Open | Opens `canlib` with the chosen device, port, and baudrate |
| BUS ID | Selects which open bus to send on |
| CAN ID | Frame ID in hex |
| DATA | Payload bytes as a hex string, e.g. `1122334455667788` |
| PERIOD | Repeat interval in ms; 0 means send once |
| Send | Transmit immediately; if PERIOD > 0, also starts periodic sending |
| CAN trace log | Live hex trace of all sent/received frames |
| stdout | Shows `CanApp` shell output; the input line sends shell commands |

**How it differs from IsoTpSend:** `UICan` sends raw CAN frames directly via `can_write()`. No transport-layer segmentation. Use `UIDcm` for UDS over ISO-TP.

## UICom — COM Signal Panel

Source: [tools/asone/src/ui/UICom.cpp](../tools/asone/src/ui/UICom.cpp)

Loads a COM network description (JSON or DBC) and shows each message as a set of signal fields. Supports Lua scripts per message for encoding/decoding.

**Typical workflow:**

1. Load a network JSON file.
2. Each message appears as a tab with editable signal fields (Tx) or read-only display (Rx).
3. Set a period and click **update** to send periodically.
4. Attach a `.lua` script to a message for custom logic.

## UIDcm — UDS Diagnostic Panel

Source: [tools/asone/src/ui/UIDcm.cpp](../tools/asone/src/ui/UIDcm.cpp)

Sends UDS services over ISO-TP (CAN or LIN). Driven primarily by a `diagnostic.json` config and Lua scripts.

**Config: `diagnostic.json` example**

```json
{
  "protocol": "CAN",
  "device": "simulator_v2",
  "port": 0,
  "baudrate": 500000,
  "rxid": "0x732",
  "txid": "0x731"
}
```

**Lua scripting:**

The panel provides a Lua console. A Lua script can call back into the panel API. An example is [tools/asone/examples/dcm.lua](../tools/asone/examples/dcm.lua):

```lua
-- Decode snapshot records from DTC data
function decode_snapshot(d)
  ...
end
```

Security access seed/key calculation is also done in Lua. See [tools/asone/examples/Tester.lua](../tools/asone/examples/Tester.lua):

```lua
function Security01Key()
  local seed = (lSeed_01[1]<<24) + (lSeed_01[2]<<16) + (lSeed_01[3]<<8) + lSeed_01[4]
  local key = seed ~ 0x78934673
  local req = { 0x27, 0x02, (key>>24)&0xFF, (key>>16)&0xFF, (key>>8)&0xFF, key&0xFF }
  return 0, req
end
```

## UIFBL — Flash Bootloader Panel

Source: [tools/asone/src/ui/UIFBL.cpp](../tools/asone/src/ui/UIFBL.cpp)

Automates the UDS bootloader sequence: extended session → security access → request download (flash driver + application) → ECU reset.

**Inputs:**

| Field | Description |
|-------|-------------|
| protocol | `CAN` or `LIN` |
| device | `simulator_v2`, `peak`, `vxl`, etc. |
| port | Bus port number |
| App image | `.s19` / `.srec` file for the application |
| Flash driver image | `.s19` / `.srec` file for the flash driver |

Progress is shown as a percentage bar. Errors are displayed inline.

Internally wraps the same `Loader` library used by the CLI `Loader` tool.

## UIXcp — XCP Panel

Source: [tools/asone/src/ui/UIXcp.cpp](../tools/asone/src/ui/UIXcp.cpp)

XCP calibration and programming over CAN. Lua scripting is supported via `xcp.lua`. Configuration is provided by `xcp.json`.

NRC codes are decoded and displayed in human-readable form.

## UITrace — Trace Log Panel

Source: [tools/asone/src/ui/UITrace.cpp](../tools/asone/src/ui/UITrace.cpp)

A filterable table view of all log / trace events. Supports filter expressions:

```
name=CAN0_TxMsgTime
network=CAN0
dir=rx
id=0x123
```

Multiple filters can be combined with commas.

## Lua Integration

Several panels expose a Lua engine. The `can` module is always available:

```lua
require('can')
local node = can.open(0)      -- open bus 0
node:write(0x731, {0x10, 0x01})
local r, canid, data = node:read(0x732, 1000)
```

Example scripts are in [tools/asone/examples/](../tools/asone/examples/).

## Plugin Development

To add a new panel, create a shared library that exports:

```cpp
extern "C" const char *name();          // returns the tab label
extern "C" QWidget   *create();         // returns a new widget instance
```

Place the `.so` / `.dll` next to the `asone` binary. It will be picked up automatically on the next launch.
