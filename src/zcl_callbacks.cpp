/*
 * Bridges the OnOff cluster to the physical relay: attribute writes (from a
 * controller, or from AppTask::UpdateClusterState() after a button press)
 * drive RelaySet(), and boot-time restore reads the persisted attribute back
 * so the relay's power-on state matches StartUpOnOff. See "Phase 2" in
 * docs/smart-plug-plan.md.
 */

#include "relay.h"
#include "status_led.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace ::chip;
using namespace ::chip::app::Clusters;

namespace
{
void ApplyOnOff(bool on)
{
	RelaySet(on);
	StatusLedSetRelayState(on);
}
} /* namespace */

void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath &attributePath, uint8_t /* type */,
				       uint16_t /* size */, uint8_t *value)
{
	if (attributePath.mClusterId == OnOff::Id && attributePath.mAttributeId == OnOff::Attributes::OnOff::Id) {
		ChipLogProgress(Zcl, "Cluster OnOff: attribute OnOff set to %" PRIu8, *value);
		ApplyOnOff(*value != 0);
	}
}

/* Runs once per endpoint, before the button/controller can write OnOff, and
 * after the persisted attribute has loaded -- this is where StartUpOnOff
 * takes effect for the physical relay. See the TODO on the upstream
 * declaration (samples/matter/light_bulb/src/zcl_callbacks.cpp) about this
 * firing before default-value initialization; reading back Attributes::OnOff
 * here rather than assuming "off" sidesteps that. */
void emberAfOnOffClusterInitCallback(EndpointId endpoint)
{
	bool storedValue = false;

	if (OnOff::Attributes::OnOff::Get(endpoint, &storedValue) == Protocols::InteractionModel::Status::Success) {
		ApplyOnOff(storedValue);
	}
}
