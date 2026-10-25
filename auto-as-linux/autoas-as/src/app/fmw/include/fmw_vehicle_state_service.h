/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2026 Parai Wang <parai@foxmail.com>
 *
 * Entry points fmw_signals.c calls into the VehicleStateService SOME/IP
 * glue (fmw_vehicle_state_service.c). TcpIp/SoAd/Sd/SomeIp themselves are
 * initialized and serviced generically by EcuM (USE_TCPIP/USE_SOAD/USE_SD/
 * USE_SOMEIP in EcuM_Cfg.c's DriverInitListTwo / DriverMainList) -- nothing
 * else in this app needs to call their Init/MainFunction directly.
 */
#ifndef FMW_VEHICLE_STATE_SERVICE_H
#define FMW_VEHICLE_STATE_SERVICE_H
/* ================================ [ INCLUDES  ] ============================================== */
#include "Std_Types.h"
/* ================================ [ DECLARES  ] ============================================== */
#ifdef __cplusplus
extern "C" {
#endif

/** @brief Offers VehicleStateService (0x1770.0x0001). Call once after the
 * native SomeIp stack is up (i.e. from Fmw_Init()). */
void FmwVehicleStateService_Init(void);

/** @brief Call every 10ms; sends EvVehicleState every 10th call (100ms,
 * SYS-002 4.2). */
void FmwVehicleStateService_MainFunction10ms(void);

#ifdef __cplusplus
}
#endif
#endif /* FMW_VEHICLE_STATE_SERVICE_H */
