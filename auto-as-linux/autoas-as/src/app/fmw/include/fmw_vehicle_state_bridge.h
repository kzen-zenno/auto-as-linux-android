/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2026 Parai Wang <parai@foxmail.com>
 *
 * Bridge between fmw_signals.c (CAN decode + SYS-001 quality/confidence
 * model) and the VehicleStateService SOME/IP skeleton (SYS-002 /
 * SWARCH-002). Implemented inside fmw_signals.c itself so it can reach the
 * file-scope signal/quality state directly; this header is the only public
 * surface the SOME/IP glue code (fmw_vehicle_state_service.c) is allowed to
 * use.
 */
#ifndef FMW_VEHICLE_STATE_BRIDGE_H
#define FMW_VEHICLE_STATE_BRIDGE_H
/* ================================ [ INCLUDES  ] ============================================== */
#include "Std_Types.h"
#include "VehicleStateSkeleton.h"
/* ================================ [ MACROS    ] ============================================== */
/* SYS-002 4.1 key IDs. */
#define FMW_VS_KEY_SPEED 0x1001u
#define FMW_VS_KEY_TACHOMETER 0x1002u
#define FMW_VS_KEY_REVERSE 0x1004u
#define FMW_VS_KEY_PARKING_BRAKE 0x1005u
#define FMW_VS_KEY_AIRCON_ON_OFF 0x1006u
#define FMW_VS_KEY_AIRCON_TEMPERATURE 0x1007u
#define FMW_VS_KEY_AIRCON_FAN 0x1008u
#define FMW_VS_KEY_AIRCON_DEFROST 0x1009u
#define FMW_VS_KEY_AIRCON_MODE 0x100Au
#define FMW_VS_KEY_ESC_IS_ERROR 0x100Cu
#define FMW_VS_KEY_ABS_IS_ERROR 0x100Du
#define FMW_VS_KEY_SEAT_IS_BELTED 0x100Eu
#define FMW_VS_KEY_DOOR_IS_OPEN 0x100Fu
#define FMW_VS_KEY_AIRBAG_IS_DEPLOYED 0x1010u
#define FMW_VS_KEY_FUEL_RELATIVE_LEVEL 0x1011u

/* SYS-002 5.2 quality values (fixed-length schema keeps the same encoding,
 * see autoas-as/src/app/fmw/README.md 3.3). FMW_Q_UNKNOWN is folded into
 * INVALID at this boundary (README.md 7.2, user-confirmed). */
#define FMW_VS_QUALITY_OK 0u
#define FMW_VS_QUALITY_INVALID 1u
#define FMW_VS_QUALITY_STALE 2u

/* Number of SYS-002 4.1 keys this Gateway ECU publishes. */
#define FMW_VS_NUM_KEYS 15u

/* WriteVehicleState result_code (SYS-002 5.2). */
#define FMW_VS_WRITE_OK 0x00u
#define FMW_VS_WRITE_INVALID 0x01u
#define FMW_VS_WRITE_UNSUPPORTED 0x02u
#define FMW_VS_WRITE_NO_FAILED_INDEX 0xFFu
/* ================================ [ TYPES     ] ============================================== */
/* ================================ [ DECLARES  ] ============================================== */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fills @p entries (capacity >= FMW_VS_NUM_KEYS) with all SYS-002
 * 4.1 keys read from the current fmw_signals.c CAN state, freshly stamped.
 * Used for both the periodic EvVehicleState notify and
 * GetVehicleStateSnapshot.
 * @return number of entries written (always FMW_VS_NUM_KEYS)
 */
uint16_t FmwVehicleState_GetSnapshot(LkvEntry_Type *entries);

/**
 * @brief Current wall-clock time in epoch milliseconds (0 on a platform
 * without gettimeofday). Shared by the snapshot/event path and
 * fmw_write_policy's applied_state echo so both use one clock source.
 */
uint64_t FmwVehicleState_NowMs(void);

/**
 * @brief Applies one already-validated HVAC entry (key in
 * FMW_VS_KEY_AIRCON_ON_OFF..FMW_VS_KEY_AIRCON_MODE) and immediately pushes
 * the resulting AirconCommand onto CAN (0x526) -- does not wait for the
 * boot-time re-assert window fmw_signals.c otherwise uses (see
 * docs/fmwapp.md 1.7). Caller (fmw_write_policy) guarantees the key/value
 * were already range-checked against SYS-002 4.1.
 */
void FmwVehicleState_ApplyHvacWrite(uint16_t keyId, float value);

#ifdef __cplusplus
}
#endif
#endif /* FMW_VEHICLE_STATE_BRIDGE_H */
