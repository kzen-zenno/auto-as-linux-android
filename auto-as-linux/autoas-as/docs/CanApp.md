# CanApp Sample Overview

This page explains what the `CanApp` sample implements and where the key source code lives.

## What CanApp Is

`CanApp` is the CAN-based application sample used in this repository. It is not just a CAN node; it is a complete AUTOSAR-style application that brings together:

- CAN communication
- DCM diagnostic handling
- NM / EcuM startup and sleep coordination
- Shell, VFS, plugin, and platform-simulator support

On Linux, the verified binary is:

```sh
as/build/posix/GCC/CanApp/CanApp
```

## Main Test Scenario

The most visible sample behavior is a UDS diagnostic session control test.
`IsoTpSend` sends `10 01`, and `CanApp` replies with a positive response such as `50 01 00 32 01 F4`.

That flow is the same one shown in the build notes and was confirmed in this workspace.

## Where The Implementation Lives

The sample is split across a few source files:

- [app/app/main.c](../app/app/main.c): main task loop, timers, sleep/run handling
- [app/app/src/app.c](../app/app/src/app.c): app initialization and periodic task hook
- [app/app/src/uds.c](../app/app/src/uds.c): DCM session/security callbacks
- [app/platform/simulator/src/simulator.c](../app/platform/simulator/src/simulator.c): simulator-specific jump to bootloader / program session handling
- [app/app/src/authentication.c](../app/app/src/authentication.c): optional DCM authentication support

## Startup And Main Loop

The app entry point is the periodic task loop in [app/app/main.c](../app/app/main.c). It initializes the application, services timers, runs shell and app callbacks, and manages EcuM transitions.

```c
void Task_MainInitTwo(void) {
  Cdd_Init();
  App_Init();
  Std_TimerInit(&timer1ms, 1000);
  timer10ms = 0;
  timer100ms = 0;
}

void Task_MainLoop(void) {
  EcuM_StartupTwo();
  Task_MainInitTwo();
  for (; FALSE == Mcu_IsResetRequested(); ) {
    if (TRUE == Std_IsTimerTimeout(&timer1ms)) {
      Std_TimerSet(&timer1ms, 1000);
      timer10ms++;
      if (timer10ms >= 10) {
        timer10ms = 0;
        MainTask_10ms();
      }
      timer100ms++;
      if (timer100ms >= 100) {
        timer100ms = 0;
        App_AliveIndicate();
      }
      EcuM_BswServiceFast();
    }
    Shell_MainFunction();
    App_MainFunction();
    OSAL_SleepUs(1000);
  }
}
```

`App_Init()` and `App_MainFunction()` are intentionally small in [app/app/src/app.c](../app/app/src/app.c); they only set up periodic execution and call the user hook.

```c
void App_Init(void) {
	User_Init();
	Std_TimerInit(&timer10ms, 10000);
	Std_TimerInit(&timer1s, 1000000);
}

void App_MainFunction(void) {
	if (TRUE == Std_IsTimerTimeout(&timer10ms)) {
		User_MainTask10ms();
		Std_TimerSet(&timer10ms, 10000);
	}
}
```

## Diagnostic Session Handling

The key diagnostic behavior is implemented in [app/app/src/uds.c](../app/app/src/uds.c).
When DCM asks to change sessions, the app checks whether the transition is allowed and whether security access is satisfied.

```c
Std_ReturnType App_GetSessionChangePermission(Dcm_SesCtrlType sesCtrlTypeActive,
                                              Dcm_SesCtrlType sesCtrlTypeNew,
                                              Dcm_NegativeResponseCodeType *nrc) {
  Std_ReturnType ercd = E_OK;

#ifndef DCM_DISABLE_PROGRAM_SESSION_PROTECTION
	if ((DCM_PROGRAMMING_SESSION == sesCtrlTypeNew) &&
			(DCM_EXTENDED_DIAGNOSTIC_SESSION != sesCtrlTypeActive)) {
		*nrc = DCM_E_REQUEST_SEQUENCE_ERROR;
		ercd = E_NOT_OK;
	}
	if ((DCM_PROGRAMMING_SESSION == sesCtrlTypeNew) &&
			(DCM_EXTENDED_DIAGNOSTIC_SESSION == sesCtrlTypeActive)) {
		Dcm_SecLevelType level = DCM_SEC_LEV_LOCKED;
		(void)Dcm_GetSecurityLevel(&level);

		if (App_SECURITY_LEVEL_EXTDS != level) {
			*nrc = DCM_E_SECUTITY_ACCESS_DENIED;
			ercd = E_NOT_OK;
		}
	}
#endif

#ifdef USE_NVM
	if (E_OK == ercd) {
		if (DCM_PROGRAMMING_SESSION == sesCtrlTypeNew) {
			MemIf_StatusType status;
			status = NvM_GetStatus();
			if (MEMIF_BUSY == status) {
				*nrc = DCM_E_RESPONSE_PENDING;
			}
		}
	}
#endif

	return ercd;
}
```

The sample also defines the seed/key callbacks for extended and programming sessions.

```c
Std_ReturnType App_GetProgramLevelSeed(uint8_t *seed, Dcm_NegativeResponseCodeType *errorCode) {
  uint32_t u32Seed;
  uint32_t u32Time = Std_GetTime();

  app_prgs_seed = app_prgs_seed ^ u32Seed ^ u32Time ^ 0xfeedbeef;
  seed[0] = (uint8_t)(app_prgs_seed >> 24);
  seed[1] = (uint8_t)(app_prgs_seed >> 16);
  seed[2] = (uint8_t)(app_prgs_seed >> 8);
  seed[3] = (uint8_t)(app_prgs_seed);
  return E_OK;
}

Std_ReturnType App_CompareExtendedLevelKey(const uint8_t *key,
                                           Dcm_NegativeResponseCodeType *errorCode) {
  uint32_t u32Key = ((uint32_t)key[0] << 24) + ((uint32_t)key[1] << 16) +
                    ((uint32_t)key[2] << 8) + ((uint32_t)key[3]);
  uint32_t u32KeyExpected = app_extds_seed ^ 0x78934673;

  if (u32KeyExpected == u32Key) {
    return E_OK;
  }

  *errorCode = DCM_E_INVALID_KEY;
  return E_NOT_OK;
}
```

## What Happens After A Programming Session Request

In the simulator, programming-session entry is handled by [app/platform/simulator/src/simulator.c](../app/platform/simulator/src/simulator.c).
When `CanApp` accepts the session change, the sample updates a flag and jumps to the bootloader executable.

```c
void App_EnterProgramSession(void) {
	char *blPath;
	char defaultPath[] = AS_BUILD_DIR "/CanBL/CanBL" AS_APP_SUFFIX;
	char *args[2] = {NULL};
	FILE *flagFile;

	blPath = getenv("BL_PATH");
	if (NULL == blPath) {
		blPath = defaultPath;
	}

	flagFile = fopen("build/bl_update_request.flag", "w");
	if (flagFile) {
		fclose(flagFile);
	}

	ASLOG(INFO, ("App: requesting jump to BL: %s\n", blPath));

	args[0] = blPath;
	execv(blPath, args);
}
```

## Optional Authentication Support

If `USE_CRYPTO` is enabled, [app/app/src/authentication.c](../app/app/src/authentication.c) provides DCM authentication callbacks.
The file loads the CA public key from `CA_PUBLIC_KEY` or `ca_public_key.txt`, then verifies certificates and proof-of-ownership data.

## Shell Interface

`CanApp` also ships with a built-in shell. On Linux, [infras/system/shell/shell.c](../infras/system/shell/shell.c) starts a stdin reader thread during `Shell_Init()` and prints the `as #` prompt. The app calls `Shell_MainFunction()` from the main loop, so shell commands are processed while the application keeps running.

```c
void Shell_Init(void) {
#if defined(_WIN32) || defined(linux)
  if (NULL == lStdinThread) {
    lStdinThread = OSAL_ThreadCreate(ProcessStdio, NULL);
  }
#endif
  Shell_Prompt();
}

static void Shell_Prompt(void) {
  PRINTF("as # ");
}
```

The shell command table is built from `SHELL_REGISTER(...)` entries across modules. Two useful examples are the COM signal commands and the trace dump command:

```c
SHELL_REGISTER(lssg, "list all the value of com signals\n", cmdComLsSgFunc);
SHELL_REGISTER(wrsg,
               "wrsg sid value [gid]\n"
               "  write signal, if sid is group signals, need the gid\n",
               cmdComWrSgFunc);
```

```c
static int Shell_TraceApp(int argc, const char *argv[]) {
  Std_TraceDump(&Std_TraceArea_APP);
  return 0;
}
SHELL_REGISTER(trace_app, "trace_app\n", Shell_TraceApp)
```

From the prompt you can use commands such as:

- `help` to list commands
- `lssg` to inspect COM signals
- `wrsg` to write a COM signal
- `trace_app` to dump the application trace area
- `ls`, `cd`, `pwd`, `mkdir`, `rm`, `cp`, `cat`, `hexdump`, `mount`, `mkfs`, `lsdev`, `free`

The `help` output also exposes testing shortcuts such as `ig`, `key`, and `lscan`, which are useful for ignition and network-management related checks.

### Shell Command Examples

The following examples were confirmed on a running `CanApp` shell.

```sh
as # help
lssg - list all the value of com signals

wrsg - wrsg sid value [gid]
  write signal, if sid is group signals, need the gid

trace_app - trace_app

key - legacy key to do some sanity test
  key 0|1|2|3|4|5|6|7|8|9: report DTC prefail
  key p|q|w|e|r|t|y|u|i|o: report DTC prepass
  key s: start operation ignition
  key k: stop operation ignition
  key x: toggle NM status

ig - toggle ignition

lscan - list can status

help - help [cmd]

ls - ls [path]
  list files of current directory or path directory

cd - cd path
  change current working directory

pwd - show full path of current working directory
```

```sh
as # lssg
as # 0 SECOC_MSG0_TX
1 CAN0_CanNmUserData_TX
2 CAN0_TxMsgTime
3 SECOC_MSG1_RX
4 CAN0_RxMsgAbsInfo
5 CAN1_CanNmUserData_TX
6 CAN1_TxMsgTime
7 CAN1_RxMsgAbsInfo
```

```sh
as # trace_app
as #
```

```sh
as # ig
as # INFO    :2026-07-20 18:27:07:000 IG OFF
as # ECUM    :2026-07-20 18:27:07:000 Enter Post Run
```

```sh
as # key s
```

```sh
as # wrsg 0 0x12 0
as #
```

For day-to-day checks, `help`, `pwd`, `ls`, `cd`, `lssg`, `wrsg`, `trace_app`, `ig`, `key`, and `lscan` are the most practical commands.

## How To Run The Sample

The minimum check is:

```sh
cd /home/<user>/work/auto-as/as/build/posix/GCC/CanApp
./CanApp
```

Then from another terminal:

```sh
cd /home/<user>/work/auto-as/as
LD_LIBRARY_PATH=/home/<user>/work/auto-as/as/build/posix/GCC/prebuilt \
  ./build/posix/GCC/IsoTpSend/IsoTpSend -v 1001
```

Expected output:

```text
TX: 10 01
RX: 50 01 00 32 01 F4
```

That result shows the application sample is accepting a UDS diagnostic session request and responding correctly through the CAN simulation stack.
