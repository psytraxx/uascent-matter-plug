#include "status_led.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

namespace
{
/* xiao_ble devicetree: led0 = red, led1 = green, led2 = blue, all ACTIVE_LOW.
 * led3 is the plug's own red LED (overlay alias -> plug_led_red, P0.17),
 * also ACTIVE_LOW to match. GPIO_ACTIVE_LOW is encoded in the devicetree
 * flags, so gpio_pin_set() takes logical values here (1 = lit) and the
 * driver inverts as needed. */
const gpio_dt_spec sLedRed = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const gpio_dt_spec sLedGreen = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
const gpio_dt_spec sLedBlue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
const gpio_dt_spec sLedPlug = GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios);

constexpr k_timeout_t kBlinkInterval = K_MSEC(500);
constexpr k_timeout_t kFastBlinkInterval = K_MSEC(150);

NetworkLedState sNetworkState = NetworkLedState::Error;
bool sRelayOn;
bool sInitialised;
bool sBlinkOn;

/* The board layer calls the LED handler once from Board::Init() with its
 * default state (DeviceDisconnected -> Error), which matches sNetworkState's
 * initial value above. Without this flag the "unchanged" guards below would
 * swallow that first call and leave every LED dark until the state next
 * changed -- and Board::UpdateDeviceState() has its own !=-guard, so that may
 * not be until commissioning starts. Force the first render regardless. */
bool sRendered;

k_timer sBlinkTimer;

void SetChannels(bool red, bool green, bool blue, bool plug)
{
	if (!sInitialised) {
		return;
	}

	gpio_pin_set_dt(&sLedRed, red);
	gpio_pin_set_dt(&sLedGreen, green);
	gpio_pin_set_dt(&sLedBlue, blue);
	gpio_pin_set_dt(&sLedPlug, plug);
}

/* Runs in ISR context; gpio_pin_set_dt() on nRF GPIO is a register write and
 * is safe here, so there is no need to bounce this to the app thread. */
void BlinkTimerHandler(k_timer *)
{
	sBlinkOn = !sBlinkOn;

	switch (sNetworkState) {
	case NetworkLedState::Pairing:
		/* Blue blinking on-board, red blinking on the plug -- the plug
		 * has no blue channel, so blink is the only signal it has. */
		SetChannels(false, false, sBlinkOn, sBlinkOn);
		break;
	case NetworkLedState::Error:
		/* Fast blink on both; solid red is reserved for "commissioned,
		 * relay off" so a fault has to look different from that. */
		SetChannels(sBlinkOn, false, false, sBlinkOn);
		break;
	case NetworkLedState::Paired:
		/* The timer is stopped whenever we are in Paired state (see
		 * Apply() below), so this branch is unreachable in practice. */
		break;
	}
}

/* Renders the current (sNetworkState, sRelayOn) pair. Commissioning takes
 * precedence over relay state on both LEDs; only once it clears does the
 * indication fall through to mirroring the relay. Idempotent -- safe to
 * call on every state change without tracking what was rendered last. */
void Apply()
{
	if (!sInitialised) {
		return;
	}

	sRendered = true;

	switch (sNetworkState) {
	case NetworkLedState::Pairing:
	case NetworkLedState::Error:
		/* Blink-driven states. Apply() only reaches here from
		 * StatusLedSetNetworkState(), never from
		 * StatusLedSetRelayState() (that one only calls Apply() when
		 * sNetworkState == Paired), so restarting the timer here
		 * cannot interrupt an in-progress blink over an unrelated
		 * relay-state change. */
	{
		const bool isError = sNetworkState == NetworkLedState::Error;
		const k_timeout_t interval = isError ? kFastBlinkInterval : kBlinkInterval;

		LOG_INF("Status: %s", isError ? "error (red blinking)"
					      : "pairing mode (blue blinking)");
		sBlinkOn = true;
		SetChannels(isError, false, !isError, true);
		k_timer_start(&sBlinkTimer, interval, interval);
		break;
	}

	case NetworkLedState::Paired:
		k_timer_stop(&sBlinkTimer);
		LOG_INF("Status: commissioned, relay %s", sRelayOn ? "on" : "off");
		SetChannels(false, sRelayOn, false, sRelayOn);
		break;
	}
}

} /* namespace */

int StatusLedInit(void)
{
	const gpio_dt_spec *leds[] = { &sLedRed, &sLedGreen, &sLedBlue, &sLedPlug };

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

	sNetworkState = state;
	Apply();
}

void StatusLedSetRelayState(bool relayOn)
{
	if (relayOn == sRelayOn) {
		return;
	}

	sRelayOn = relayOn;

	/* While commissioning is in progress the relay-state change is still
	 * recorded above, but does not touch the LEDs -- Apply() will pick it
	 * up once NetworkLedState::Paired is reported.
	 *
	 * No sRendered check here, unlike the network setter: the relay LED's
	 * "off" default is what StatusLedInit()'s GPIO_OUTPUT_INACTIVE already
	 * drove, so a redundant first RelaySet(false) has nothing to render. */
	if (sNetworkState == NetworkLedState::Paired) {
		Apply();
	}
}
