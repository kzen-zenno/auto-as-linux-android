/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2026 Parai Wang <parai@foxmail.com>
 *
 * Decodes and supervises the 12 CAN messages of specs/sys/SYS-001-can-signal.md
 * (draft v3). This node represents the vehicle's Multimedia (MM) unit: it
 * receives the 11 vehicle-status messages and transmits AirconCommand (0x526)
 * to FMW_HVAC. Each of the 11 Rx messages has a Com RxNotification (called on
 * every valid reception) and a Com RxTimeout (called by Com's own supervision
 * timer, armed for CycleTime * timeout_factor(3), matching the spec's "N ms
 * 未受信でStale/Unknown" column exactly). AirconCommand instead has no RX
 * supervision - see Fmw_SendAirconCommand().
 *
 * Quality model (SYS-001 3., applies to the 11 Rx messages): UNKNOWN (never
 * received) / OK / INVALID / STALE.
 * Recovery from STALE or INVALID requires 2 consecutive valid samples (SYS-001
 * 3.: "連続2サンプル有効受信"). confidence is a simplified 100/60/40/0 staging:
 * 100=OK, 60=freshly STALE, 40=STALE for more than 3x its own timeout, 0=
 * INVALID or never received. SYS-001 3. gives inconsistent per-class timeout
 * pairs (300/1000, 500/3000, 1500/3000 ms) that do not share a common ratio
 * with each row's own timeout column, so the 60->40 staging uses a fixed x3
 * factor here instead of trying to reconcile that table.
 *
 * Reserved-bit violations (SYS-001 3.2) are logged as DTC candidates; no Dem
 * integration is done since this app does not link Dem/Dcm.
 */
/* ================================ [ INCLUDES  ] ============================================== */
#include "Std_Types.h"
#include "Std_Debug.h"
#include "Com.h"
#include "Com_Cfg.h"
#include "fmw_vehicle_state_bridge.h"
#include "fmw_vehicle_state_service.h"
#if defined(_WIN32) || defined(linux)
#include <sys/time.h>
#endif
/* ================================ [ MACROS    ] ============================================== */
#define AS_LOG_FMW 0
/* ================================ [ TYPES     ] ============================================== */
typedef enum {
  FMW_Q_UNKNOWN = 0,
  FMW_Q_OK,
  FMW_Q_INVALID,
  FMW_Q_STALE,
} Fmw_QualityType;

typedef struct {
  const char *name;
  Fmw_QualityType quality;
  uint8_t confidence;     /* 0 / 40 / 60 / 100, see file header */
  uint16_t consecutiveValid;
  uint16_t timeoutMs;     /* this message's own Stale threshold, from the spec table */
  uint32_t elapsedMs;     /* time since last valid rx, aged by Fmw_MainFunction10ms */
  boolean everReceived;
} Fmw_QualityCtxType;

/* one seat/door/airbag quad-state, shared shape for Seatbelt/DoorOpen/Airbag */
typedef struct {
  boolean row1Driver;
  boolean row1Passenger;
  boolean row2Driver;
  boolean row2Passenger;
} Fmw_QuadBoolType;
/* ================================ [ DECLARES  ] ============================================== */
static void Fmw_ApplyAirconCommand(void);
/* ================================ [ DATAS     ] ============================================== */
static Fmw_QualityCtxType Speed_Q = {"Speed", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
static Fmw_QualityCtxType TacoMeter_Q = {"TacoMeter", FMW_Q_UNKNOWN, 0, 0, 150, 0, FALSE};
static Fmw_QualityCtxType Aircon_Q = {"Aircon", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
/* AirconCommand (0x526) is transmitted BY this node (Multimedia/MM), not received - no RX
 * quality supervision applies to it, see Fmw_SendAirconCommand() below. */
static Fmw_QualityCtxType Reverse_Q = {"Reverse", FMW_Q_UNKNOWN, 0, 0, 150, 0, FALSE};
static Fmw_QualityCtxType ParkingBrake_Q = {"ParkingBrake", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
static Fmw_QualityCtxType ESC_Q = {"ESC", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
static Fmw_QualityCtxType ABS_Q = {"ABS", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
static Fmw_QualityCtxType Seatbelt_Q = {"Seatbelt", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
static Fmw_QualityCtxType DoorOpen_Q = {"DoorOpen", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
static Fmw_QualityCtxType Airbag_Q = {"Airbag", FMW_Q_UNKNOWN, 0, 0, 300, 0, FALSE};
static Fmw_QualityCtxType LowFuel_Q = {"LowFuel", FMW_Q_UNKNOWN, 0, 0, 1500, 0, FALSE};

static float Speed_Kph = 0.0f;
static float EngineSpeed_Rpm = 0.0f;
static struct {
  boolean acOnOff, defrost;
  uint8_t mode;
  uint8_t fanLevel;
  float targetTempC;
} Aircon;
/* the command this node currently asserts on the bus; Fmw_ApplyAirconCommand() pushes it into
 * Com every 10ms tick (see file header: Com_IpduGroupStart(0,TRUE) at EnterRUN re-initializes
 * the Tx buffer to InitialValue(0), which happens after Fmw_Init() runs, so a one-shot
 * Com_SendSignal() from Fmw_Init() alone would be silently wiped) */
static struct {
  boolean acCmd, tempCmd, fanCmd, defCmd;
  uint8_t cmdFanLevel;
  float cmdTargetTempC;
  boolean cmdAcOnOff, cmdDefrost;
  uint8_t cmdMode;
} AirconCommand = {TRUE, TRUE, TRUE, FALSE, 4, 24.0f, TRUE, FALSE, 1};
static boolean Reverse_Engaged = FALSE;
static boolean ParkingBrake_Applied = FALSE;
static boolean ParkingBrake_AutoHold = FALSE;
static boolean ESC_Error = FALSE;
static boolean ABS_Error = FALSE;
static Fmw_QuadBoolType Seatbelt;
static Fmw_QuadBoolType DoorOpen;
static Fmw_QuadBoolType Airbag;
static uint8_t LowFuel_Percent = 100;
/* ================================ [ LOCALS    ] ============================================== */
static void Fmw_OnValid(Fmw_QualityCtxType *ctx) {
  ctx->everReceived = TRUE;
  ctx->elapsedMs = 0;
  if ((FMW_Q_STALE == ctx->quality) || (FMW_Q_INVALID == ctx->quality)) {
    ctx->consecutiveValid++;
    if (ctx->consecutiveValid >= 2u) {
      /* SYS-001 3.: recovery requires 2 consecutive valid samples */
      ctx->quality = FMW_Q_OK;
      ctx->confidence = 100;
    }
  } else {
    ctx->quality = FMW_Q_OK;
    ctx->confidence = 100;
    ctx->consecutiveValid = 1;
  }
}

static void Fmw_OnInvalid(Fmw_QualityCtxType *ctx, const char *reason) {
  ctx->quality = FMW_Q_INVALID;
  ctx->confidence = 0;
  ctx->consecutiveValid = 0;
  ctx->elapsedMs = 0;
  ASLOG(FMW, ("FMW: %s INVALID (%s)\n", ctx->name, reason));
}

/* Com's RxTOut keeps firing periodically for as long as no frame arrives, not just once at the
 * moment of the transition into Stale - returns TRUE only the first time, so callers can log /
 * apply fail-safe side effects once instead of on every re-fire. */
static boolean Fmw_OnTimeout(Fmw_QualityCtxType *ctx) {
  boolean isNewTransition = (FMW_Q_OK == ctx->quality) || (FMW_Q_UNKNOWN == ctx->quality);
  if (isNewTransition) {
    /* only reset these on the real Stale/Unknown->Stale edge: a Com RxTOut re-fire while
     * already Stale must not wipe out progress a valid rx already made toward the 2-sample
     * recovery threshold, nor undo the elapsed-time confidence downgrade from Fmw_Age(). */
    ASLOG(FMW, ("FMW: %s -> %s (%u ms without reception)\n", ctx->name,
                ctx->everReceived ? "STALE" : "Unknown(no data yet)", ctx->timeoutMs));
    ctx->confidence = 60;
    ctx->consecutiveValid = 0;
  }
  ctx->quality = FMW_Q_STALE;
  return isNewTransition;
}

static void Fmw_Age(Fmw_QualityCtxType *ctx, uint16_t deltaMs) {
  if (ctx->elapsedMs <= (0xFFFFFFFFu - deltaMs)) {
    ctx->elapsedMs += deltaMs;
  }
  if ((FMW_Q_STALE == ctx->quality) && (ctx->confidence > 40u) &&
      (ctx->elapsedMs > (3u * (uint32_t)ctx->timeoutMs))) {
    ctx->confidence = 40;
  }
}
/* ================================ [ FUNCTIONS ] ============================================== */
void Fmw_Init(void) {
  ASLOG(INFO,
        ("FMW: SYS-001 CAN signal app initialized (12 messages, CAN0=simulator_v2 channel 0)\n"));
  ASLOG(FMW, ("FMW: AirconCommand(TX) ACCmd=%d TempCmd=%d FanCmd=%d DefCmd=%d FanLevel=%u "
              "TargetTemp=%.1fdegC AC=%d Defrost=%d Mode=%u\n",
              AirconCommand.acCmd, AirconCommand.tempCmd, AirconCommand.fanCmd,
              AirconCommand.defCmd, AirconCommand.cmdFanLevel, AirconCommand.cmdTargetTempC,
              AirconCommand.cmdAcOnOff, AirconCommand.cmdDefrost, AirconCommand.cmdMode));
  FmwVehicleStateService_Init();
}

void Fmw_MainFunction10ms(void) {
  /* re-assert only for a short window after boot: EnterRUN's Com_IpduGroupStart(0,TRUE) resets
   * the Tx buffer to InitialValue once, at a point that can race with Fmw_Init()'s first push
   * (see the comment on AirconCommand above). 1s comfortably covers that one-time race. Beyond
   * it we stop, so a shell 'wrsg' (or any future dynamic command update) on an AirconCommand
   * signal actually sticks instead of being overwritten again on the next tick. */
  static uint16_t applyTicks = 0;
  if (applyTicks < 100u) {
    Fmw_ApplyAirconCommand();
    applyTicks++;
  }
  Fmw_Age(&Speed_Q, 10);
  Fmw_Age(&TacoMeter_Q, 10);
  Fmw_Age(&Aircon_Q, 10);
  Fmw_Age(&Reverse_Q, 10);
  Fmw_Age(&ParkingBrake_Q, 10);
  Fmw_Age(&ESC_Q, 10);
  Fmw_Age(&ABS_Q, 10);
  Fmw_Age(&Seatbelt_Q, 10);
  Fmw_Age(&DoorOpen_Q, 10);
  Fmw_Age(&Airbag_Q, 10);
  Fmw_Age(&LowFuel_Q, 10);
  FmwVehicleStateService_MainFunction10ms();
}

/* ---- 0x120 Speed ---- */
void CAN0_Speed_RX_RxNotification(void) {
  uint16_t raw = 0;
  (void)Com_ReceiveSignal(COM_SID_VehicleSpeedRaw, &raw);
  if (0xFFFFu == raw) {
    Fmw_OnInvalid(&Speed_Q, "raw=0xFFFF");
  } else {
    float kph = (float)raw * 0.01f;
    if (kph > 255.0f) {
      kph = 255.0f; /* SYS-001 5.: OEM common cap at 255 km/h */
    }
    Speed_Kph = kph;
    Fmw_OnValid(&Speed_Q);
  }
}

void CAN0_Speed_RX_RxTimeout(void) {
  Fmw_OnTimeout(&Speed_Q);
}

/* ---- 0x121 TacoMeter ---- */
void CAN0_TacoMeter_RX_RxNotification(void) {
  uint16_t raw = 0;
  (void)Com_ReceiveSignal(COM_SID_EngineSpeedRaw, &raw);
  if ((0xFFFFu == raw) || (raw > 32000u)) {
    Fmw_OnInvalid(&TacoMeter_Q, "raw out of range");
  } else {
    EngineSpeed_Rpm = (float)raw * 0.25f;
    Fmw_OnValid(&TacoMeter_Q);
  }
}

void CAN0_TacoMeter_RX_RxTimeout(void) {
  Fmw_OnTimeout(&TacoMeter_Q);
}

/* ---- 0x323 Aircon ---- */
void CAN0_Aircon_RX_RxNotification(void) {
  uint8_t acOnOff = 0, defrost = 0, mode = 0, reserved = 0, fanLevel = 0, tempRaw = 0;

  (void)Com_ReceiveSignal(COM_SID_Mode, &mode);
  (void)Com_ReceiveSignal(COM_SID_AirconReserved0, &reserved);
  (void)Com_ReceiveSignal(COM_SID_FanLevel, &fanLevel);
  (void)Com_ReceiveSignal(COM_SID_TargetTempRaw, &tempRaw);

  if (0u != reserved) {
    ASLOG(ERROR,
          ("FMW: Aircon reserved bits=0x%X set - protocol violation, DTC candidate\n", reserved));
  }
  if ((mode > 6u) || (fanLevel > 7u) || (0xFFu == tempRaw) || (tempRaw > 28u)) {
    Fmw_OnInvalid(&Aircon_Q, "mode/fan/temp out of range");
    return;
  }

  (void)Com_ReceiveSignal(COM_SID_AC_OnOff, &acOnOff);
  (void)Com_ReceiveSignal(COM_SID_Defrost, &defrost);

  Aircon.acOnOff = (0u != acOnOff);
  Aircon.defrost = (0u != defrost);
  Aircon.mode = mode;
  Aircon.fanLevel = fanLevel;
  Aircon.targetTempC = 16.0f + ((float)tempRaw * 0.5f);

  Fmw_OnValid(&Aircon_Q);
}

void CAN0_Aircon_RX_RxTimeout(void) {
  Fmw_OnTimeout(&Aircon_Q);
}

/* ---- 0x526 AirconCommand (MM, TX) ----
 * This node (Multimedia/MM) transmits AirconCommand to FMW_HVAC, cycle 100ms (SYS-001 5.: it
 * is not received here). Pushes the AirconCommand struct's current values into Com's Tx
 * buffer; called every 10ms tick (not just once) so it keeps winning over
 * Com_IpduGroupStart(0,TRUE)'s InitialValue reset regardless of exactly when in the EcuM
 * startup sequence that reset happens relative to Fmw_Init(). */
static void Fmw_ApplyAirconCommand(void) {
  uint8_t u8;
  uint8_t tempRaw;
  float targetTempC = AirconCommand.cmdTargetTempC;
  uint8_t fanLevel = AirconCommand.cmdFanLevel;
  uint8_t cmdMode = AirconCommand.cmdMode;

  if (targetTempC < 16.0f) {
    targetTempC = 16.0f;
  } else if (targetTempC > 30.0f) {
    targetTempC = 30.0f;
  }
  tempRaw = (uint8_t)((targetTempC - 16.0f) / 0.5f);
  fanLevel = (fanLevel > 7u) ? 7u : fanLevel;
  cmdMode = (cmdMode > 6u) ? 6u : cmdMode;

  u8 = AirconCommand.acCmd ? 1u : 0u;
  (void)Com_SendSignal(COM_SID_MM_AC_Cmd, &u8);
  u8 = AirconCommand.tempCmd ? 1u : 0u;
  (void)Com_SendSignal(COM_SID_MM_Temp_Cmd, &u8);
  u8 = AirconCommand.fanCmd ? 1u : 0u;
  (void)Com_SendSignal(COM_SID_MM_Fan_Cmd, &u8);
  u8 = AirconCommand.defCmd ? 1u : 0u;
  (void)Com_SendSignal(COM_SID_MM_Def_Cmd, &u8);
  (void)Com_SendSignal(COM_SID_CmdFanLevel, &fanLevel);
  (void)Com_SendSignal(COM_SID_CmdTargetTempRaw, &tempRaw);
  u8 = AirconCommand.cmdAcOnOff ? 1u : 0u;
  (void)Com_SendSignal(COM_SID_CmdAC_OnOff, &u8);
  u8 = AirconCommand.cmdDefrost ? 1u : 0u;
  (void)Com_SendSignal(COM_SID_CmdDefrost, &u8);
  (void)Com_SendSignal(COM_SID_CmdMode, &cmdMode);
}

/* ---- 0x224 Reverse ---- */
void CAN0_Reverse_RX_RxNotification(void) {
  uint8_t state = 0, reserved = 0;
  (void)Com_ReceiveSignal(COM_SID_ReverseState, &state);
  (void)Com_ReceiveSignal(COM_SID_ReverseReserved0, &reserved);
  if (0u != reserved) {
    ASLOG(ERROR,
          ("FMW: Reverse reserved bits=0x%X set - protocol violation, DTC candidate\n", reserved));
  }
  if (state > 1u) {
    Fmw_OnInvalid(&Reverse_Q, "state out of range");
    return;
  }
  Reverse_Engaged = (0u != state);
  Fmw_OnValid(&Reverse_Q);
}

void CAN0_Reverse_RX_RxTimeout(void) {
  if (Fmw_OnTimeout(&Reverse_Q)) {
    ASLOG(FMW, ("FMW: Reverse stale -> Unknown, reverse-linked features disabled (fail-safe)\n"));
  }
}

/* ---- 0x225 ParkingBrake ---- */
void CAN0_ParkingBrake_RX_RxNotification(void) {
  uint8_t state = 0, autoHold = 0, reserved = 0;
  (void)Com_ReceiveSignal(COM_SID_ParkingBrakeState, &state);
  (void)Com_ReceiveSignal(COM_SID_AutoHoldActive, &autoHold);
  (void)Com_ReceiveSignal(COM_SID_ParkingBrakeReserved0, &reserved);
  if (0u != reserved) {
    ASLOG(ERROR, ("FMW: ParkingBrake reserved bits=0x%X set - protocol violation, DTC "
                  "candidate\n",
                  reserved));
  }
  if ((state > 1u) || (autoHold > 1u)) {
    Fmw_OnInvalid(&ParkingBrake_Q, "state out of range");
    return;
  }
  ParkingBrake_Applied = (0u != state);
  ParkingBrake_AutoHold = (0u != autoHold);
  Fmw_OnValid(&ParkingBrake_Q);
}

void CAN0_ParkingBrake_RX_RxTimeout(void) {
  if (Fmw_OnTimeout(&ParkingBrake_Q)) {
    ASLOG(FMW, ("FMW: ParkingBrake stale -> Unknown, launch inhibit defaults to conservative "
                "(Applied-equivalent)\n"));
  }
}

/* ---- 0x226 ESC ---- */
void CAN0_ESC_RX_RxNotification(void) {
  uint8_t err = 0, reserved = 0;
  (void)Com_ReceiveSignal(COM_SID_ESC_IsError, &err);
  (void)Com_ReceiveSignal(COM_SID_ESCReserved0, &reserved);
  if (0u != reserved) {
    ASLOG(ERROR,
          ("FMW: ESC reserved bits=0x%X set - protocol violation, DTC candidate\n", reserved));
  }
  if (err > 1u) {
    Fmw_OnInvalid(&ESC_Q, "value out of range");
    return;
  }
  ESC_Error = (0u != err);
  Fmw_OnValid(&ESC_Q);
}

void CAN0_ESC_RX_RxTimeout(void) {
  if (Fmw_OnTimeout(&ESC_Q)) {
    ASLOG(FMW, ("FMW: ESC stale -> Unknown, fail-safe warning ON\n"));
  }
}

/* ---- 0x227 ABS ---- */
void CAN0_ABS_RX_RxNotification(void) {
  uint8_t err = 0, reserved = 0;
  (void)Com_ReceiveSignal(COM_SID_ABS_IsError, &err);
  (void)Com_ReceiveSignal(COM_SID_ABSReserved0, &reserved);
  if (0u != reserved) {
    ASLOG(ERROR,
          ("FMW: ABS reserved bits=0x%X set - protocol violation, DTC candidate\n", reserved));
  }
  if (err > 1u) {
    Fmw_OnInvalid(&ABS_Q, "value out of range");
    return;
  }
  ABS_Error = (0u != err);
  Fmw_OnValid(&ABS_Q);
}

void CAN0_ABS_RX_RxTimeout(void) {
  if (Fmw_OnTimeout(&ABS_Q)) {
    ASLOG(FMW, ("FMW: ABS stale -> Unknown, fail-safe warning ON\n"));
  }
}

/* ---- 0x228 Seatbelt ---- */
void CAN0_Seatbelt_RX_RxNotification(void) {
  uint8_t b = 0, reserved = 0;
  (void)Com_ReceiveSignal(COM_SID_SeatbeltReserved0, &reserved);
  if (0u != reserved) {
    ASLOG(ERROR, ("FMW: Seatbelt reserved bits=0x%X set - protocol violation, DTC candidate\n",
                  reserved));
  }
  (void)Com_ReceiveSignal(COM_SID_Row1DriverIsBelted, &b);
  Seatbelt.row1Driver = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row1PassengerIsBelted, &b);
  Seatbelt.row1Passenger = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row2DriverIsBelted, &b);
  Seatbelt.row2Driver = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row2PassengerIsBelted, &b);
  Seatbelt.row2Passenger = (0u != b);

  Fmw_OnValid(&Seatbelt_Q);
}

void CAN0_Seatbelt_RX_RxTimeout(void) {
  if (Fmw_OnTimeout(&Seatbelt_Q)) {
    ASLOG(FMW, ("FMW: Seatbelt stale -> per-seat state Unknown\n"));
  }
}

/* ---- 0x229 DoorOpen ---- */
void CAN0_DoorOpen_RX_RxNotification(void) {
  uint8_t b = 0, reserved = 0;
  (void)Com_ReceiveSignal(COM_SID_DoorOpenReserved0, &reserved);
  if (0u != reserved) {
    ASLOG(ERROR, ("FMW: DoorOpen reserved bits=0x%X set - protocol violation, DTC candidate\n",
                  reserved));
  }
  (void)Com_ReceiveSignal(COM_SID_Row1DriverIsOpen, &b);
  DoorOpen.row1Driver = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row1PassengerIsOpen, &b);
  DoorOpen.row1Passenger = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row2DriverIsOpen, &b);
  DoorOpen.row2Driver = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row2PassengerIsOpen, &b);
  DoorOpen.row2Passenger = (0u != b);

  Fmw_OnValid(&DoorOpen_Q);
}

void CAN0_DoorOpen_RX_RxTimeout(void) {
  if (Fmw_OnTimeout(&DoorOpen_Q)) {
    ASLOG(FMW, ("FMW: DoorOpen stale -> per-door state Unknown\n"));
  }
}

/* ---- 0x22A Airbag ---- */
void CAN0_Airbag_RX_RxNotification(void) {
  uint8_t b = 0, reserved = 0;
  (void)Com_ReceiveSignal(COM_SID_AirbagReserved0, &reserved);
  if (0u != reserved) {
    ASLOG(ERROR, ("FMW: Airbag reserved bits=0x%X set - protocol violation, DTC candidate\n",
                  reserved));
  }
  (void)Com_ReceiveSignal(COM_SID_Row1DriverAirbagDeployed, &b);
  Airbag.row1Driver = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row1PassengerAirbagDeployed, &b);
  Airbag.row1Passenger = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row2DriverAirbagDeployed, &b);
  Airbag.row2Driver = (0u != b);
  (void)Com_ReceiveSignal(COM_SID_Row2PassengerAirbagDeployed, &b);
  Airbag.row2Passenger = (0u != b);

  Fmw_OnValid(&Airbag_Q);
}

void CAN0_Airbag_RX_RxTimeout(void) {
  if (Fmw_OnTimeout(&Airbag_Q)) {
    ASLOG(FMW, ("FMW: Airbag stale -> Unknown, fail-safe warning ON\n"));
  }
}

/* ---- 0x22B LowFuel ---- */
void CAN0_LowFuel_RX_RxNotification(void) {
  uint8_t raw = 0;
  (void)Com_ReceiveSignal(COM_SID_FuelRelativeLevel, &raw);
  if ((0xFFu == raw) || (raw > 100u)) {
    Fmw_OnInvalid(&LowFuel_Q, "raw out of range");
    return;
  }
  LowFuel_Percent = raw;
  Fmw_OnValid(&LowFuel_Q);
}

void CAN0_LowFuel_RX_RxTimeout(void) {
  Fmw_OnTimeout(&LowFuel_Q);
}

/* ---- VehicleStateService bridge (SYS-002 / SWARCH-002) ----
 * Implemented here (not a separate .c) so it can read the file-scope signal
 * and quality state above directly; fmw_vehicle_state_bridge.h is the only
 * symbol surface the SOME/IP glue (fmw_vehicle_state_service.c) may use. */
uint64_t FmwVehicleState_NowMs(void) {
#if defined(_WIN32) || defined(linux)
  struct timeval tv;
  (void)gettimeofday(&tv, NULL);
  return ((uint64_t)tv.tv_sec * 1000u) + ((uint64_t)tv.tv_usec / 1000u);
#else
  return 0u;
#endif
}

/* SYS-002 5.2 has no UNKNOWN quality value; FMW_Q_UNKNOWN (never received)
 * is folded into INVALID at this boundary (README.md 7.2, user-confirmed). */
static uint8_t Fmw_VsQuality(Fmw_QualityType q) {
  uint8_t out;
  switch (q) {
  case FMW_Q_OK:
    out = FMW_VS_QUALITY_OK;
    break;
  case FMW_Q_STALE:
    out = FMW_VS_QUALITY_STALE;
    break;
  case FMW_Q_INVALID:
  case FMW_Q_UNKNOWN:
  default:
    out = FMW_VS_QUALITY_INVALID;
    break;
  }
  return out;
}

/* Fmw_QualityCtxType.confidence is staged 0/40/60/100 (see file header);
 * SYS-002 5.2 confidence_level is 0..3 in the same OK/High=3, freshly
 * Stale=60->2, long-Stale=40->1, Invalid/Unknown=0->0 order. */
static uint8_t Fmw_VsConfidence(uint8_t confidence) {
  uint8_t level;
  if (confidence >= 100u) {
    level = 3u;
  } else if (confidence >= 60u) {
    level = 2u;
  } else if (confidence >= 40u) {
    level = 1u;
  } else {
    level = 0u;
  }
  return level;
}

static void Fmw_VsFillEntry(LkvEntry_Type *e, uint16_t keyId, float value,
                             const Fmw_QualityCtxType *ctx, uint64_t now) {
  e->keyId = keyId;
  e->quality = Fmw_VsQuality(ctx->quality);
  e->confidence = Fmw_VsConfidence(ctx->confidence);
  e->timestampMs = now;
  e->ageMs = ctx->elapsedMs;
  e->value = value;
}

uint16_t FmwVehicleState_GetSnapshot(LkvEntry_Type *entries) {
  uint64_t now = FmwVehicleState_NowMs();
  uint16_t n = 0;

  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_SPEED, Speed_Kph, &Speed_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_TACHOMETER, EngineSpeed_Rpm, &TacoMeter_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_REVERSE, Reverse_Engaged ? 1.0f : 0.0f, &Reverse_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_PARKING_BRAKE, ParkingBrake_Applied ? 1.0f : 0.0f,
                  &ParkingBrake_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_AIRCON_ON_OFF, Aircon.acOnOff ? 1.0f : 0.0f, &Aircon_Q,
                  now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_AIRCON_TEMPERATURE, Aircon.targetTempC, &Aircon_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_AIRCON_FAN, (float)Aircon.fanLevel, &Aircon_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_AIRCON_DEFROST, Aircon.defrost ? 1.0f : 0.0f, &Aircon_Q,
                  now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_AIRCON_MODE, (float)Aircon.mode, &Aircon_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_ESC_IS_ERROR, ESC_Error ? 1.0f : 0.0f, &ESC_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_ABS_IS_ERROR, ABS_Error ? 1.0f : 0.0f, &ABS_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_SEAT_IS_BELTED, Seatbelt.row1Driver ? 1.0f : 0.0f,
                  &Seatbelt_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_DOOR_IS_OPEN, DoorOpen.row1Driver ? 1.0f : 0.0f,
                  &DoorOpen_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_AIRBAG_IS_DEPLOYED, Airbag.row1Driver ? 1.0f : 0.0f,
                  &Airbag_Q, now);
  Fmw_VsFillEntry(&entries[n++], FMW_VS_KEY_FUEL_RELATIVE_LEVEL, (float)LowFuel_Percent, &LowFuel_Q,
                  now);

  return n; /* == FMW_VS_NUM_KEYS */
}

void FmwVehicleState_ApplyHvacWrite(uint16_t keyId, float value) {
  /* acCmd/tempCmd/fanCmd/defCmd gate whether AirconCommand's corresponding
   * field is asserted on the bus at all (SYS-001 0x526 MM_*_Cmd bits); a
   * SOME/IP write is by definition an explicit assertion of that field, so
   * it also sets the gate. CmdMode has no such gate bit in SYS-001. */
  switch (keyId) {
  case FMW_VS_KEY_AIRCON_ON_OFF:
    AirconCommand.acCmd = TRUE;
    AirconCommand.cmdAcOnOff = (value != 0.0f) ? TRUE : FALSE;
    break;
  case FMW_VS_KEY_AIRCON_TEMPERATURE:
    AirconCommand.tempCmd = TRUE;
    AirconCommand.cmdTargetTempC = value;
    break;
  case FMW_VS_KEY_AIRCON_FAN:
    AirconCommand.fanCmd = TRUE;
    AirconCommand.cmdFanLevel = (uint8_t)value;
    break;
  case FMW_VS_KEY_AIRCON_DEFROST:
    AirconCommand.defCmd = TRUE;
    AirconCommand.cmdDefrost = (value != 0.0f) ? TRUE : FALSE;
    break;
  case FMW_VS_KEY_AIRCON_MODE:
    AirconCommand.cmdMode = (uint8_t)value;
    break;
  default:
    /* fmw_write_policy rejects every other key before calling this. */
    return;
  }
  /* Push immediately: Fmw_MainFunction10ms() only re-applies for the first
   * 1s after boot (docs/fmwapp.md 1.7), so a write arriving later would
   * otherwise never reach Com's Tx buffer. */
  Fmw_ApplyAirconCommand();
}
