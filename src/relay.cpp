#include "relay.h"

#include "status_led.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

namespace
{
/* Plain GPIO producer node, no driver binding -- see the overlay's comment.
 * GPIO_ACTIVE_HIGH is encoded in the devicetree flags, so gpio_pin_set_dt()
 * takes logical values here (1 = relay closed = load powered). If tracing
 * shows the plug's relay driver is inverting, fix the flag in the overlay
 * rather than negating here. */
const gpio_dt_spec sRelay = GPIO_DT_SPEC_GET(DT_NODELABEL(relay_drive), gpios);

bool sInitialised;
bool sOn;
} /* namespace */

int RelayInit(void)
{
	if (!gpio_is_ready_dt(&sRelay)) {
		LOG_ERR("Relay GPIO port %s not ready", sRelay.port ? sRelay.port->name : "<null>");
		return -ENODEV;
	}

	/* GPIO_OUTPUT_INACTIVE honours the devicetree polarity, so the relay
	 * starts open regardless of how the driver stage is wired.
	 * emberAfOnOffClusterInitCallback() (zcl_callbacks.cpp) may switch it
	 * back on shortly after, once the stack has restored the persisted
	 * OnOff attribute. */
	const int err = gpio_pin_configure_dt(&sRelay, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure relay pin %u (%d)", sRelay.pin, err);
		return err;
	}

	sInitialised = true;
	sOn = false;

	return 0;
}

int RelaySet(bool on)
{
	if (!sInitialised) {
		LOG_ERR("Relay used before init");
		return -ENODEV;
	}

	const int err = gpio_pin_set_dt(&sRelay, on);
	if (err) {
		LOG_ERR("Failed to drive relay pin (%d)", err);
		return err;
	}

	if (on != sOn) {
		LOG_INF("Relay %s", on ? "on" : "off");
	}
	sOn = on;

	/* Keep the plug's LED in step with the load. StatusLed applies its own
	 * precedence rules (commissioning blink outranks relay state), so this
	 * is unconditional here. */
	StatusLedSetRelayState(on);

	return 0;
}

bool RelayIsOn(void)
{
	return sOn;
}
