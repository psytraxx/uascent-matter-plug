#include "app_task.h"

#include "status_led.h"

#include "app/matter_init.h"
#include "app/task_executor.h"

#include "lib/core/CHIPError.h"
#include "lib/support/CodeUtils.h"

#include <app/server/Server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

void AppTask::RefreshStatus()
{
	/* Fully provisioned means we hold Thread network credentials and are on a
	 * fabric, which is the "paired" indication. Otherwise, if a commissioning
	 * window is open we are actively pairing. */
	if (ConfigurationMgr().IsFullyProvisioned()) {
		StatusLedSet(StatusLedState::Paired);
		return;
	}

	if (Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen()) {
		StatusLedSet(StatusLedState::Pairing);
		return;
	}

	/* Not provisioned and no way to commission: nothing useful can happen
	 * until the device is reset or a window is reopened. */
	StatusLedSet(StatusLedState::Error);
}

void AppTask::MatterEventHandler(const ChipDeviceEvent *event, intptr_t /* arg */)
{
	switch (event->Type) {
	case DeviceEventType::kCommissioningComplete:
		LOG_INF("Commissioning complete");
		break;
	case DeviceEventType::kFailSafeTimerExpired:
		LOG_WRN("Commissioning failed (fail-safe expired)");
		break;
	case DeviceEventType::kCHIPoBLEAdvertisingChange:
	case DeviceEventType::kWindowStatusChange:
	case DeviceEventType::kThreadConnectivityChange:
	case DeviceEventType::kServerReady:
		break;
	default:
		/* Not a state-changing event for our indication. */
		return;
	}

	RefreshStatus();
}

CHIP_ERROR AppTask::Init()
{
	const int ledErr = StatusLedInit();
	if (ledErr) {
		LOG_ERR("Status LED init failed (%d)", ledErr);
		return CHIP_ERROR_INCORRECT_STATE;
	}

	/* Any failure past this point leaves the red LED lit, since the LED layer
	 * starts in the Error state. */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer());
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(AppTask::MatterEventHandler, 0));
	ReturnErrorOnFailure(Nrf::Matter::StartServer());

	/* Log the onboarding QR code / manual pairing code to the console. */
	PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

	RefreshStatus();

	return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
	const CHIP_ERROR err = Init();
	if (err != CHIP_NO_ERROR) {
		StatusLedSet(StatusLedState::Error);
		return err;
	}

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
