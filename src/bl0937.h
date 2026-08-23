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

/* Called periodically (see kMeterPollIntervalMs in app_task.cpp) to turn the
 * pulses counted since the last call into a reading and push it into the
 * Matter clusters via PowerMeasurementUpdate(). */
void MeterPoll(void);
