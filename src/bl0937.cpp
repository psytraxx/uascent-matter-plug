/*
 * See bl0937.h and docs/smart-plug-plan.md's Phase 3 section for the design.
 *
 * Measurement strategy: CF and CF1 are pulse-frequency outputs (active power
 * and V/I respectively, the latter muxed by SEL). Pulses are counted via GPIO
 * edge interrupts and turned into a frequency once per second rather than
 * timing individual periods -- far more robust at low power, where pulses can
 * be seconds apart.
 *
 * The sampling scheme here is a deliberate reimplementation of what the stock
 * Uascent firmware did, recovered by disassembly -- see
 * docs/original-firmware.md ("Metering pipeline") for the addresses and
 * evidence behind each constant. Matching it matters because the calibration
 * divisors below were lifted from that firmware's own NV store, and they only
 * mean what they say when fed the same way: a 1 Hz sample rate, a median of
 * three, and a 3 s SEL dwell.
 */

#include "bl0937.h"

#include "power_measurement.h"
#ifdef CONFIG_APP_OVERPOWER_PROTECTION
#include "app_task.h"
#include "relay.h"
#endif

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
 * key-value store -- this exact unit's factory values, not generic ballpark
 * numbers. The plaintext KV region at 0x1F8000 holds a "bl0937" key whose
 * 12-byte payload is three little-endian floats.
 *
 * The disassembly also settles which float is which: the stock metering tick
 * divides CF by the third and CF1 by the first or second depending on SEL
 * (docs/original-firmware.md, "Scaling"). So these are divisors -- "counts
 * per second per unit" -- and the V/I assignment is proven, not inferred.
 *
 * Stored scaled by 1000 as integers because the arithmetic here is int64
 * fixed-point; the *1000 that converts W to mW cancels against that scaling,
 * hence the *1'000'000 in one step at each use site. */
constexpr int64_t kMilliCountsPerSecPerWatt = 775;   /* 0.7752066 */
constexpr int64_t kMilliCountsPerSecPerVolt = 8077;  /* 8.0772724 */
constexpr int64_t kMilliCountsPerSecPerAmp = 91636;  /* 91.6363602 */

/* Samples per filter window. The stock firmware collects three, sorts them and
 * takes the middle one. That median is also what makes a separate SEL settling
 * window unnecessary: the one sample straddling a SEL flip is a transient
 * outlier, and a median of three discards outliers by construction. Changing
 * this to an even number would mean averaging two middle samples and would
 * reintroduce the settling error. */
constexpr size_t kFilterDepth = 3;

/* SEL holds for one full filter window -- 3 s at the 1 Hz poll rate, matching
 * the stock firmware's 3000 ms dwell. Since the two quantities take turns,
 * each of V and I refreshes every 6 s; active power, which is not muxed,
 * refreshes every 3 s. */
bool sSelIsVoltage = true;

int64_t sLastActivePowerMw;
int64_t sLastRmsVoltageMv;
int64_t sLastRmsCurrentMa;
int64_t sLastPollMs;

/* Median-of-three over a channel's counts-per-second samples. Push() returns
 * true exactly once per kFilterDepth calls, when a window completes. */
class MedianFilter {
public:
	bool Push(uint32_t sample, uint32_t *median)
	{
		mSamples[mCount++] = sample;
		if (mCount < kFilterDepth) {
			return false;
		}
		mCount = 0;

		/* Sorting network for three elements -- the stock firmware
		 * bubble-sorts and indexes the middle; same result, no loop. */
		uint32_t a = mSamples[0], b = mSamples[1], c = mSamples[2];
		if (a > b) {
			const uint32_t t = a; a = b; b = t;
		}
		if (b > c) {
			const uint32_t t = b; b = c; c = t;
		}
		if (a > b) {
			const uint32_t t = a; a = b; b = t;
		}
		*median = b;
		return true;
	}

private:
	uint32_t mSamples[kFilterDepth];
	size_t mCount;
};

MedianFilter sCfFilter;
MedianFilter sCf1Filter;

#ifdef CONFIG_APP_OVERPOWER_PROTECTION
/* Consecutive samples seen above the trip threshold. Reset by any sample at or
 * below it, so only a *sustained* overload counts. */
uint32_t sOverPowerSamples;

/* Checked against the raw per-second sample rather than the median, matching
 * where the stock firmware placed it: the median deliberately lags by three
 * seconds, and protection should not.
 *
 * There is no latch. A trip opens the relay and nothing more, so turning the
 * plug back on is allowed and simply re-arms the check -- a persistent
 * overload trips again after APP_OVERPOWER_SAMPLES seconds. That matches the
 * stock firmware, and avoids a latch's own failure mode of leaving the plug
 * stuck off with no obvious way to clear it. The cost is that a controller
 * automation which blindly re-enables the plug could cycle the relay; a latch
 * would be the answer if that ever shows up in practice. */
void CheckOverPower(int64_t instantPowerMw)
{
	if (instantPowerMw <= CONFIG_APP_OVERPOWER_THRESHOLD_MW) {
		sOverPowerSamples = 0;
		return;
	}

	if (++sOverPowerSamples < CONFIG_APP_OVERPOWER_SAMPLES) {
		return;
	}

	sOverPowerSamples = 0;

	if (!RelayIsOn()) {
		return;
	}

	LOG_ERR("Over-power: %lld mW above %d mW for %d s -- opening relay",
		instantPowerMw, CONFIG_APP_OVERPOWER_THRESHOLD_MW, CONFIG_APP_OVERPOWER_SAMPLES);

	/* Same three steps the button path takes, so the relay, the plug LED and
	 * the OnOff attribute cannot disagree about what happened.
	 * UpdateClusterState() marshals the attribute write onto the Matter
	 * thread; this runs on the app task. */
	RelaySet(false);
	AppTask::Instance().UpdateClusterState();
}
#endif /* CONFIG_APP_OVERPOWER_PROTECTION */

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

/* Normalise a window's raw pulse count to counts per second. The stock
 * firmware assumes its 1 s tick is exactly 1 s and uses the raw count; doing
 * the division against the measured window makes the reading immune to timer
 * jitter and is identical whenever the timer is on time. */
uint32_t CountsPerSec(uint32_t pulses, int64_t windowMs)
{
	return static_cast<uint32_t>((static_cast<int64_t>(pulses) * 1000) / windowMs);
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

	/* SEL HIGH selects voltage, LOW selects current. This is the opposite
	 * of the HLW8012 the BL0937 is otherwise pin-compatible with, and the
	 * stock firmware is unambiguous about it: its metering tick sends
	 * SEL == 0 to the current divisor and SEL != 0 to the voltage divisor,
	 * and its .data image boots SEL to 1. See docs/original-firmware.md,
	 * "SEL multiplexing -- polarity".
	 *
	 * GPIO_OUTPUT_ACTIVE honours devicetree polarity, so this starts in the
	 * voltage phase regardless of the overlay's active-level choice,
	 * matching sSelIsVoltage above. */
	gpio_pin_configure_dt(&sSel, GPIO_OUTPUT_ACTIVE);

	gpio_init_callback(&sCfCallback, CfIsr, BIT(sCf.pin));
	gpio_add_callback(sCf.port, &sCfCallback);
	gpio_pin_interrupt_configure_dt(&sCf, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_init_callback(&sCf1Callback, Cf1Isr, BIT(sCf1.pin));
	gpio_add_callback(sCf1.port, &sCf1Callback);
	gpio_pin_interrupt_configure_dt(&sCf1, GPIO_INT_EDGE_TO_ACTIVE);
}

void MeterPoll(void)
{
	const int64_t nowMs = k_uptime_get();
	const int64_t windowMs = sLastPollMs ? (nowMs - sLastPollMs) : 0;
	sLastPollMs = nowMs;

	const uint32_t cfPulses = TakePulses(&sCfPulses);
	const uint32_t cf1Pulses = TakePulses(&sCf1Pulses);

	/* First call has no elapsed window to normalise against; it only starts
	 * the clock. Discard its counts rather than feeding a bogus rate into
	 * the filters. */
	if (windowMs <= 0) {
		return;
	}

	uint32_t median;
	const uint32_t cfCountsPerSec = CountsPerSec(cfPulses, windowMs);

#ifdef CONFIG_APP_OVERPOWER_PROTECTION
	CheckOverPower((static_cast<int64_t>(cfCountsPerSec) * 1'000'000) / kMilliCountsPerSecPerWatt);
#endif

	/* Active power is measured continuously -- CF is not muxed. */
	if (sCfFilter.Push(cfCountsPerSec, &median)) {
		sLastActivePowerMw = (static_cast<int64_t>(median) * 1'000'000) / kMilliCountsPerSecPerWatt;
	}

	/* SEL is flipped only here, when a CF1 window completes, so the level
	 * was constant across all kFilterDepth samples that produced this
	 * median and sSelIsVoltage still names it. */
	if (sCf1Filter.Push(CountsPerSec(cf1Pulses, windowMs), &median)) {
		if (sSelIsVoltage) {
			sLastRmsVoltageMv = (static_cast<int64_t>(median) * 1'000'000) / kMilliCountsPerSecPerVolt;
		} else {
			sLastRmsCurrentMa = (static_cast<int64_t>(median) * 1'000'000) / kMilliCountsPerSecPerAmp;
		}

		sSelIsVoltage = !sSelIsVoltage;
		gpio_pin_set_dt(&sSel, sSelIsVoltage ? 1 : 0);
	}

	PowerMeasurementUpdate(sLastActivePowerMw, sLastRmsVoltageMv, sLastRmsCurrentMa);
}
