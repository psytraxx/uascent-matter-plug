/*
 * Synthetic power/energy source, standing in for the BL0937 driver (Phase 3,
 * not written yet).
 *
 * Two jobs:
 *  - Step C of the plan's fit check: link both measurement clusters against
 *    real data flow (attribute storage, TLV encoding, reporting) without any
 *    hardware, to prove the firmware fits before the PCB is traced.
 *  - Ongoing: a build option to test the Matter side without mains, since the
 *    real driver can only be validated with the plug on mains (see
 *    docs/smart-plug-plan.md).
 *
 * Selected by CONFIG_APP_METER_STUB; the real driver will present the same
 * MeterInit()/MeterPoll() shape so main.cpp does not change when it lands.
 */

#pragma once

#include <cstdint>

struct MeterReading {
	int64_t activePowerMw;
	int64_t rmsVoltageMv;
	int64_t rmsCurrentMa;
};

void MeterInit(void);

/* Called periodically (see kMeterPollInterval in meter_stub.cpp) to produce
 * the next synthetic reading and push it into the Matter clusters. */
void MeterPoll(void);
