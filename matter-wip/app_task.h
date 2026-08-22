#pragma once

#include <platform/CHIPDeviceLayer.h>

class AppTask {
public:
	static AppTask &Instance()
	{
		static AppTask sAppTask;
		return sAppTask;
	};

	CHIP_ERROR StartApp();

private:
	CHIP_ERROR Init();

	/* Drives the status LED from Matter stack events. */
	static void MatterEventHandler(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg);

	/* Recomputes the LED state from the current provisioning/commissioning
	 * status. Called after any event that could have changed it. */
	static void RefreshStatus();
};
