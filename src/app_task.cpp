/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "power_measurement.h"
#include "relay.h"
#include "status_led.h"

#if CONFIG_APP_METER_STUB
#include "meter_stub.h"
#endif

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"
#include "clusters/identify.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <lib/support/TypeTraits.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr EndpointId kPlugEndpointId = 1;
constexpr uint32_t kMeterPollIntervalMs = 2000;

Nrf::Matter::IdentifyCluster sIdentifyCluster(kPlugEndpointId);

/* The plug has a single tactile switch, wired to the board layer's function
 * button (DK_BTN1). Long press is consumed by Nrf::Board's FunctionHandler
 * for factory reset; the short press is ours to handle. */
#define APPLICATION_BUTTON_MASK DK_BTN1_MSK

#ifdef CONFIG_CHIP_ICD_UAT_SUPPORT
#define UAT_BUTTON_MASK DK_BTN3_MSK
#endif

#if CONFIG_APP_METER_STUB
k_timer sMeterPollTimer;

void MeterPollTimerCallback(k_timer *)
{
	Nrf::PostTask([] { MeterPoll(); });
}
#endif
} /* namespace */

/* Feeds the network axis of the LED indication; see src/status_led.h for the
 * full truth table and the relay axis. Registered via Board::Init(), which
 * calls it once at startup and again on every device state change. */
void AppTask::UpdateStatusLed()
{
	switch (Nrf::GetBoard().GetDeviceState()) {
	case Nrf::DeviceState::DeviceProvisioned:
		StatusLedSetNetworkState(NetworkLedState::Paired);
		break;
	case Nrf::DeviceState::DeviceAdvertisingBLE:
	case Nrf::DeviceState::DeviceConnectedBLE:
		StatusLedSetNetworkState(NetworkLedState::Pairing);
		break;
	case Nrf::DeviceState::DeviceDisconnected:
	default:
		StatusLedSetNetworkState(NetworkLedState::Error);
		break;
	}
}

void AppTask::ButtonEventHandler(Nrf::ButtonState state, Nrf::ButtonMask hasChanged)
{
	if (APPLICATION_BUTTON_MASK & hasChanged & ~state) {
		/* Released. Toggle the relay directly (this runs on the app
		 * task, not the Matter thread) and report the new state
		 * through the cluster so subscribers see it -- UpdateClusterState()
		 * marshals the attribute write onto the Matter thread. A long
		 * press never reaches this point -- Nrf::Board's FunctionHandler
		 * consumes it for factory reset first. */
		LOG_INF("Plug button released (short press)");
		RelaySet(!RelayIsOn());
		StatusLedSetRelayState(RelayIsOn());
		Instance().UpdateClusterState();
#ifdef CONFIG_CHIP_ICD_UAT_SUPPORT
	} else if ((UAT_BUTTON_MASK & state & hasChanged)) {
		LOG_INF("ICD UserActiveMode has been triggered.");
		Server::GetInstance().GetICDManager().OnNetworkActivity();
#endif
	}
}

/* Reports the relay's actuator state through the OnOff cluster. Called after
 * a button press changes the relay directly; MatterPostAttributeChangeCallback
 * (src/zcl_callbacks.cpp) is the reverse direction, for controller-initiated
 * writes. Attribute writes must run on the Matter thread, which is why this
 * marshals through SystemLayer() rather than writing from the button's own
 * task context. */
void AppTask::UpdateClusterState()
{
	SystemLayer().ScheduleLambda([] {
		const Protocols::InteractionModel::Status status =
			Clusters::OnOff::Attributes::OnOff::Set(kPlugEndpointId, RelayIsOn());
		if (status != Protocols::InteractionModel::Status::Success) {
			LOG_ERR("Updating OnOff cluster failed: %x", to_underlying(status));
		}
	});
}

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer(Nrf::Matter::InitData{ .mPostServerInitClbk = [] {
		/* Endpoint config is only valid once the data model has loaded,
		 * which is why this waits for the post-server-init hook rather
		 * than running alongside RelayInit()/StatusLedInit() below.
		 * OnOff -> RelaySet() bridging is src/zcl_callbacks.cpp. */
		return PowerMeasurementInit(kPlugEndpointId);
	} }));

	const int ledErr = StatusLedInit();
	if (ledErr) {
		LOG_ERR("Status LED init failed (%d)", ledErr);
		return CHIP_ERROR_INCORRECT_STATE;
	}

	/* Before the Matter stack starts, so the load is guaranteed off until
	 * something explicitly switches it on. */
	const int relayErr = RelayInit();
	if (relayErr) {
		LOG_ERR("Relay init failed (%d)", relayErr);
		return CHIP_ERROR_INCORRECT_STATE;
	}

	if (!Nrf::GetBoard().Init(ButtonEventHandler, UpdateStatusLed)) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}

	/* Register Matter event handler that controls the connectivity status LED based on the captured Matter network
	 * state. */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));

	ReturnErrorOnFailure(sIdentifyCluster.Init());

	const CHIP_ERROR startErr = Nrf::Matter::StartServer();
	ReturnErrorOnFailure(startErr);

#if CONFIG_APP_METER_STUB
	MeterInit();
	k_timer_init(&sMeterPollTimer, MeterPollTimerCallback, nullptr);
	k_timer_start(&sMeterPollTimer, K_MSEC(kMeterPollIntervalMs), K_MSEC(kMeterPollIntervalMs));
#endif

	return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
