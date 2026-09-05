/*
 * LED indication for the smart plug.
 *
 * There are three indicators, each fed from its own signal source, no
 * precedence needed between them:
 *
 *   - the plug's LED 1 mirrors RELAY state -- solid when the load is
 *     switched on, off when it is off. It is *not* driven by this module:
 *     it sits on the relay drive net (header H2) and follows the relay in
 *     hardware, so it needs no GPIO. See boards/xiao_ble.overlay.
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
 *   Plug LED 1 (relay)  Plug LED 2 (network)  On-board RGB    Condition
 *   ------------------  --------------------  --------------  ------------------
 *   mirrors relay       blinking              blue, blinking  commissioning open
 *   solid on            off                   green solid     relay on, commissioned
 *   off                 off                   off             relay off, commissioned
 *   mirrors relay       fast blink            red, fast blink fatal/unrecoverable
 *
 * Call StatusLedSetNetworkState() from the Matter device-state callback and
 * StatusLedSetRelayState() from the OnOff attribute-change callback; the two
 * axes are rendered independently.
 */

#pragma once

enum class NetworkLedState {
	Pairing, /* Commissioning window open: LED 2 blinks, RGB blinks blue. */
	Paired,  /* Provisioned and on a fabric: LED 2 off, RGB reflects relay. */
	Error,   /* Fatal/unrecoverable: LED 2 fast blinks, RGB fast blinks red. */
};

/* Initialises the four GPIO LED channels this firmware drives (on-board RGB
 * + plug LED 2). Plug LED 1 is hardware-driven off the relay net and has no
 * channel here. Returns 0 on success, negative errno on failure (in which
 * case no LED can be driven at all). */
int StatusLedInit(void);

/* Reports the Matter network/commissioning state. Drives plug LED 2 and the
 * RGB's blink axis; independent of relay state. */
void StatusLedSetNetworkState(NetworkLedState state);

/* Reports the relay's current on/off state. Drives the RGB's colour axis;
 * plug LED 1 follows the relay in hardware, so nothing else is written.
 * Independent of network state. */
void StatusLedSetRelayState(bool relayOn);
