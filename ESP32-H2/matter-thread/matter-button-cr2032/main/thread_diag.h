#pragma once

/** Log Thread role, IPv6 addresses, on-mesh prefixes and SRP registration every 10 s (no-op when
 *  CONFIG_THREAD_DIAG_LOG is off). The battery button runs it only while a first commissioning window is open.
 *  Call after esp_matter::start(), which brings up OpenThread. Can be started again after a stop. */
void thread_diag_start();

/** Stop the periodic report (takes effect at the next 10 s tick; the task then blocks without any wakeup).
 *  Safe to call from any task. */
void thread_diag_stop();
