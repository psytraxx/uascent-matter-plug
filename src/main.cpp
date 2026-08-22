/*
 * Blink test for the Seeed XIAO nRF52840 Sense on nRF Connect SDK / Zephyr.
 *
 * Cycles the status indications so all three can be checked by eye:
 *   blue blinking = pairing, green = paired, red = error.
 */

#include "status_led.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("XIAO nRF52840 Sense blink test starting");

	if (StatusLedInit()) {
		LOG_ERR("Status LED init failed");
		return 0;
	}

	const StatusLedState cycle[] = {
		StatusLedState::Pairing,
		StatusLedState::Paired,
		StatusLedState::Error,
	};

	while (true) {
		for (StatusLedState state : cycle) {
			StatusLedSet(state);
			k_sleep(K_SECONDS(3));
		}
	}

	return 0;
}
