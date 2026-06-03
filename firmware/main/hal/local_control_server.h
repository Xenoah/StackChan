/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string>

namespace local_control {

bool start();
void stop();
bool is_running();
std::string get_url();

}  // namespace local_control
