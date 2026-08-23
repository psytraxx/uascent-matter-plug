/*
 * LED indication for the smart plug.
 *
 * There are three LEDs, each driven from its own signal source, no
 * precedence needed between them:
 *
 *   - the plug's LED 1 (P0.17) mirrors RELAY state -- solid when the load
 *     is switched on, off when it is off.
 *   - the plug's LED 2 (D1) mirrors NETWORK/commissioning state -- blinking
 *     while the commissioning window is open, off once provisioned.
 *   - the on-board RGB LED mirrors both simultaneously in colour, and stays
 *     useful for debugging with the case open (the plug's own two LEDs are
 *     the only feedback visible once the enclosure is sealed).
 *
 * Splitting relay state and network state across two separate physical
 * LEDs means neither has to defer to the other -- unlike a single shared
 * LED, there is no case where commissioning blink must override relay
 * state, because they are never rendered on the same LED.
 *
 *   Plug LED 1 (relay)   Plug LED 2 (network)   On-board RGB   Condition
 *   ------------------   ---------------------  ------------   ---------
 *   mirrors relay         blinking               blue blinking  commissioning window open
 *   solid on               off                    green solid    relay on, commissioned
 *   off                     off                    off             relay off, commissioned
 *   fast blink              fast blink             red solid        fatal/unrecoverable
 *
 * Call UpdateNetworkState() from the Matter device-state callback and
 * UpdateRelayState() from the OnOff attribute-change callback; each drives
 * its own LED (plus the RGB mirror) independently.
 */

#pragma once

enum class NetworkLedState {
	Pairing, /* Commissioning window open: LED 2 blinks, RGB blinks blue. */
	Paired,  /* Provisioned and on a fabric: LED 2 off, RGB reflects relay. */
	Error,   /* Fatal/unrecoverable: LED 2 fast blinks, RGB fast blinks red. */
};

/* Initialises all five GPIO LED channels (on-board RGB + plug LED 1 + plug
 * LED 2). Returns 0 on success, negative errno on failure (in which case no
 * LED can be driven at all). */
int StatusLedInit(void);

/* Reports the Matter network/commissioning state. Drives plug LED 2 and the
 * RGB's blink axis; independent of relay state. */
void StatusLedSetNetworkState(NetworkLedState state);

/* Reports the relay's current on/off state. Drives plug LED 1 and the RGB's
 * colour axis; independent of network state. */
void StatusLedSetRelayState(bool relayOn);
