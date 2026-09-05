/*
 * ElectricalPowerMeasurement (0x0090) and ElectricalEnergyMeasurement
 * (0x0091) server wiring for endpoint 1.
 *
 * Both clusters use an AttributeAccessInterface this app constructs and
 * registers itself: EPM's is the Delegate/Instance pattern, EEM's is
 * ElectricalEnergyMeasurementAttrAccess plus free functions
 * (SetMeasurementAccuracy()/NotifyCumulativeEnergyMeasured()) that reach it
 * through a singleton keyed by endpoint. Neither self-registers -- the SDK's
 * reference app (modules/lib/matter/examples/energy-management-app/
 * energy-management-common/common/src/EnergyManagementAppCommonMain.cpp)
 * constructs and Init()s the EEM AAI the same way PowerMeasurementInit()
 * does here. Skipping that step compiles and boots fine; it just leaves EEM
 * reads falling through to the SDK's weak emberAfExternalAttributeReadCallback
 * stub, which returns Status::Failure for every attribute the cluster claims
 * to serve externally.
 */

#pragma once

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

#include <cstdint>

CHIP_ERROR PowerMeasurementInit(chip::EndpointId endpoint);

/* Pushes one reading into both clusters: EPM's attributes through the
 * delegate, dirty-marked only when they move past a deadband (see
 * ReportIfMoved() in the .cpp); and EEM's cumulative energy by integrating
 * activePowerMw over the elapsed time since the previous call, also
 * dirty-marked on every call that reports. First call after init only primes
 * the integrator; it reports zero elapsed energy.
 *
 * The cumulative energy total is also periodically written to the KVS (see
 * PersistCumulativeEnergyIfDue() in the .cpp) so it survives a reboot; this
 * function's caller does not need to do anything for that to happen. */
void PowerMeasurementUpdate(int64_t activePowerMw, int64_t rmsVoltageMv, int64_t rmsCurrentMa);
