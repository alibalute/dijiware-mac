# Recommended FreeRTOS (ESP-IDF) menuconfig for eTar

These settings suit the eTar firmware: real-time strum/tap detection, 1 ms loop delay, MIDI (UART + BLE), and multiple tasks with priorities 1–8.

Run **`idf.py menuconfig`**, then go to:

**Component config → FreeRTOS**

---

## Kernel

| Option | Recommended | Why |
|--------|-------------|-----|
| **configTICK_RATE_HZ** (Tick rate (Hz)) | **1000** | Default is 100 Hz (10 ms per tick). The main loop uses `vTaskDelay(pdMS_TO_TICKS(1))`; at 100 Hz that can round to 0–10 ms and cause jitter or contribute to freezes (especially with BLE). 1000 Hz gives 1 ms resolution and stable timing for strum/tap and `timer_check()`. |
| **Max number of priorities** (configMAX_PRIORITIES) | **25** (default) or ≥ 8 | Your tasks use priorities 1, 6, 7, 8. Default 25 is fine. |
| **Tick hook** | Off (default) | No need unless you add a tick hook. |
| **Idle task hook** | Off (default) | No need unless you add an idle hook. |
| **Stack overflow checking** | **None** or **Printf** (if it builds) | You had `configCHECK_FOR_STACK_OVERFLOW` disabled due to build error; leave as None unless you fix the build and want overflow detection. |

---

## Port

| Option | Recommended | Why |
|--------|-------------|-----|
| **Run FreeRTOS only on first core** (CONFIG_FREERTOS_UNICORE) | **No** (default) | You use both cores: `task_uart_midi` is pinned to core 1; other tasks can run on either core. Keep dual-core. |
| **Core to run FreeRTOS kernel** | **Core 0** (default) | Core 0 keeps the tick count; leave as is. |
| **Resolution of tick interrupt** | **1 ms** (if available) or match tick rate | Aligns with 1000 Hz tick and 1 ms delays. |

---

## Thread local storage, hooks, timers, and stats

Your code does **not** use task local storage, idle/tick hooks, or `xTimer*` software timers. Recommendations:

| Option | Recommended | Why |
|--------|-------------|-----|
| **configNUM_THREAD_LOCAL_STORAGE_POINTERS** | **0** | You don’t use `pvTaskGetThreadLocalStoragePointer` / `vTaskSetThreadLocalStoragePointer`. 0 saves a few bytes per task. Use 1 only if you add TLS later. |
| **configMINIMAL_STACK_SIZE** (Idle task stack size) | **1536** | Fine for idle. Increase only if you enable an idle hook that needs more stack. |
| **configUSE_IDLE_HOOK** | **Off (0)** | No idle hook in the project; off avoids unnecessary calls each idle cycle. |
| **configUSE_TICK_HOOK** | **Off (0)** | No tick hook in the project; off avoids extra work every tick. |
| **configMAX_TASK_NAME_LEN** | **16** | Longest task name is 14 chars (`task_uart_midi`). 16 is enough. |
| **configENABLE_BACKWARD_COMPATIBILITY** | **On (1)** | Keep ESP-IDF default. Some components may rely on legacy names/behavior. |
| **configTIMER_TASK_PRIORITY** | **1** | You don’t use software timers; this only affects the timer daemon if something in IDF does. 1 is fine. |
| **configTIMER_TASK_STACK_DEPTH** | **2048** | Adequate for the timer service task. |
| **configTIMER_QUEUE_LENGTH** | **10** | Enough for timer commands unless you add many software timers. |
| **configQUEUE_REGISTRY_SIZE** | **0** | Only needed for kernel-aware debuggers that name queues. 0 saves RAM. |
| **configUSE_TRACE_FACILITY** | **Off (0)** | Adds TCB members for tracing. Off saves RAM; turn on only if you use a trace tool. |
| **configGENERATE_RUN_TIME_STATS** | **Off (0)** | Needed only for `vTaskGetRunTimeStats()`. Off avoids overhead; turn on only for profiling. |

---

## Debug, hooks, mutex, ISR, and flash placement

| Option | Recommended | Why |
|--------|-------------|-----|
| **Enable stack overflow debug watchpoint** | **Off** (dev: **On** to find overflows) | Uses a hardware watchpoint to catch stack overflow. Helpful when debugging stack issues; uses a limited watchpoint. **Production: Off.** Development: On if you suspect stack overflows. |
| **Enable static task clean up hook** | **Off** | You don’t use `vTaskSetTaskCleanUpHook`. Turn on only if you add a per-task cleanup hook. |
| **Check that mutex semaphore is given by owner task** | **On** (dev) / **Off** (production) | Asserts that only the task that took the mutex gives it back. Catches misuse; small runtime cost. **Development: On.** Production: Off if you need to minimize overhead. |
| **ISR stack size** | **1536** | Stack for interrupt service routines. 1536 is a common default. Increase (e.g. 2048) only if you see crashes in ISRs or use heavy/nested interrupts. |
| **Enable backtrace from interrupt to task context** | **Off** | Adds debug backtrace when switching from ISR to task. Useful only for low-level debugging; leave off normally. |
| **Tick timer source (Xtensa only)** | **SYSTIMER 0 (level 1)** or default | Default tick source for ESP32/ESP32-S3 (Xtensa). Keep default unless IDF or docs suggest otherwise. |
| **Place FreeRTOS functions into Flash** | **On** | Moves part of the kernel from IRAM to Flash. **Saves IRAM** (important on ESP32); small latency cost. Usually recommended. |
| **Place task snapshot functions into flash** | **On** (if snapshots enabled) / N/A | Only relevant if task snapshot is enabled. Puts snapshot code in Flash to save IRAM. |
| **Tests compliance with Vanilla FreeRTOS port*_CRITICAL calls** | **Off** | Development check for critical-section API use. **Production: Off.** |
| **Halt when an SMP-untested function is called** | **Off** | Stops when an SMP-untested FreeRTOS API is used. For SMP bring-up/debug only. **Production: Off.** |
| **Enable task snapshot functions** | **Off** | Task snapshot API for debugging. Adds code and RAM. **Off** unless you use it (e.g. with SystemView). |

---

## Optional (tuning / debugging)

| Option | Suggested | Why |
|--------|------------|-----|
| **Enable FreeRTOS trace facility** | Off | Only for tracing; adds overhead. |
| **Enable FreeRTOS stats formatting** | Off | Only if you use `vTaskGetRunTimeStats()` or similar. |
| **Task watchdog** | On (default) | Helps catch tasks that block too long; keep on. |
| **Interrupt watchdog** | On (default) | Helps catch ISRs that run too long; keep on. |

---

## Summary (quick checklist)

1. **Component config → FreeRTOS → Kernel**
   - Set **Tick rate (Hz)** to **1000**.

2. **Component config → FreeRTOS → Port**
   - Leave **Run FreeRTOS only on first core** = **No**.

3. Save and exit menuconfig, then rebuild.

---

## Your current task layout (for reference)

| Task           | Stack | Priority | Core   |
|----------------|-------|----------|--------|
| volumeTask     | 4096  | 8        | any    |
| etarTask       | 8192  | 8        | any    |
| task_uart_midi | 4096  | 7        | 1      |
| buttonsTask    | 4096  | 6        | any    |
| batteryTask    | 4096  | 6        | any    |
| wifiTask       | 8192  | 6        | any    |
| task_ble_midi  | 4096  | 1        | any    |

### If the system still freezes

1. **Turn off "Halt when an SMP-untested function is called"** (Component config → FreeRTOS). When this is **on**, BLE or IDF can trigger a halt that looks like a freeze. This is now disabled in `sdkconfig.defaults`; run `idf.py fullclean` then `idf.py build` to apply, or set it in menuconfig.
2. **Increase etarTask stack** in `main.c`: change the stack size in `xTaskCreate(etarTask, "etarTask", ...)` to **10240** or **12288** to rule out stack overflow.

**Unicore:** Do **not** set **Run FreeRTOS only on first core** to **Yes**. `task_uart_midi` is pinned to **core 1** by design; with unicore everything would run on core 0 and you would lose the dedicated UART MIDI core.

Max priority in use is **8**; tick rate **1000 Hz** makes `pdMS_TO_TICKS(1)` and `timer_check()` timing accurate and can reduce the “freezing when BLE is on” type of issues.
