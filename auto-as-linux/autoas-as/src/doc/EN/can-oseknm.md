---
layout: post
title: Introduction to CAN OSEK NM
category: AUTOSAR
comments: true
---

# Introduction to CAN OSEK NM

CAN NM (Network Management) is the network-management mechanism for non-Ethernet CAN networks. Compared with Ethernet network management, CAN NM is much simpler. At its core, it solves one thing: how all CAN nodes on a network wake up together and go to sleep together.

CAN NM also has several variants, such as [AUTOSAR CAN NM](../../infras/include/CanNm.h) and [OSEK NM](../../infras/include/OsekNm.h). In addition, many OEMs have their own custom network-management mechanisms. Despite the differences, all CAN NM variants pursue the same goal: synchronized wakeup and sleep behavior across the network.

OSEK NM itself can be divided into two mechanisms: direct network management and indirect network management.

The distinction is simple:

* If dedicated CAN messages are used to transfer network-state information, it is direct network management.
* If application messages are used to indirectly synchronize network-state information, it is indirect network management.

By that definition, AUTOSAR CAN NM belongs to direct network management.

This article briefly introduces direct OSEK NM. Indirect OSEK NM is used less often in practice, and the application messages involved vary from vehicle to vehicle and OEM to OEM, which makes standardization difficult.

For the underlying theory, refer to the OSEK NM 253 specification and the many introductions available online. This article focuses on how to use my OSEK NM implementation.

## 1. Configuration

The OSEK NM configuration definitions are located in [OsekNm_Priv.h](../../infras/communication/OsekNm/OsekNm_Priv.h). For the configuration of a CAN channel, refer to [OsekNm_Cfg.c](../../app/app/config/OsekNm_Cfg.c).

The configuration is fairly direct and mainly consists of node-related OSEK NM parameters:

| NM Parameter  | Definition                                                   | Valid Area                   |
| :------------ | ------------------------------------------------------------ | ---------------------------- |
| NodeId        | Relative identification of the node-specific NM messages     | local for each node specific |
| TTyp          | Typical time interval between two ring messages              | global for all nodes         |
| TMax          | Maximum time interval between two ring messages              | global for all nodes         |
| TError        | Time interval between two ring messages with NMLimpHome identification | global all nodes             |
| TWaitBusSleep | Time the NM waits before transmission in NMBusSleep          | global all nodes             |
| TTx           | Delay to repeat the transmission request of a NM message if the request was rejected by the DLL | local for each node specific |

## 2. Integration and Usage

This OSEK NM implementation can be integrated into any system and can also be used standalone. To integrate it, the following APIs must be provided:

```c
void OsekNm_D_Init(NetworkHandleType NetId, OsekNm_RoutineRefType Routine);
// disable application communication by OsekNm_D_Offline
void OsekNm_D_Offline(NetworkHandleType NetId);
// enable application communication by OsekNm_D_Online
void OsekNm_D_Online(NetworkHandleType NetId);
// request to transmit NM frame
Std_ReturnType OsekNm_D_WindowDataReq(NetworkHandleType NetId, OsekNm_PduType *NMPDU, uint8_t DataLengthTx);
```

In `ssas-public`, you may notice that the first three APIs are currently empty implementations. Strictly speaking, that is not correct; they should be implemented according to the OSEK NM specification.

Below is a simple implementation of `OsekNm_D_WindowDataReq` using the AUTOSAR CAN driver. It assumes that CAN IDs from `0x500` to `0x5FF` are reserved for the NM segment.

```c
Std_ReturnType OsekNm_D_WindowDataReq(NetworkHandleType NetId, OsekNm_PduType *NMPDU, uint8_t DataLengthTx) {
  Std_ReturnType ercd;
  Can_PduType canPdu;

  canPdu.swPduHandle = 2;
  canPdu.id = 0x500 + NMPDU->Source;
  canPdu.length = DataLengthTx;
  canPdu.sdu = &NMPDU->Destination;

  ercd = Can_Write(NetId, &canPdu);
  // When NM frame transmission succeeds, OsekNm_TxConformation
  // must be called from the transmit-complete interrupt.

  return ercd;
}
```

Reception handling of NM frames from other nodes can be done as follows:

```c
void CanIf_RxIndication(const Can_HwType *Mailbox, const PduInfoType *PduInfoPtr) {
  ...
  if ((Mailbox->CanId >= 0x500) && ((Mailbox->CanId <= 0x5FF))) {
    OsekNm_PduType NMPDU;
    NMPDU.Source = Mailbox->CanId - 0x500;
    memcpy(&NMPDU.Destination, PduInfoPtr->SduDataPtr, 8);
    OsekNm_RxIndication(Mailbox->ControllerId, &NMPDU);
  }
  ...
}
```

Once all of the above is in place, OSEK NM can start working after initialization:

```c
  OsekNm_Init(NULL);
  OsekNm_Talk(0);
  OsekNm_Start(0);
```

In addition, `OsekNm_MainFunction` must be called periodically to drive the state machine.

The `GotoMode` API can be used to control the network state:

```c
OsekNm_GotoMode(0, OSEKNM_BUS_SLEEP); // request sleep and release the network
OsekNm_GotoMode(0, OSEKNM_AWAKE);    // request wakeup and activate the network
```

There are also two APIs to control whether the node actively participates in network management. These can be used to implement the UDS communication-control service for network management:

```c
Std_ReturnType OsekNm_Silent(NetworkHandleType);
Std_ReturnType OsekNm_Talk(NetworkHandleType);
```

One more point deserves special emphasis: OSEK NM includes bus-off handling. When a CAN bus-off occurs, call `OsekNm_BusErrorIndication`. When the CAN bus transitions from sleep to wakeup, call `OsekNm_WakeupIndication`.

## 3. Example

Refer to [OsekNm](../../examples/OsekNm.md).