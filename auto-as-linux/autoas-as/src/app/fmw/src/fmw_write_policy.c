/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2026 Parai Wang <parai@foxmail.com>
 */
/* ================================ [ INCLUDES  ] ============================================== */
#include "fmw_write_policy.h"
#include "fmw_vehicle_state_bridge.h"
/* ================================ [ MACROS    ] ============================================== */
/* ================================ [ LOCALS    ] ============================================== */
static boolean Fmw_WpIsWritableKey(uint16_t keyId) {
  return (boolean)((keyId >= FMW_VS_KEY_AIRCON_ON_OFF) && (keyId <= FMW_VS_KEY_AIRCON_MODE));
}

/* SYS-002 4.1 per-key valid ranges. The fixed-length schema (README.md 3.3)
 * dropped the wire value_type tag, so bool/uint8 fields are validated as
 * "integral value within range" on the float wire value instead of a
 * separate type check. */
static boolean Fmw_WpInRange(uint16_t keyId, float value) {
  boolean ok;
  switch (keyId) {
  case FMW_VS_KEY_AIRCON_ON_OFF:
  case FMW_VS_KEY_AIRCON_DEFROST:
    ok = (boolean)((0.0f == value) || (1.0f == value));
    break;
  case FMW_VS_KEY_AIRCON_TEMPERATURE:
    ok = (boolean)((value >= 16.0f) && (value <= 30.0f));
    break;
  case FMW_VS_KEY_AIRCON_FAN:
    ok = (boolean)((value >= 0.0f) && (value <= 7.0f) && (value == (float)(int)value));
    break;
  case FMW_VS_KEY_AIRCON_MODE:
    ok = (boolean)((value >= 0.0f) && (value <= 6.0f) && (value == (float)(int)value));
    break;
  default:
    ok = FALSE;
    break;
  }
  return ok;
}
/* ================================ [ FUNCTIONS ] ============================================== */
void FmwWritePolicy_Apply(const WriteRequest_Type *request, WriteResponse_Type *response) {
  uint8_t i;

  response->entriesLen = 0;

  for (i = 0; i < request->entriesLen; i++) {
    uint16_t keyId = request->entries[i].keyId;
    if (FALSE == Fmw_WpIsWritableKey(keyId)) {
      response->resultCode = FMW_VS_WRITE_UNSUPPORTED;
      response->failedEntryIndex = i;
      return;
    }
    if (FALSE == Fmw_WpInRange(keyId, request->entries[i].value)) {
      response->resultCode = FMW_VS_WRITE_INVALID;
      response->failedEntryIndex = i;
      return;
    }
  }

  /* Every entry validated: apply atomically (SWARCH-002 4), then echo each
   * with a fresh stamp as applied_state. */
  {
    uint64_t now = FmwVehicleState_NowMs();
    for (i = 0; i < request->entriesLen; i++) {
      uint16_t keyId = request->entries[i].keyId;
      float value = request->entries[i].value;

      FmwVehicleState_ApplyHvacWrite(keyId, value);

      response->entries[i].keyId = keyId;
      response->entries[i].value = value;
      response->entries[i].quality = FMW_VS_QUALITY_OK;
      response->entries[i].confidence = 3u; /* High */
      response->entries[i].timestampMs = now;
      response->entries[i].ageMs = 0u;
    }
    response->entriesLen = request->entriesLen;
  }
  response->resultCode = FMW_VS_WRITE_OK;
  response->failedEntryIndex = FMW_VS_WRITE_NO_FAILED_INDEX;
}
