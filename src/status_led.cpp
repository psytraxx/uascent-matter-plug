#include "status_led.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

namespace
{
/* xiao_ble devicetree: led0 = red, led1 = green, led2 = blue, all ACTIVE_LOW.
 * GPIO_ACTIVE_LOW is encoded in the devicetree flags, so gpio_pin_set() takes
 * logical values here (1 = lit) and the driver inverts as needed. */
const gpio_dt_spec sLedRed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const gpio_dt_spec sLedGreen = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
const gpio_dt_spec sLedBlue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

constexpr k_timeout_t kBlinkInterval = K_MSEC(500);

StatusLedState sState = StatusLedState::Error;
bool sInitialised;
bool sBlinkOn;

k_timer sBlinkTimer;

void SetChannels(bool red, bool green, bool blue)
{
	if (!sInitialised) {
		return;
	}

	gpio_pin_set_dt(&sLedRed, red);
	gpio_pin_set_dt(&sLedGreen, green);
	gpio_pin_set_dt(&sLedBlue, blue);
}

/* Runs in ISR context; gpio_pin_set_dt() on nRF GPIO is a register write and
 * is safe here, so there is no need to bounce this to the app thread. */
void BlinkTimerHandler(k_timer *)
{
	sBlinkOn = !sBlinkOn;
	SetChannels(false, false, sBlinkOn);
}

} /* namespace */

int StatusLedInit(void)
{
	const gpio_dt_spec *leds[] = { &sLedRed, &sLedGreen, &sLedBlue };

	for (const gpio_dt_spec *led : leds) {
		if (!gpio_is_ready_dt(led)) {
			LOG_ERR("LED GPIO port %s not ready", led->port ? led->port->name : "<null>");
			return -ENODEV;
		}

		const int err = gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
		if (err) {
			LOG_ERR("Failed to configure LED pin %u (%d)", led->pin, err);
			return err;
		}
	}

	k_timer_init(&sBlinkTimer, BlinkTimerHandler, nullptr);
	sInitialised = true;

	return 0;
}

void StatusLedSet(StatusLedState state)
{
	if (sInitialised && state == sState) {
		return;
	}

	sState = state;
	k_timer_stop(&sBlinkTimer);

	switch (state) {
	case StatusLedState::Pairing:
		LOG_INF("Status: pairing mode (blue blinking)");
		sBlinkOn = true;
		SetChannels(false, false, true);
		k_timer_start(&sBlinkTimer, kBlinkInterval, kBlinkInterval);
		break;

	case StatusLedState::Paired:
		LOG_INF("Status: commissioned (green)");
		SetChannels(false, true, false);
		break;

	case StatusLedState::Error:
		LOG_ERR("Status: error (red)");
		SetChannels(true, false, false);
		break;
	}
}
