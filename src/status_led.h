/*
 * LED indication for the smart plug.
 *
 * There are two independent LEDs, driven from two different signal sources:
 *
 *   - the plug's own red LED (P0.17) mirrors RELAY state -- solid when the
 *     load is switched on, off when it is off. That is what the enclosure
 *     shows the user day to day.
 *   - the on-board RGB LED mirrors the same information in colour, plus
 *     network/commissioning state, and stays useful for debugging with the
 *     case open.
 *
 * Commissioning takes priority over relay state on BOTH LEDs: while the
 * commissioning window is open, both blink regardless of the relay, since
 * that is what the user needs to see in that moment. Once commissioning
 * completes (or was never entered), the LEDs revert to mirroring the relay.
 *
 *   Plug LED (red)          On-board RGB           Condition
 *   ---------------          -----------            ---------
 *   blinking                 blue blinking          commissioning window open
 *   solid on                 green solid            relay on, commissioned
 *   off                      off                     relay off, commissioned
 *   fast blink                red solid               fatal/unrecoverable
 *
 * Call UpdateNetworkState() from the Matter device-state callback and
 * UpdateRelayState() from the OnOff attribute-change callback; whichever
 * fires last wins for its own axis, and commissioning precedence is
 * re-evaluated on every call so the two never need to coordinate directly.
 */

#pragma once

enum class NetworkLedState {
	Pairing, /* Commissioning window open: blink blue / blink red. */
	Paired,  /* Provisioned and on a fabric: defer to relay state. */
	Error,   /* Fatal/unrecoverable: solid red / fast red blink. */
};

/* Initialises all four GPIO LED channels (on-board RGB + plug red).
 * Returns 0 on success, negative errno on failure (in which case no LED can
 * be driven at all). */
int StatusLedInit(void);

/* Reports the Matter network/commissioning state. See precedence rules
 * above -- this can override relay-state indication on both LEDs. */
void StatusLedSetNetworkState(NetworkLedState state);

/* Reports the relay's current on/off state. Ignored for indication purposes
 * while a commissioning window is open; remembered and applied as soon as
 * commissioning state clears. */
void StatusLedSetRelayState(bool relayOn);
