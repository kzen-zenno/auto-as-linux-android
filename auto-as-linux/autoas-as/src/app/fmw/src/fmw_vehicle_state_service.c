/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2026 Parai Wang <parai@foxmail.com>
 *
 * VehicleStateService SOME/IP server (SYS-002 / SWARCH-002), Gateway ECU
 * side. Thin glue between the generated native-SomeIp plain-C skeleton
 * (VehicleStateSkeleton.h) and fmw_vehicle_state_bridge.h /
 * fmw_write_policy.h; see autoas-as/src/app/fmw/README.md section 4.
 *
 * TcpIp/SoAd/Sd/SomeIp themselves are initialized and serviced generically
 * by EcuM (USE_TCPIP/USE_SOAD/USE_SD/USE_SOMEIP), matching how Com/CanIf/
 * PduR already work for the CAN side -- this file only offers the service
 * once and answers requests/sends the event synchronously.
 */
/* ================================ [ INCLUDES  ] ============================================== */
#include "Std_Types.h"
#include "Std_Debug.h"
#include "Sd.h"
#include "VehicleStateSkeleton.h"
#include "fmw_vehicle_state_bridge.h"
#include "fmw_write_policy.h"
#include "fmw_vehicle_state_service.h"
/* ================================ [ MACROS    ] ============================================== */
#define AS_LOG_FMWVS 1
/* SYS-002 4.2: EvVehicleState cycle is 100 ms; this module is ticked every
 * 10 ms (Fmw_MainFunction10ms). */
#define FMW_VS_EVENT_CYCLE_TICKS 10u
/* ================================ [ DATAS     ] ============================================== */
static uint16_t s_tickCount = 0;
/* ================================ [ FUNCTIONS ] ============================================== */
boolean Sd_ServerServiceVehicleState_CRMC(PduIdType pduID, uint8_t type, uint16_t serviceID,
                                           uint16_t instanceID, uint8_t majorVersion,
                                           uint32_t minorVersion,
                                           const Sd_ConfigOptionStringType *receivedConfigOptionPtrArray,
                                           const Sd_ConfigOptionStringType *configuredConfigOptionPtrArray) {
  (void)pduID;
  (void)type;
  (void)serviceID;
  (void)instanceID;
  (void)majorVersion;
  (void)minorVersion;
  (void)receivedConfigOptionPtrArray;
  (void)configuredConfigOptionPtrArray;
  return TRUE;
}

void SomeIp_VehicleState_OnConnect(uint16_t conId, boolean isConnected) {
  ASLOG(FMWVS, ("VehicleState [%u] %sconnected\n", conId, isConnected ? "" : "dis"));
}

void VehicleState_OnEvVehicleStateSubscribed(boolean isSubscribe, TcpIp_SockAddrType *RemoteAddr) {
  (void)RemoteAddr;
  ASLOG(FMWVS, ("EventGroupVehicleState %s\n", isSubscribe ? "subscribed" : "unsubscribed"));
}

Std_ReturnType VehicleState_GetVehicleStateSnapshot(VehicleStateMessage_Type *returnVehicleStateMessage) {
  returnVehicleStateMessage->schemaVersion = 1u;
  returnVehicleStateMessage->entriesLen =
    (uint8_t)FmwVehicleState_GetSnapshot(returnVehicleStateMessage->entries);
  return E_OK;
}

Std_ReturnType
VehicleState_GetVehicleStateSnapshot_Async(VehicleStateMessage_Type *returnVehicleStateMessage) {
  (void)returnVehicleStateMessage;
  /* GetVehicleStateSnapshot always answers synchronously (E_OK above); the
   * stack only polls this when the sync call returned SOMEIP_E_PENDING. */
  return E_NOT_OK;
}

Std_ReturnType VehicleState_WriteVehicleState(const WriteRequest_Type *request,
                                               WriteResponse_Type *returnWriteResponse) {
  FmwWritePolicy_Apply(request, returnWriteResponse);
  ASLOG(FMWVS, ("WriteVehicleState resultCode=%u failedEntryIndex=%u\n",
                returnWriteResponse->resultCode, returnWriteResponse->failedEntryIndex));
  return E_OK;
}

Std_ReturnType VehicleState_WriteVehicleState_Async(WriteResponse_Type *returnWriteResponse) {
  (void)returnWriteResponse;
  /* WriteVehicleState always answers synchronously (E_OK above). */
  return E_NOT_OK;
}

void FmwVehicleStateService_Init(void) {
  (void)VehicleState_OfferService();
}

void FmwVehicleStateService_MainFunction10ms(void) {
  VehicleStateMessage_Type msg;

  s_tickCount++;
  if (s_tickCount >= FMW_VS_EVENT_CYCLE_TICKS) {
    s_tickCount = 0;
    msg.schemaVersion = 1u;
    msg.entriesLen = (uint8_t)FmwVehicleState_GetSnapshot(msg.entries);
    (void)VehicleState_SendEvVehicleState(&msg);
  }
}
