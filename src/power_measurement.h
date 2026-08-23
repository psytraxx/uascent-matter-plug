/*
 * ElectricalPowerMeasurement (0x0090) and ElectricalEnergyMeasurement
 * (0x0091) server wiring for endpoint 1.
 *
 * EPM uses the Delegate/Instance pattern (a real object this app owns);
 * EEM uses free functions over a singleton the cluster server owns
 * internally, indexed by endpoint -- there is no delegate to write, just
 * SetMeasurementAccuracy()/NotifyCumulativeEnergyMeasured() calls. See
 * modules/lib/matter/src/app/clusters/electrical-energy-measurement-server/
 * ElectricalEnergyMeasurementCluster.h for that shape; it looks asymmetric
 * with EPM's Delegate because it is -- two different NCS-vendored
 * implementations, not a design choice made here.
 */

#pragma once

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

#include <cstdint>

CHIP_ERROR PowerMeasurementInit(chip::EndpointId endpoint);

/* Pushes one reading into both clusters: EPM's attributes directly (no
 * change-detection -- Matter's reporting engine handles that), and EEM's
 * cumulative energy by integrating activePowerMw over the elapsed time since
 * the previous call. First call after init only primes the integrator; it
 * reports zero elapsed energy. */
void PowerMeasurementUpdate(int64_t activePowerMw, int64_t rmsVoltageMv, int64_t rmsCurrentMa);
