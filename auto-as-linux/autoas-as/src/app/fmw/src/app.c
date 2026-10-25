/**
 * SSAS - Simple Smart Automotive Software
 * Copyright (C) 2021 Parai Wang <parai@foxmail.com>
 */
/* ================================ [ INCLUDES  ] ============================================== */
#include "app.h"
#include "Std_Timer.h"
#include "Std_Debug.h"
#include "Nm.h"
#include "CanNm.h"
#include "Com.h"
#if defined(_WIN32) || defined(linux)
#include <assert.h>
#endif
/* ================================ [ MACROS    ] ============================================== */
#define AS_LOG_APP 0
/* ================================ [ TYPES     ] ============================================== */
/* ================================ [ DECLARES  ] ============================================== */
#if defined(__HIWARE__)
#else
void __weak User_Init(void) {
}
void __weak User_MainTask10ms(void) {
}
#endif
extern void Fmw_Init(void);
extern void Fmw_MainFunction10ms(void);
/* ================================ [ DATAS     ] ============================================== */
static Std_TimerType timer10ms;
/* ================================ [ LOCALS    ] ============================================== */
/* ================================ [ FUNCTIONS ] ============================================== */
void App_Init(void) {
  User_Init();
  Fmw_Init();
  Std_TimerInit(&timer10ms, 10000);
}

void App_MainFunction(void) {
  if (TRUE == Std_IsTimerTimeout(&timer10ms)) {
    Std_TimerSet(&timer10ms, 10000);
    User_MainTask10ms();
    Fmw_MainFunction10ms();
  }
}

#if !defined(USE_COMM) && (defined(USE_CANNM) || defined(USE_UDPNM))
void Nm_NetworkStartIndication(NetworkHandleType nmNetworkHandle) {
#ifdef USE_CANNM
  CanNm_PassiveStartUp(nmNetworkHandle);
#endif
}
void Nm_NetworkMode(NetworkHandleType nmNetworkHandle) {
#if defined(USE_CANNM) && defined(CANNM_GLOBAL_PN_SUPPORT)
  CanNm_ConfirmPnAvailability(0);
#endif
}
void Nm_BusSleepMode(NetworkHandleType nmNetworkHandle) {
}
void Nm_PrepareBusSleepMode(NetworkHandleType nmNetworkHandle) {
#ifdef USE_CANNM
#ifdef USE_COM
  Com_IpduGroupStop(0);
#endif
#endif
}
void Nm_TxTimeoutException(NetworkHandleType nmNetworkHandle) {
  ASLOG(ERROR, ("Nm_TxTimeoutException(%d)\n", nmNetworkHandle));
}

void Nm_RepeatMessageIndication(NetworkHandleType nmNetworkHandle) {
}

void Nm_RemoteSleepIndication(NetworkHandleType nmNetworkHandle) {
  ASLOG(INFO, ("%d: NM Remote Sleep Ind\n", nmNetworkHandle));
}

void Nm_RemoteSleepCancellation(NetworkHandleType nmNetworkHandle) {
  ASLOG(INFO, ("%d: NM Remote Sleep Cancel\n", nmNetworkHandle));
}

void Nm_CoordReadyToSleepIndication(NetworkHandleType nmChannelHandle) {
}

void Nm_CoordReadyToSleepCancellation(NetworkHandleType nmChannelHandle) {
}

void Nm_StateChangeNotification(NetworkHandleType nmNetworkHandle, Nm_StateType nmPreviousState,
                                Nm_StateType nmCurrentState) {
#ifdef USE_COM
  if (NM_STATE_NORMAL_OPERATION == nmCurrentState) {
    Com_IpduGroupStart(0, TRUE);
  }
#endif
}

void Nm_CarWakeUpIndication(NetworkHandleType nmChannelHandle) {
}
#endif

void Fee_PanicUserAction(uint8_t fault) {
  /* user must handle this, this means Fee fault error and can't recover */
#if defined(_WIN32) || defined(linux)
  assert(0);
#else
#endif
}
