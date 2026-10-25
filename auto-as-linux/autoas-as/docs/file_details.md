# infras File Details

This document provides an English summary of the `infras` layer and its relationship to AUTOSAR Classic Platform R4.4.

## AUTOSAR-CP Module Definition Index

The table below lists major modules referenced in this repository, their official names, and primary responsibilities.

| Abbreviation | Official Name | Primary Function |
|---|---|---|
| Can | CAN Driver | Low-level CAN hardware control, ISR handling, and frame Tx/Rx interface. |
| CanIf | CAN Interface | Unified CAN interface for upper layers (such as Com and CanTp), including routing and filtering. |
| CanTp | CAN Transport Protocol | Segmentation/reassembly of variable-length payloads over CAN with N-PCI, flow control, and timeout handling. |
| CanNm | CAN Network Management | CAN network wake/sleep management and network supervision. |
| CanSM | CAN State Manager | CAN communication state control and lifecycle coordination for CAN-related modules. |
| CanTrcv | CAN Transceiver Driver | CAN transceiver mode management (normal/listen/sleep). |
| CanTSyn | CAN Time Synchronization | Time synchronization over CAN, including master/slave correction. |
| Com | Communication | Signal packing/unpacking and PDU-level communication processing. |
| ComM | Communication Manager | Communication mode coordination across CAN/LIN/Ethernet. |
| Dcm | Diagnostic Communication Manager | UDS service dispatcher, session/security handling, and diagnostic flow control. |
| Dem | Diagnostic Event Manager | DTC/event recording, state transitions, filtering, and reporting. |
| Det | Development Error Tracer | Development-time error reporting and lightweight diagnostics. |
| DoIP | Diagnostic over IP | UDS transport and routing over TCP/IP. |
| LinIf | LIN Interface | Unified LIN interface between hardware and upper layers. |
| LinTp | LIN Transport Protocol | Segmentation/reassembly for LIN transport messages. |
| NvM | Non-Volatile Memory Manager | Abstraction and persistence management over Ea/Fee. |
| PduR | PDU Router | Routing across multiple communication stacks and channels. |
| SoAd | Socket Adapter | Socket abstraction layer for upper modules over TcpIp. |
| TcpIp | TCP/IP Stack | IPv4/IPv6, UDP/TCP, DHCP/ARP and related networking services. |
| Dio | Digital Input/Output Driver | GPIO abstraction for channel/port-level digital operations. |
| Ea | EEPROM Abstraction | Logical block-oriented EEPROM access abstraction. |
| Eep | EEPROM Driver | Low-level EEPROM read/write access and job handling. |
| EcuM | ECU State Manager | ECU startup/shutdown sequencing and wake/sleep transitions. |
| Fee | Flash EEPROM Emulation | EEPROM emulation over Flash with wear-leveling and consistency handling. |
| Fls | Flash Driver | Sector erase/page write/read operations for Flash devices. |
| Port | Port Driver | Pin initialization and multiplexing configuration. |
| ARA | AUTOSAR Runtime for Adaptive Applications | Adaptive-style API compatibility helpers used by this project. |
| lwIP | Lightweight IP | Lightweight embedded TCP/IP stack used as networking foundation. |
| OSAL | Operating System Abstraction Layer | OS abstraction for portability across POSIX/RTOS/Windows-style APIs. |
| RoD | Record of Data | Project-specific lightweight persistent data/log module. |
| X.509 | Public Key Certificate Profile | Certificate and signature-related structures used by TLS/SecOC paths. |

## Overall infras Index

| Folder | Role | R4.4 Mapping |
|---|---|---|
| `infras` | Base infrastructure layer integrating communication, diagnostics, memory, system, and support modules. | Primarily AUTOSAR CP BSW-aligned, with project-specific extensions. |
| `infras/communication` | Communication stacks and routing paths for CAN/LIN/Ethernet and diagnostics. | Mainly Communication Services and Communication Stack scope. |
| `infras/crypto` | Crypto service wrapper layer. | Roughly aligned with CSM concepts; crypto backend is external-library based. |
| `infras/diagnostic` | Diagnostic services including UDS/event/error trace handling. | Corresponds to DCM/DEM/DET areas. |
| `infras/include` | Public headers for BSW/MCAL/common interfaces. | Mix of AUTOSAR-like interfaces and project-specific headers. |
| `infras/libraries` | Reusable libraries and dependency integration. | Largely outside strict AUTOSAR scope, supporting BSW implementation. |
| `infras/mcal` | MCAL-like drivers (CAN/LIN/Flash/Port/DIO). | Maps to MCAL concepts with platform-specific implementations. |
| `infras/memory` | Memory stack modules and persistence helpers. | Directly related to Memory Stack layers; includes project extensions. |
| `infras/system` | System runtime layer (ECU control, OS glue, FS/network/shell/trace). | Combination of EcuM/OS-style responsibilities and middleware extensions. |

## Folder-by-Folder Notes

### `infras`

- Role: Root infrastructure aggregation layer.
- R4.4 Mapping: BSW-equivalent base with non-standard additions.

### `infras/communication`

- Role: Communication stack implementations and adapters.
- R4.4 Mapping: Communication Services / Communication Stack heavy.

### `infras/crypto`

- Role: Crypto service integration layer.
- R4.4 Mapping: CSM-adjacent behavior with external cryptographic backends.

### `infras/diagnostic`

- Role: UDS/event/error-trace diagnostic modules.
- R4.4 Mapping: DCM/DEM/DET-aligned responsibilities.

### `infras/include`

- Role: Public API and shared type headers.
- R4.4 Mapping: Mixed AUTOSAR-like and project-specific definitions.

### `infras/libraries`

- Role: Shared libraries and third-party integration helpers.
- R4.4 Mapping: Mostly support components outside strict AUTOSAR module boundaries.

### `infras/mcal`

- Role: MCAL-style hardware abstraction drivers.
- R4.4 Mapping: MCAL conceptually aligned, platform-specific in implementation details.

### `infras/memory`

- Role: Non-volatile memory service stack.
- R4.4 Mapping: Memory Stack aligned, with additional project-level persistence helpers.

### `infras/system`

- Role: Core system runtime infrastructure.
- R4.4 Mapping: Combination of EcuM/OS-like concerns and extended middleware functions.

