#pragma once

/** Log Thread role, IPv6 addresses, on-mesh prefixes and SRP registration every 10 s
 *  until commissioning completes (no-op when CONFIG_THREAD_DIAG_LOG is off).
 *  Call after esp_matter::start(), which brings up OpenThread. */
void thread_diag_start();

/** Stop the periodic report (takes effect at the next 10 s tick). Safe to call from any task. */
void thread_diag_stop();
