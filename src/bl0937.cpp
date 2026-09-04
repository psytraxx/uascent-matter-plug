/*
 * See bl0937.h and docs/smart-plug-plan.md's Phase 3 section for the design.
 *
 * Measurement strategy: CF and CF1 are pulse-frequency outputs (active power
 * and V/I respectively, the latter muxed by SEL). Pulses are counted via GPIO
 * edge interrupts and turned into a frequency once per poll window
 * (kMeterPollIntervalMs, currently 2s, set in app_task.cpp) rather than timing
 * individual periods -- far more robust at low power, where pulses can be
 * seconds apart.
 *
 * SEL alternates between voltage and current every two poll windows: the
 * first window after a flip settles (discarded), the second is read. So V
 * and I each update once per four poll windows -- see the SelPhase state
 * machine below.
 */

#include "bl0937.h"

#include "power_measurement.h"
#include "relay.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

namespace
{

/* Plain GPIO producer/consumer nodes, no driver binding -- see the overlay's
 * comment on why gpio-keys/gpio-leds bindings are reused for signals that are
 * neither. GPIO_ACTIVE_HIGH is encoded in devicetree, so logical values are
 * used throughout here. */
const gpio_dt_spec sCf = GPIO_DT_SPEC_GET(DT_NODELABEL(bl0937_cf), gpios);
const gpio_dt_spec sCf1 = GPIO_DT_SPEC_GET(DT_NODELABEL(bl0937_cf1), gpios);
const gpio_dt_spec sSel = GPIO_DT_SPEC_GET(DT_NODELABEL(bl0937_sel), gpios);

gpio_callback sCfCallback;
gpio_callback sCf1Callback;

/* Incremented from GPIO ISR context, read/reset from MeterPoll() on the app
 * task -- atomic rather than a lock, since the only operations needed are
 * "add one" and "swap out for zero". */
atomic_t sCfPulses;
atomic_t sCf1Pulses;

/* Calibration constants, recovered from the stock firmware's own flash
 * key-value store -- these are this exact unit's factory values, not generic
 * ballpark numbers. See docs/original-pcb-trace.md ("Calibration constants"):
 * the plaintext KV region at 0x1F8000 holds a "bl0937" key whose 12-byte
 * payload is three little-endian floats.
 *
 * They are stored here scaled by 1000 and kept as integers, because the
 * arithmetic below is all int64 fixed-point -- the *1000 in each formula
 * that used to convert W->mW now cancels against this scaling instead.
 *
 * Expressed as "counts per second per unit", matching the stock firmware's
 * own semantics: at 230 V CF1 runs ~1.86 kHz, and at 2300 W (this plug's 10 A
 * ceiling) CF runs ~1.78 kHz -- both comfortably inside the BL0937's range,
 * which is the cross-check that these are divisors and not multipliers.
 *
 * Still worth confirming against a reference meter at mains bring-up
 * (docs/smart-plug-plan.md, Phase 3): the recovered floats are certain, but
 * which one maps to voltage vs. current is inferred from BL0937 driver
 * convention and magnitude, not proven. If V and I read swapped, exchange
 * kMilliCountsPerSecPerVolt and kMilliCountsPerSecPerAmp. */
constexpr int64_t kMilliCountsPerSecPerWatt = 775;   /* 0.7752066 */
constexpr int64_t kMilliCountsPerSecPerVolt = 8077;  /* 8.0772724 */
constexpr int64_t kMilliCountsPerSecPerAmp = 91636;  /* 91.6363602 */

/* No pulses for this long on CF means no load, not "power dropped to a
 * value too low to produce a pulse in one window" -- see the plan's
 * zero-power-handling note. Several poll windows, so a genuinely light but
 * nonzero load isn't misreported as zero. */
constexpr uint32_t kZeroPowerTimeoutMs = 6'000;

/* Each phase holds for two poll windows: the first after a flip settles (and
 * is discarded), the second is read. So SEL flips once every two polls, not
 * every poll -- flipping every poll would mean every single window is a
 * settling window and none would ever be usable. */
enum class SelPhase {
	kVoltageSettling,
	kVoltageReady,
	kCurrentSettling,
	kCurrentReady,
};

SelPhase sSelPhase = SelPhase::kVoltageSettling;

int64_t sLastRmsVoltageMv;
int64_t sLastRmsCurrentMa;
int64_t sLastNonZeroCfMs;
int64_t sLastPollMs;

void CfIsr(const device *, gpio_callback *, uint32_t)
{
	atomic_inc(&sCfPulses);
}

void Cf1Isr(const device *, gpio_callback *, uint32_t)
{
	atomic_inc(&sCf1Pulses);
}

/* Swaps the given pulse counter out for zero and returns what it held,
 * atomically -- so a pulse arriving between the read and the reset is never
 * lost, unlike a plain read-then-clear. */
uint32_t TakePulses(atomic_t *counter)
{
	return static_cast<uint32_t>(atomic_set(counter, 0));
}

} /* namespace */

void MeterInit(void)
{
	LOG_INF("BL0937 driver active");

	for (const gpio_dt_spec *spec : { &sCf, &sCf1, &sSel }) {
		if (!gpio_is_ready_dt(spec)) {
			LOG_ERR("BL0937 GPIO port %s not ready", spec->port ? spec->port->name : "<null>");
			return;
		}
	}

	gpio_pin_configure_dt(&sCf, GPIO_INPUT);
	gpio_pin_configure_dt(&sCf1, GPIO_INPUT);
	/* SEL: LOW selects voltage, HIGH selects current on the BL0937 -- see
	 * the datasheet's SEL pin description. GPIO_OUTPUT_INACTIVE honours
	 * devicetree polarity, so this starts LOW (voltage) regardless of the
	 * overlay's active-level choice, matching sSelPhase's initial value
	 * of kVoltageSettling above. */
	gpio_pin_configure_dt(&sSel, GPIO_OUTPUT_INACTIVE);

	gpio_init_callback(&sCfCallback, CfIsr, BIT(sCf.pin));
	gpio_add_callback(sCf.port, &sCfCallback);
	gpio_pin_interrupt_configure_dt(&sCf, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_init_callback(&sCf1Callback, Cf1Isr, BIT(sCf1.pin));
	gpio_add_callback(sCf1.port, &sCf1Callback);
	gpio_pin_interrupt_configure_dt(&sCf1, GPIO_INT_EDGE_TO_ACTIVE);

	sLastNonZeroCfMs = k_uptime_get();
}

void MeterPoll(void)
{
	const int64_t nowMs = k_uptime_get();
	const int64_t windowMs = sLastPollMs ? (nowMs - sLastPollMs) : 0;
	sLastPollMs = nowMs;

	const uint32_t cfPulses = TakePulses(&sCfPulses);
	const uint32_t cf1Pulses = TakePulses(&sCf1Pulses);

	if (cfPulses > 0) {
		sLastNonZeroCfMs = nowMs;
	}

	int64_t activePowerMw = 0;
	if (windowMs > 0 && RelayIsOn() && nowMs - sLastNonZeroCfMs < kZeroPowerTimeoutMs) {
		/* counts/window * 1000 / windowMs = counts/s. Then counts/s / (milli-counts
		 * per W / 1000) = W, and *1000 again for mW -- so *1000000 over the
		 * milli-scaled constant in one step. */
		const int64_t countsPerSec = (static_cast<int64_t>(cfPulses) * 1000) / windowMs;
		activePowerMw = (countsPerSec * 1'000'000) / kMilliCountsPerSecPerWatt;
	}
	/* Else: relay open, or no load recently, or first call with nothing to
	 * compare against yet -- report a clean 0 W per the plan's zero-power
	 * handling, rather than holding a stale value. */

	/* The window that just elapsed measured whatever phase sSelPhase named
	 * *before* this call -- use it only if that phase was a *Ready state,
	 * meaning SEL had already been stable for one full prior window. */
	if (windowMs > 0 &&
	    (sSelPhase == SelPhase::kVoltageReady || sSelPhase == SelPhase::kCurrentReady)) {
		const int64_t cf1CountsPerSec = (static_cast<int64_t>(cf1Pulses) * 1000) / windowMs;

		if (sSelPhase == SelPhase::kVoltageReady) {
			sLastRmsVoltageMv = (cf1CountsPerSec * 1'000'000) / kMilliCountsPerSecPerVolt;
		} else {
			sLastRmsCurrentMa = (cf1CountsPerSec * 1'000'000) / kMilliCountsPerSecPerAmp;
		}
	}

	/* Advance to the next state for the window about to start. Only the
	 * two "Settling" transitions actually move the SEL pin; entering a
	 * "Ready" state leaves SEL exactly where the settling window already
	 * put it, one window ago. */
	switch (sSelPhase) {
	case SelPhase::kVoltageSettling:
		sSelPhase = SelPhase::kVoltageReady;
		break;
	case SelPhase::kVoltageReady:
		sSelPhase = SelPhase::kCurrentSettling;
		gpio_pin_set_dt(&sSel, 1);
		break;
	case SelPhase::kCurrentSettling:
		sSelPhase = SelPhase::kCurrentReady;
		break;
	case SelPhase::kCurrentReady:
		sSelPhase = SelPhase::kVoltageSettling;
		gpio_pin_set_dt(&sSel, 0);
		break;
	}

	PowerMeasurementUpdate(activePowerMw, sLastRmsVoltageMv, sLastRmsCurrentMa);
}
