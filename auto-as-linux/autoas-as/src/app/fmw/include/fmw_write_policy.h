/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2026 Parai Wang <parai@foxmail.com>
 *
 * WriteVehicleState validation (SYS-002 4.1 / 5.2, SWARCH-002 4). All the
 * writable-key range/type/value checks live here so
 * fmw_vehicle_state_service.c stays a thin SOME/IP glue layer.
 */
#ifndef FMW_WRITE_POLICY_H
#define FMW_WRITE_POLICY_H
/* ================================ [ INCLUDES  ] ============================================== */
#include "Std_Types.h"
#include "VehicleStateSkeleton.h"
/* ================================ [ DECLARES  ] ============================================== */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validates @p request against SYS-002 4.1 (key range
 * 0x1006..0x100A, per-key value range) all-or-nothing: the first invalid or
 * unsupported entry aborts validation and leaves the Gateway ECU state
 * untouched (no partial apply, no CAN Tx). On success, every entry is
 * applied via FmwVehicleState_ApplyHvacWrite() and echoed back in
 * response->entries with a fresh timestamp; the model itself lives only in
 * fmw_signals.c / CAN, see README.md 7.1.
 */
void FmwWritePolicy_Apply(const WriteRequest_Type *request, WriteResponse_Type *response);

#ifdef __cplusplus
}
#endif
#endif /* FMW_WRITE_POLICY_H */
