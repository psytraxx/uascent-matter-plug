#include "power_measurement.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/AttributeAccessInterface.h>
#include <app/clusters/electrical-energy-measurement-server/ElectricalEnergyMeasurementCluster.h>
#include <app/clusters/electrical-energy-measurement-server/electrical-energy-measurement-server.h>
#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>
#include <app/reporting/reporting.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <lib/support/BitMask.h>
#include <platform/KeyValueStoreManager.h>

#include <zephyr/logging/log.h>

#include <memory>

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalPowerMeasurement;

namespace
{

/* EPM's Delegate. All the harmonic/ranges iterators return an empty list --
 * this plug measures neither -- and every scalar attribute the delegate does
 * not track (reactive/apparent power, frequency, power factor, ...) is left
 * out of the OptionalAttributes bitmask passed to Instance's constructor, so
 * the cluster never asks the delegate for those in the first place.
 * ActivePower/RMSVoltage/RMSCurrent are the only three PowerMeasurementUpdate()
 * actually writes. */
/* 3680 W / 16 A is a 230 V socket's ceiling; 300 V spans any mains this plug
 * could plausibly see. These bound what the cluster claims it can measure --
 * they are not the over-power trip point, which is APP_OVERPOWER_THRESHOLD_MW
 * and must match the plug's own rating. */
struct MeasurementSpec {
	MeasurementTypeEnum type;
	int64_t min;
	int64_t max;
	const Structs::MeasurementAccuracyRangeStruct::Type *ranges;
};

constexpr chip::Percent100ths kFivePercent = 500;

const Structs::MeasurementAccuracyRangeStruct::Type kPowerRange[] = {
	{ .rangeMin = 0, .rangeMax = 3'680'000, .percentMax = MakeOptional(kFivePercent) }
};
const Structs::MeasurementAccuracyRangeStruct::Type kVoltageRange[] = {
	{ .rangeMin = 0, .rangeMax = 300'000, .percentMax = MakeOptional(kFivePercent) }
};
const Structs::MeasurementAccuracyRangeStruct::Type kCurrentRange[] = {
	{ .rangeMin = 0, .rangeMax = 16'000, .percentMax = MakeOptional(kFivePercent) }
};

const MeasurementSpec kMeasurementSpecs[] = {
	{ MeasurementTypeEnum::kActivePower, 0, 3'680'000, kPowerRange },
	{ MeasurementTypeEnum::kRMSVoltage, 0, 300'000, kVoltageRange },
	{ MeasurementTypeEnum::kRMSCurrent, 0, 16'000, kCurrentRange },
};
constexpr uint8_t kMeasurementTypeCount = static_cast<uint8_t>(ARRAY_SIZE(kMeasurementSpecs));

class PlugPowerDelegate : public ElectricalPowerMeasurement::Delegate {
public:
	PowerModeEnum GetPowerMode() override { return PowerModeEnum::kAc; }
	uint8_t GetNumberOfMeasurementTypes() override { return kMeasurementTypeCount; }

	CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }

	/* The Accuracy attribute is a list with one entry per quantity the
	 * device actually measures. Declaring only ActivePower here -- as this
	 * did originally -- leaves RMSVoltage and RMSCurrent published but
	 * undeclared, which is not conformant and gives a controller no accuracy
	 * or range to describe them with. Some controllers use this list to
	 * decide which sensors to surface at all, so an omission here can show up
	 * as a missing entity rather than as a missing accuracy figure.
	 *
	 * Ranges are the plug's plausible span, not measured figures; the
	 * percentages are placeholders pending the calibration check in
	 * docs/smart-plug-plan.md's Phase 3. */
	CHIP_ERROR GetAccuracyByIndex(uint8_t index, Structs::MeasurementAccuracyStruct::Type &accuracy) override
	{
		if (index >= kMeasurementTypeCount) {
			return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
		}

		const MeasurementSpec &spec = kMeasurementSpecs[index];
		accuracy.measurementType = spec.type;
		accuracy.measured = true;
		accuracy.minMeasuredValue = spec.min;
		accuracy.maxMeasuredValue = spec.max;
		accuracy.accuracyRanges =
			DataModel::List<const Structs::MeasurementAccuracyRangeStruct::Type>(spec.ranges, 1);
		return CHIP_NO_ERROR;
	}
	CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }

	CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
	CHIP_ERROR GetRangeByIndex(uint8_t, Structs::MeasurementRangeStruct::Type &) override
	{
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }

	CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
	CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t, Structs::HarmonicMeasurementStruct::Type &) override
	{
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }

	CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
	CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t, Structs::HarmonicMeasurementStruct::Type &) override
	{
		return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
	}
	CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }

	/* Voltage is the instantaneous/DC attribute; an AC-only plug measures
	 * RMSVoltage instead, so this is never populated and the attribute is
	 * left out of the OptionalAttributes bitmask below -- the .matter IDL
	 * has no storage for it either. The override still has to exist: it is
	 * pure virtual on Delegate. */
	DataModel::Nullable<int64_t> GetVoltage() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetActiveCurrent() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetReactiveCurrent() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetApparentCurrent() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetActivePower() override { return mActivePowerMw; }
	DataModel::Nullable<int64_t> GetReactivePower() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetApparentPower() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetRMSVoltage() override { return mRmsVoltageMv; }
	DataModel::Nullable<int64_t> GetRMSCurrent() override { return mRmsCurrentMa; }
	DataModel::Nullable<int64_t> GetRMSPower() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetFrequency() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetPowerFactor() override { return DataModel::NullNullable; }
	DataModel::Nullable<int64_t> GetNeutralCurrent() override { return DataModel::NullNullable; }

	void Set(int64_t activePowerMw, int64_t rmsVoltageMv, int64_t rmsCurrentMa)
	{
		mActivePowerMw = activePowerMw;
		mRmsVoltageMv = rmsVoltageMv;
		mRmsCurrentMa = rmsCurrentMa;
	}

private:
	DataModel::Nullable<int64_t> mActivePowerMw;
	DataModel::Nullable<int64_t> mRmsVoltageMv;
	DataModel::Nullable<int64_t> mRmsCurrentMa;
};

std::unique_ptr<PlugPowerDelegate> sDelegate;
std::unique_ptr<Instance> sInstance;
std::unique_ptr<ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess> sEnergyAttrAccess;

EndpointId sEndpoint;
bool sHaveLastSample;
int64_t sLastActivePowerMw;
int64_t sCumulativeEnergyMwh;
int64_t sLastSampleMs;
int64_t sLastReportMs;

/* Cumulative energy is reported far less often than it is sampled: the poll
 * rate exists to keep EPM's power attributes responsive, while EEM only needs
 * enough resolution to track consumption over time. */
constexpr int64_t kEnergyReportIntervalMs = 60'000;

/* sCumulativeEnergyMwh is otherwise a RAM-only running total that resets to
 * zero on every reboot -- not acceptable for an attribute the spec expects
 * to be monotonic across the device's life. Persisted through the Matter
 * stack's own KeyValueStoreMgr(), which on this board is backed by Zephyr's
 * settings/NVS partition -- no separate storage handler is set up here.
 *
 * Writing on every energy report (kEnergyReportIntervalMs, 60 s) would be
 * ~1440 flash writes/day; NVS wear-levels but that is still worth bounding.
 * Instead write when the accumulated delta since the last write passes
 * kEnergyPersistThresholdMwh *and* at least kEnergyPersistMinIntervalMs has
 * elapsed, or unconditionally once kEnergyPersistMaxIntervalMs has elapsed
 * with any nonzero delta. The threshold+min-interval pair caps the worst
 * case (continuous full-rating draw) at a little over 2 writes/hour; the
 * max-interval floor bounds staleness for a plug drawing only a trickle.
 * Energy lost to a hard power cut is bounded by
 * max(kEnergyPersistThresholdMwh, kEnergyPersistMinIntervalMs at the
 * current power) -- this board has no brown-out hold-up budget to do
 * better than that. */
constexpr char kEnergyKvsKey[] = "uam-plug/cum-energy-mwh";
constexpr int64_t kEnergyPersistThresholdMwh = 100'000; /* 100 Wh */
constexpr int64_t kEnergyPersistMinIntervalMs = 600'000; /* 10 min */
constexpr int64_t kEnergyPersistMaxIntervalMs = 21'600'000; /* 6 h */

int64_t sLastPersistedEnergyMwh;
int64_t sLastPersistMs;

void LoadCumulativeEnergy()
{
	int64_t stored = 0;
	CHIP_ERROR err = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kEnergyKvsKey, &stored);
	if (err == CHIP_NO_ERROR && stored > 0) {
		sCumulativeEnergyMwh = stored;
	}
	/* CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND on first boot, or a
	 * negative/corrupt read, both leave sCumulativeEnergyMwh at its
	 * zero-initialised default -- monotonicity matters more here than
	 * salvaging a damaged value. */
	sLastPersistedEnergyMwh = sCumulativeEnergyMwh;
}

void PersistCumulativeEnergyIfDue(int64_t nowMs)
{
	const int64_t deltaMwh = sCumulativeEnergyMwh - sLastPersistedEnergyMwh;
	if (deltaMwh <= 0) {
		return;
	}

	const int64_t sincePersistMs = nowMs - sLastPersistMs;
	const bool thresholdDue =
		deltaMwh >= kEnergyPersistThresholdMwh && sincePersistMs >= kEnergyPersistMinIntervalMs;
	const bool maxIntervalDue = sincePersistMs >= kEnergyPersistMaxIntervalMs;
	if (!thresholdDue && !maxIntervalDue) {
		return;
	}

	CHIP_ERROR err =
		chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kEnergyKvsKey, sCumulativeEnergyMwh);
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Failed to persist cumulative energy: %" CHIP_ERROR_FORMAT, err.Format());
		return;
	}

	sLastPersistedEnergyMwh = sCumulativeEnergyMwh;
	sLastPersistMs = nowMs;
}

/* Storing a value in the delegate does not tell subscribers anything -- the
 * cluster only reads it when something marks the attribute dirty. Without
 * that, a subscriber learns about a jump from 0 W to 2000 W no sooner than
 * its own max interval, which can be tens of seconds.
 *
 * So report on change, gated by a deadband, which is what the stock firmware
 * did: it compared each reading against the last one it had sent and pushed
 * only when the difference exceeded a fixed threshold (docs/original-firmware.md,
 * "Energy accumulation and reporting"). Power and current mirror its 0.05 and
 * 0.003; the voltage figure has no stock counterpart. Which stock constant
 * belonged to which quantity is not fully pinned down, so treat all three as
 * tunable rather than recovered.
 *
 * These are far finer than the hardware resolves -- one CF pulse per second is
 * ~1.29 W -- so in practice every genuine change reports and the deadband only
 * suppresses arithmetic jitter. That is the intent: bound the traffic without
 * adding latency to real changes. */
constexpr int64_t kActivePowerDeadbandMw = 50;
constexpr int64_t kRmsVoltageDeadbandMv = 100;
constexpr int64_t kRmsCurrentDeadbandMa = 3;

bool sHaveReported;
int64_t sReportedActivePowerMw;
int64_t sReportedRmsVoltageMv;
int64_t sReportedRmsCurrentMa;

/* Marks one attribute dirty if it has moved past its deadband since the value
 * subscribers were last told about. The first call always reports, so the
 * initial reading is never held back by a deadband it has no baseline for. */
void ReportIfMoved(int64_t value, int64_t *reported, int64_t deadband, AttributeId attribute)
{
	const int64_t delta = value > *reported ? value - *reported : *reported - value;
	if (sHaveReported && delta < deadband) {
		return;
	}

	*reported = value;
	MatterReportingAttributeChangeCallback(sEndpoint, ElectricalPowerMeasurement::Id, attribute);
}

} /* namespace */

CHIP_ERROR PowerMeasurementInit(EndpointId endpoint)
{
	sEndpoint = endpoint;

	sDelegate = std::make_unique<PlugPowerDelegate>();
	sInstance = std::make_unique<Instance>(
		endpoint, *sDelegate, BitMask<Feature>(Feature::kAlternatingCurrent),
		BitMask<OptionalAttributes>(OptionalAttributes::kOptionalAttributeRMSVoltage,
					    OptionalAttributes::kOptionalAttributeRMSCurrent));

	CHIP_ERROR err = sInstance->Init();
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("EPM Instance init failed: %" CHIP_ERROR_FORMAT, err.Format());
		sInstance.reset();
		sDelegate.reset();
		return err;
	}

	/* EEM's AttributeAccessInterface is not self-registering: unlike EPM's
	 * Instance above, nothing in the SDK constructs or Init()s it for us.
	 * Skipping this compiles and boots fine -- the failure is silent until
	 * something reads CumulativeEnergyImported and gets Status::Failure from
	 * the weak emberAfExternalAttributeReadCallback stub
	 * (modules/lib/matter/src/app/util/generic-callback-stubs.cpp). Feature
	 * bits: only kImportedEnergy | kCumulativeEnergy (0x05) -- the endpoint
	 * neither exports nor reports periodically, and declaring kExportedEnergy
	 * or kPeriodicEnergy would make CumulativeEnergyExported /
	 * PeriodicEnergyImported mandatory attributes this endpoint doesn't have,
	 * which is a conformance failure, not just an unused feature bit. */
	sEnergyAttrAccess = std::make_unique<ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess>(
		BitMask<ElectricalEnergyMeasurement::Feature>(ElectricalEnergyMeasurement::Feature::kImportedEnergy,
							       ElectricalEnergyMeasurement::Feature::kCumulativeEnergy),
		BitMask<ElectricalEnergyMeasurement::OptionalAttributes>(0));

	err = sEnergyAttrAccess->Init();
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("EEM AttrAccess init failed: %" CHIP_ERROR_FORMAT, err.Format());
		sEnergyAttrAccess.reset();
		return err;
	}

	/* Accuracy is set once at init; readings arrive later purely through
	 * NotifyCumulativeEnergyMeasured(). measurementType is kElectricalEnergy
	 * here -- this is the *energy* cluster's accuracy entry, not power's, and
	 * min/maxMeasuredValue/rangeMax are an energy bound in mWh, not the
	 * 3'680'000 mW power ceiling EPM uses for the same numeral. A round
	 * 10-year figure at the plug's full rating (3.68 kW * 24 h * 365 d * 10)
	 * is used as a plausible span, same placeholder status as EPM's
	 * percentMax -- see the comment on kFivePercent above. */
	static const ElectricalEnergyMeasurement::Structs::MeasurementAccuracyRangeStruct::Type kEnergyRanges[] = { {
		.rangeMin = 0,
		.rangeMax = 322'368'000'000,
		.percentMax = MakeOptional(static_cast<chip::Percent100ths>(500)),
	} };
	ElectricalEnergyMeasurement::Structs::MeasurementAccuracyStruct::Type energyAccuracy;
	energyAccuracy.measurementType = MeasurementTypeEnum::kElectricalEnergy;
	energyAccuracy.measured = true;
	energyAccuracy.minMeasuredValue = 0;
	energyAccuracy.maxMeasuredValue = 322'368'000'000;
	energyAccuracy.accuracyRanges =
		DataModel::List<const ElectricalEnergyMeasurement::Structs::MeasurementAccuracyRangeStruct::Type>(
			kEnergyRanges);

	err = ElectricalEnergyMeasurement::SetMeasurementAccuracy(endpoint, energyAccuracy);
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("EEM accuracy set failed: %" CHIP_ERROR_FORMAT, err.Format());
		sEnergyAttrAccess.reset();
		return err;
	}

	LoadCumulativeEnergy();

	return CHIP_NO_ERROR;
}

void PowerMeasurementUpdate(int64_t activePowerMw, int64_t rmsVoltageMv, int64_t rmsCurrentMa)
{
	if (!sDelegate) {
		return;
	}

	sDelegate->Set(activePowerMw, rmsVoltageMv, rmsCurrentMa);

	ReportIfMoved(activePowerMw, &sReportedActivePowerMw, kActivePowerDeadbandMw,
		      Attributes::ActivePower::Id);
	ReportIfMoved(rmsVoltageMv, &sReportedRmsVoltageMv, kRmsVoltageDeadbandMv,
		      Attributes::RMSVoltage::Id);
	ReportIfMoved(rmsCurrentMa, &sReportedRmsCurrentMa, kRmsCurrentDeadbandMa,
		      Attributes::RMSCurrent::Id);
	sHaveReported = true;

	/* Trapezoidal energy integration over wall-clock time between calls.
	 * The first call has no prior sample to integrate from, so it only
	 * primes sLastActivePowerMw/sHaveLastSample and reports no energy --
	 * see power_measurement.h. */
	const int64_t nowMs = k_uptime_get();

	if (sHaveLastSample) {
		const int64_t elapsedMs = nowMs - sLastSampleMs;
		const int64_t avgPowerMw = (sLastActivePowerMw + activePowerMw) / 2;
		/* mW * ms / 3'600'000 = mWh. Fine for the plug's power range at
		 * this poll rate; would need wider intermediates for a much
		 * higher-power or much-less-frequently-polled design. */
		sCumulativeEnergyMwh += (avgPowerMw * elapsedMs) / 3'600'000;

		PersistCumulativeEnergyIfDue(nowMs);

		/* Accumulation above is unconditional so the running total stays
		 * correct, but the event is only worth generating when someone
		 * can receive it. Each Notify call writes an entry to the event
		 * buffer and bumps the persisted event number, so emitting while
		 * uncommissioned churns that buffer and wears NVS for events no
		 * controller will ever read -- and wakes a sleepy end device to
		 * do it. Rate-limit the rest: cumulative energy does not need the
		 * poll interval's resolution. */
		if (Server::GetInstance().GetFabricTable().FabricCount() > 0 &&
		    nowMs - sLastReportMs >= kEnergyReportIntervalMs) {
			ElectricalEnergyMeasurement::Structs::EnergyMeasurementStruct::Type imported;
			imported.energy = sCumulativeEnergyMwh;
			ElectricalEnergyMeasurement::NotifyCumulativeEnergyMeasured(
				sEndpoint, MakeOptional(imported), NullOptional);
			/* NotifyCumulativeEnergyMeasured() only logs the
			 * CumulativeEnergyMeasured event -- it does not mark the
			 * CumulativeEnergyImported attribute dirty (contrast
			 * SetMeasurementAccuracy(), which does call
			 * MatterReportingAttributeChangeCallback() itself). Without this,
			 * an attribute subscriber sees no report until its own max
			 * interval, the same gap f8299ba closed for EPM. */
			MatterReportingAttributeChangeCallback(
				sEndpoint, ElectricalEnergyMeasurement::Id,
				ElectricalEnergyMeasurement::Attributes::CumulativeEnergyImported::Id);
			sLastReportMs = nowMs;
		}
	}

	sLastActivePowerMw = activePowerMw;
	sLastSampleMs = nowMs;
	sHaveLastSample = true;
}
