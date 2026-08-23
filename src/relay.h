/*
 * Relay control for the smart plug.
 *
 * Drives the plug's existing relay via the GPIO the original BK7231N module
 * used (devicetree node "relay_drive"; see
 * boards/xiao_ble_nrf52840_sense.overlay). The pin assignment there is
 * provisional until the plug PCB is traced -- this module reads it from
 * devicetree so a re-trace never touches this code.
 *
 * Note the relay coil may be driven from a mains-derived rail rather than
 * 3V3. When the board is bench-powered over USB with no mains present, the
 * GPIO will toggle correctly but the relay may not physically click. That is
 * expected, not a fault; verify with a scope on the pad rather than by ear.
 * See docs/smart-plug-plan.md.
 */

#pragma once

/* Configures the relay GPIO, leaving the relay OFF. Returns 0 on success,
 * negative errno on failure. */
int RelayInit(void);

/* Switches the load. Safe to call repeatedly with the same value. */
int RelaySet(bool on);

/* Last value passed to RelaySet(), or false before the first call. Reflects
 * what the firmware commanded, not any sensed state -- the plug has no relay
 * feedback contact. */
bool RelayIsOn(void);
