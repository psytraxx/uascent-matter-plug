#include "meter_stub.h"

#include "power_measurement.h"
#include "relay.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

namespace
{

/* Mains-ish constants, chosen to look plausible on a controller UI, not to
 * model any real load. */
constexpr int64_t kNominalVoltageMv = 230'000;
constexpr int64_t kMinPowerMw = 5'000;
constexpr int64_t kMaxPowerMw = 60'000;
constexpr int64_t kPowerStepMw = 2'500;

int64_t sPowerMw = kMinPowerMw;

} /* namespace */

void MeterInit(void)
{
	LOG_INF("Meter stub active: synthetic readings, no BL0937 present");
}

void MeterPoll(void)
{
	if (!RelayIsOn()) {
		/* Mirrors the real driver's documented zero-power timeout
		 * behaviour (Phase 3): no load, no pulses, report a clean 0 W
		 * rather than holding a stale nonzero value. */
		PowerMeasurementUpdate(0, kNominalVoltageMv, 0);
		return;
	}

	sPowerMw += kPowerStepMw;
	if (sPowerMw > kMaxPowerMw) {
		sPowerMw = kMinPowerMw;
	}

	/* P = V * I, so I = P / V in consistent units (mW / mV -> A, *1000 for mA). */
	const int64_t currentMa = (sPowerMw * 1000) / kNominalVoltageMv;

	PowerMeasurementUpdate(sPowerMw, kNominalVoltageMv, currentMa);
}
