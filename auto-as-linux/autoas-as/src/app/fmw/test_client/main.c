/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2026 Parai Wang <parai@foxmail.com>
 *
 * Throwaway test client for VehicleStateService (see
 * autoas-as/src/app/fmw/README.md section 8 step 8 / section 10): finds the
 * service, subscribes to EvVehicleState, polls GetVehicleStateSnapshot, and
 * cycles through kWriteTests (SWARCH-002 6 scenario 4/5: valid HVAC writes
 * plus out-of-range/unsupported-key rejections) one per second, logging
 * PASS/FAIL against each case's expected result_code. Not part of the
 * production Gateway ECU image.
 */
/* ================================ [ INCLUDES  ] ============================================== */
#include <string.h>
#include "TcpIp.h"
#include "SoAd.h"
#include "Sd.h"
#include "SomeIp.h"
#include "SomeIpXf.h"

#include "Sd_Cfg.h"
#include "VehicleStateProxy.h"
#include "SomeIp_Cfg.h"
#include "SomeIpXf_Cfg.h"

#include "Std_Timer.h"
#include "Std_Debug.h"
/* ================================ [ MACROS    ] ============================================== */
#define AS_LOG_FMWVSC 1
/* ================================ [ DATAS     ] ============================================== */
static Std_TimerType timer10ms;
static Std_TimerType timer1s;
static boolean lIsAvailable = FALSE;
/* ================================ [ FUNCTIONS ] ============================================== */
void SomeIp_VehicleState_OnAvailability(boolean isAvailable) {
  ASLOG(FMWVSC, ("VehicleState %s\n", isAvailable ? "online" : "offline"));
  lIsAvailable = isAvailable;
}

void VehicleState_SubscribeEvVehicleStateAck(boolean isSubscribe) {
  ASLOG(FMWVSC, ("EventGroupVehicleState %s\n", isSubscribe ? "subscribed" : "unsubscribed"));
}

void VehicleState_EvVehicleState(const VehicleStateMessage_Type *sample) {
  ASLOG(FMWVSC, ("EvVehicleState: %u entries, schemaVersion=%u\n", sample->entriesLen,
                 sample->schemaVersion));
  if (sample->entriesLen > 0) {
    ASLOG(FMWVSC, ("  keyId=0x%04X value=%f quality=%u confidence=%u ageMs=%u\n",
                   sample->entries[0].keyId, sample->entries[0].value, sample->entries[0].quality,
                   sample->entries[0].confidence, sample->entries[0].ageMs));
  }
}

void VehicleState_GetVehicleStateSnapshot_Return(Std_ReturnType ercd,
                                                  const VehicleStateMessage_Type *response) {
  uint8_t i;
  ASLOG(FMWVSC, ("GetVehicleStateSnapshot %s(%u)\n", (E_OK == ercd) ? "success" : "failed", ercd));
  if ((E_OK == ercd) && (NULL != response)) {
    for (i = 0; i < response->entriesLen; i++) {
      ASLOG(FMWVSC, ("  [%u] keyId=0x%04X value=%f quality=%u confidence=%u ageMs=%u\n", i,
                     response->entries[i].keyId, response->entries[i].value,
                     response->entries[i].quality, response->entries[i].confidence,
                     response->entries[i].ageMs));
    }
  }
}

/* SWRS-GW-003 / SWARCH-002 6 scenario 4/5 test matrix: valid HVAC writes
 * (expect result_code=0x00 OK) plus out-of-range values and unsupported
 * (non-HVAC) keys (expect 0x01 INVALID / 0x02 UNSUPPORTED per
 * fmw_write_policy.c, mirroring SYS-002 4.1 ranges). */
typedef struct {
  const char *name;
  uint8_t entryCount;
  uint16_t keyId[2];
  float value[2];
  uint8_t expectedResultCode;
} WriteTestCaseType;

static const WriteTestCaseType kWriteTests[] = {
  {"valid: AC on + temp 24.0", 2, {0x1006, 0x1007}, {1.0f, 24.0f}, 0x00},
  {"valid: fan level 3", 1, {0x1008, 0}, {3.0f, 0.0f}, 0x00},
  {"valid: defrost on", 1, {0x1009, 0}, {1.0f, 0.0f}, 0x00},
  {"valid: mode 2 (Heat)", 1, {0x100A, 0}, {2.0f, 0.0f}, 0x00},
  {"invalid: temp 99.0 out of 16..30 range", 1, {0x1007, 0}, {99.0f, 0.0f}, 0x01},
  {"invalid: temp 5.0 out of 16..30 range", 1, {0x1007, 0}, {5.0f, 0.0f}, 0x01},
  {"invalid: fan 3.5 non-integral", 1, {0x1008, 0}, {3.5f, 0.0f}, 0x01},
  {"invalid: fan 9 out of 0..7 range", 1, {0x1008, 0}, {9.0f, 0.0f}, 0x01},
  {"invalid: mode 7 out of 0..6 range", 1, {0x100A, 0}, {7.0f, 0.0f}, 0x01},
  {"invalid: AC on/off 2.0 not 0/1", 1, {0x1006, 0}, {2.0f, 0.0f}, 0x01},
  {"unsupported: Speed 0x1001 not writable", 1, {0x1001, 0}, {10.0f, 0.0f}, 0x02},
  {"unsupported: Fuel 0x1011 not writable", 1, {0x1011, 0}, {50.0f, 0.0f}, 0x02},
};
#define WRITE_TEST_COUNT (sizeof(kWriteTests) / sizeof(kWriteTests[0]))
static uint32_t s_writeTestIndex = 0;
static boolean s_writeTestInFlight = FALSE;

void VehicleState_WriteVehicleState_Return(Std_ReturnType ercd, const WriteResponse_Type *response) {
  const WriteTestCaseType *tc = &kWriteTests[(s_writeTestIndex - 1) % WRITE_TEST_COUNT];
  s_writeTestInFlight = FALSE;
  ASLOG(FMWVSC, ("WriteVehicleState %s(%u)\n", (E_OK == ercd) ? "success" : "failed", ercd));
  if (NULL != response) {
    boolean pass = (boolean)(response->resultCode == tc->expectedResultCode);
    ASLOG(FMWVSC, ("  resultCode=%u failedEntryIndex=%u appliedLen=%u -- [%s] '%s' (expected "
                   "resultCode=%u)\n",
                   response->resultCode, response->failedEntryIndex, response->entriesLen,
                   pass ? "PASS" : "FAIL", tc->name, tc->expectedResultCode));
  }
}

static void RequestSnapshot(void) {
  (void)VehicleState_GetVehicleStateSnapshot();
}

static void RequestNextWriteTest(void) {
  WriteRequest_Type request;
  const WriteTestCaseType *tc;
  uint8_t i;

  if (TRUE == s_writeTestInFlight) {
    return; /* previous case's response has not arrived yet */
  }
  tc = &kWriteTests[s_writeTestIndex % WRITE_TEST_COUNT];
  s_writeTestIndex++;
  s_writeTestInFlight = TRUE;

  memset(&request, 0, sizeof(request));
  request.entriesLen = tc->entryCount;
  for (i = 0; i < tc->entryCount; i++) {
    request.entries[i].keyId = tc->keyId[i];
    request.entries[i].value = tc->value[i];
  }

  ASLOG(FMWVSC, ("WriteVehicleState request: '%s'\n", tc->name));
  (void)VehicleState_WriteVehicleState(&request);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  TcpIp_Init(NULL);
  SoAd_Init(NULL);
  Sd_Init(NULL);
  SomeIp_Init(NULL);

  VehicleState_FindService();

  Std_TimerStart(&timer10ms);
  Std_TimerStart(&timer1s);

  for (;;) {
    if (Std_GetTimerElapsedTime(&timer10ms) >= 10000) {
      Std_TimerStart(&timer10ms);
      TcpIp_MainFunction();
      SoAd_MainFunction();
      Sd_MainFunction();
      SomeIp_MainFunction();
    }

    if (Std_GetTimerElapsedTime(&timer1s) >= 1000000) {
      Std_TimerStart(&timer1s);
      if (TRUE == lIsAvailable) {
        static boolean subscribed = FALSE;
        if (FALSE == subscribed) {
          VehicleState_SubscribeEvVehicleState();
          subscribed = TRUE;
        }
        RequestSnapshot();
        RequestNextWriteTest();
      }
    }
  }

  return 0;
}
