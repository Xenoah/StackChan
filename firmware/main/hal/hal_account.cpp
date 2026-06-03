/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <settings.h>
#include <mooncake_log.h>

static const std::string_view _tag = "HAL-Account";

static const std::string_view _setting_ns              = "account";
static const std::string_view _setting_username_key    = "username";
static const std::string_view _setting_device_name_key = "device_name";

UserAccountInfo_t Hal::getUserAccountInfo()
{
    UserAccountInfo_t info;
    Settings settings(_setting_ns.data(), false);
    info.username   = settings.GetString(_setting_username_key.data(), "Local Mode");
    info.deviceName = settings.GetString(_setting_device_name_key.data(), getFactoryMacString());
    return info;
}

bool Hal::updateAccountInfo(std::function<void(std::string_view)> onLog)
{
    std::string username    = "Local Mode";
    std::string device_name = getFactoryMacString();

    Settings settings(_setting_ns.data(), true);
    settings.SetString(_setting_username_key.data(), username);
    settings.SetString(_setting_device_name_key.data(), device_name);

    mclog::tagInfo(_tag, "local account info set: username={}, device_name={}", username, device_name);
    if (onLog) {
        onLog("Local mode enabled");
    }
    return true;
}

bool Hal::unbindAccount(std::function<void(std::string_view)> onLog)
{
    Settings settings(_setting_ns.data(), true);
    settings.SetString(_setting_username_key.data(), "Local Mode");
    settings.SetString(_setting_device_name_key.data(), getFactoryMacString());
    mclog::tagInfo(_tag, "local mode reset requested");
    if (onLog) {
        onLog("Factory reset");
    }
    resetAppConfiged();

    return true;
}
