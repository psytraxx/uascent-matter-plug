/*
 * BL0937 energy-metering chip driver.
 *
 * No Zephyr in-tree driver exists for this part, so this is written from
 * scratch against the chip's pulse-frequency output: CF's frequency is
 * proportional to active power, CF1's frequency is proportional to RMS
 * voltage or RMS current depending on SEL. See docs/smart-plug-plan.md's
 * Phase 3 section for the design this follows.
 *
 */

#pragma once

void MeterInit(void);

/* Called once per second (kMeterPollIntervalMs in app_task.cpp) to turn the
 * pulses counted since the last call into a reading and push it into the
 * Matter clusters via PowerMeasurementUpdate().
 *
 * The rate is part of the calibration, not a free parameter: readings are a
 * median of three consecutive samples, so active power refreshes every 3 s,
 * and SEL holds for one such window so V and I take turns and each refreshes
 * every 6 s -- the cadence the stock firmware used to derive the calibration
 * divisors. See docs/original-firmware.md. */
void MeterPoll(void);
