/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "relay.h"
#include "status_led.h"

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"
#include "clusters/identify.h"

#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr EndpointId kPlugEndpointId = 1;

Nrf::Matter::IdentifyCluster sIdentifyCluster(kPlugEndpointId);

/* The plug has a single tactile switch, wired to the board layer's function
 * button (DK_BTN1). Long press is consumed by Nrf::Board's FunctionHandler
 * for factory reset; the short press is ours to handle. */
#define APPLICATION_BUTTON_MASK DK_BTN1_MSK

#ifdef CONFIG_CHIP_ICD_UAT_SUPPORT
#define UAT_BUTTON_MASK DK_BTN3_MSK
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
		/* Released. TODO(phase 2): toggle the OnOff attribute on
		 * kPlugEndpointId here, so the relay change is reported to
		 * Matter rather than driving RelaySet() behind the cluster's
		 * back. A long press never reaches this point -- Nrf::Board's
		 * FunctionHandler consumes it for factory reset first. */
		LOG_INF("Plug button released (short press)");
#ifdef CONFIG_CHIP_ICD_UAT_SUPPORT
	} else if ((UAT_BUTTON_MASK & state & hasChanged)) {
		LOG_INF("ICD UserActiveMode has been triggered.");
		Server::GetInstance().GetICDManager().OnNetworkActivity();
#endif
	}
}

CHIP_ERROR AppTask::Init()
{
	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer(Nrf::Matter::InitData{ .mPostServerInitClbk = [] {
		// TODO(phase 2/4): relay and ElectricalPowerMeasurement/ElectricalEnergyMeasurement
		// init needs to go here, replacing the light-switch client's LightSwitch::Init().
		return CHIP_NO_ERROR;
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

	return Nrf::Matter::StartServer();
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
