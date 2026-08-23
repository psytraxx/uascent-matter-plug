#include "status_led.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

namespace
{
/* xiao_ble devicetree: led0 = red, led1 = green, led2 = blue, all ACTIVE_LOW.
 * led3 is plug LED 1 (overlay alias -> plug_led_relay, P0.17); led4 is plug
 * LED 2 (overlay alias -> plug_led_network, D1/P0.03). Both plug LEDs are
 * assumed ACTIVE_LOW to match the on-board ones, pending independent
 * confirmation during tracing. GPIO_ACTIVE_LOW is encoded in the devicetree
 * flags, so gpio_pin_set() takes logical values here (1 = lit) and the
 * driver inverts as needed. */
const gpio_dt_spec sLedRed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const gpio_dt_spec sLedGreen = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
const gpio_dt_spec sLedBlue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
const gpio_dt_spec sPlugLedRelay = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);
const gpio_dt_spec sPlugLedNetwork = GPIO_DT_SPEC_GET(DT_ALIAS(led4), gpios);

constexpr k_timeout_t kBlinkInterval = K_MSEC(500);
constexpr k_timeout_t kFastBlinkInterval = K_MSEC(150);

NetworkLedState sNetworkState = NetworkLedState::Error;
bool sRelayOn;
bool sInitialised;
bool sBlinkOn;

/* The board layer calls the LED handler once from Board::Init() with its
 * default state (DeviceDisconnected -> Error), which matches sNetworkState's
 * initial value above. Without this flag the "unchanged" guard in
 * StatusLedSetNetworkState() would swallow that first call and leave LED 2
 * dark until the state next changed -- and Board::UpdateDeviceState() has
 * its own !=-guard, so that may not be until commissioning starts. Force the
 * first render regardless. */
bool sRendered;

k_timer sBlinkTimer;

/* Renders the relay axis: plug LED 1, and the RGB's green channel (only
 * meaningful once network state is Paired -- see RenderRgb()). */
void RenderRelay()
{
	if (!sInitialised) {
		return;
	}

	gpio_pin_set_dt(&sPlugLedRelay, sRelayOn);
}

/* Renders the RGB's colour for the current (network, relay, blink phase)
 * combination. Red/blue are network-driven; green is relay-driven, but only
 * shown once commissioning is out of the way so the RGB's colour axis does
 * not fight its own blink axis. */
void RenderRgb()
{
	if (!sInitialised) {
		return;
	}

	switch (sNetworkState) {
	case NetworkLedState::Pairing:
		gpio_pin_set_dt(&sLedRed, false);
		gpio_pin_set_dt(&sLedGreen, false);
		gpio_pin_set_dt(&sLedBlue, sBlinkOn);
		break;
	case NetworkLedState::Error:
		gpio_pin_set_dt(&sLedRed, sBlinkOn);
		gpio_pin_set_dt(&sLedGreen, false);
		gpio_pin_set_dt(&sLedBlue, false);
		break;
	case NetworkLedState::Paired:
		gpio_pin_set_dt(&sLedRed, false);
		gpio_pin_set_dt(&sLedGreen, sRelayOn);
		gpio_pin_set_dt(&sLedBlue, false);
		break;
	}
}

/* Runs in ISR context; gpio_pin_set_dt() on nRF GPIO is a register write and
 * is safe here, so there is no need to bounce this to the app thread. */
void BlinkTimerHandler(k_timer *)
{
	sBlinkOn = !sBlinkOn;

	/* Plug LED 2 mirrors the network axis directly: blinking in both
	 * Pairing and Error, the only two states for which this timer runs
	 * (see StatusLedSetNetworkState()). */
	gpio_pin_set_dt(&sPlugLedNetwork, sBlinkOn);
	RenderRgb();
}

} /* namespace */

int StatusLedInit(void)
{
	const gpio_dt_spec *leds[] = { &sLedRed, &sLedGreen, &sLedBlue, &sPlugLedRelay, &sPlugLedNetwork };

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

void StatusLedSetNetworkState(NetworkLedState state)
{
	if (sRendered && state == sNetworkState) {
		return;
	}

	sRendered = true;
	sNetworkState = state;

	if (!sInitialised) {
		return;
	}

	switch (state) {
	case NetworkLedState::Pairing:
	case NetworkLedState::Error: {
		const bool isError = state == NetworkLedState::Error;
		const k_timeout_t interval = isError ? kFastBlinkInterval : kBlinkInterval;

		LOG_INF("Status: %s", isError ? "error (fast blink)" : "pairing mode (blinking)");
		sBlinkOn = true;
		gpio_pin_set_dt(&sPlugLedNetwork, true);
		RenderRgb();
		k_timer_start(&sBlinkTimer, interval, interval);
		break;
	}

	case NetworkLedState::Paired:
		k_timer_stop(&sBlinkTimer);
		LOG_INF("Status: commissioned, relay %s", sRelayOn ? "on" : "off");
		gpio_pin_set_dt(&sPlugLedNetwork, false);
		RenderRgb();
		break;
	}
}

void StatusLedSetRelayState(bool relayOn)
{
	if (relayOn == sRelayOn) {
		return;
	}

	sRelayOn = relayOn;
	RenderRelay();

	/* The RGB's green channel only shows once commissioning is out of the
	 * way (see RenderRgb()); harmless to call unconditionally otherwise,
	 * since RenderRgb() re-derives red/blue from sNetworkState too. */
	RenderRgb();
}
