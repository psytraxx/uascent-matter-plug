/*
 * Status LED indication for Matter commissioning state.
 *
 * Blue blinking  - commissioning window open (pairing mode)
 * Green solid    - commissioned onto a Matter fabric
 * Red solid      - unrecoverable error
 */

#pragma once

enum class StatusLedState {
	Pairing, /* Commissioning window open: blink blue. */
	Paired,  /* Provisioned and on a fabric: solid green. */
	Error,   /* Fatal/unrecoverable: solid red. */
};

/* Initialises the three GPIO LED channels. Returns 0 on success, negative
 * errno on failure (in which case no LED can be driven at all). */
int StatusLedInit(void);

/* Switches the indication. Safe to call repeatedly with the same state; the
 * blink timer is only restarted when the state actually changes. */
void StatusLedSet(StatusLedState state);
