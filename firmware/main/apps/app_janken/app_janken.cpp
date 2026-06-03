/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_janken.h"

#include <apps/common/common.h>
#include <assets/assets.h>
#include <esp_random.h>
#include <hal/board/hal_bridge.h>
#include <hal/hal.h>
#include <hal/local_control_server.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <smooth_lvgl.hpp>
#include <stackchan/avatar/avatar/elements/emotion.h>
#include <stackchan/stackchan.h>

#include <algorithm>
#include <cctype>
#include <exception>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;
using stackchan::avatar::Emotion;

namespace {

constexpr uint32_t kDetectIntervalMs = 900;
constexpr uint32_t kRockHoldMs = 3000;
constexpr uint32_t kResultHoldMs = 2800;
constexpr uint32_t kThemeColor = 0x64D6A7;
constexpr uint32_t kThemeDark = 0x17352B;

std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains_any(const std::string& text, const char* a, const char* b, const char* c)
{
    return text.find(a) != std::string::npos || text.find(b) != std::string::npos ||
           text.find(c) != std::string::npos;
}

}  // namespace

AppJanken::AppJanken()
{
    setAppInfo().name = "JANKEN";
    static auto icon = assets::get_image("icon_controller.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t theme_color = kThemeColor;
    setAppInfo().userData = (void*)&theme_color;
}

void AppJanken::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppJanken::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;

    auto default_avatar = std::make_unique<stackchan::avatar::DefaultAvatar>();
    default_avatar->init(lv_screen_active());
    GetStackChan().attachAvatar(std::move(default_avatar));

    _title = std::make_unique<Label>(lv_screen_active());
    _title->align(LV_ALIGN_TOP_MID, 0, 34);
    _title->setText("JANKEN");

    _status_label = std::make_unique<Label>(lv_screen_active());
    _status_label->align(LV_ALIGN_CENTER, 0, -4);
    _status_label->setLongMode(LV_LABEL_LONG_WRAP);
    _status_label->setSize(260, 58);

    _detail_label = std::make_unique<Label>(lv_screen_active());
    _detail_label->align(LV_ALIGN_CENTER, 0, 52);
    _detail_label->setLongMode(LV_LABEL_LONG_WRAP);
    _detail_label->setSize(280, 56);

    _quit_button = std::make_unique<Button>(lv_screen_active());
    _quit_button->setSize(84, 36);
    _quit_button->align(LV_ALIGN_BOTTOM_MID, 0, -12);
    _quit_button->label().setText("Back");
    _quit_button->onClick().connect([this]() { close(); });

    view::create_home_indicator([&]() { close(); }, kThemeColor, kThemeDark);
    view::create_status_bar(kThemeColor, kThemeDark);

    _phase = Phase::WaitingRock;
    _next_action_ms = 0;
    _rock_started_ms = 0;
    setUiMessage("Show rock for 3 sec", "Vision URL is set from local web UI.");
    updateUi();
}

void AppJanken::onRunning()
{
    runStateMachine();

    LvglLockGuard lock;
    updateUi();
    GetStackChan().update();
    view::update_home_indicator();
    view::update_status_bar();
}

void AppJanken::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;
    GetStackChan().resetAvatar();
    _quit_button.reset();
    _detail_label.reset();
    _status_label.reset();
    _title.reset();
    view::destroy_home_indicator();
    view::destroy_status_bar();
}

void AppJanken::runStateMachine()
{
    uint32_t now = GetHAL().millis();
    if (now < _next_action_ms) {
        return;
    }

    switch (_phase) {
        case Phase::WaitingRock: {
            Hand detected = classifyHand();
            _last_detected = detected;
            if (detected == Hand::Rock) {
                if (_rock_started_ms == 0) {
                    _rock_started_ms = now;
                }
                uint32_t held_ms = now - _rock_started_ms;
                if (held_ms >= kRockHoldMs) {
                    _phase = Phase::SayJanken;
                    _next_action_ms = now;
                    setUiMessage("Ready!", "Rock detected for 3 sec.");
                } else {
                    int remaining = static_cast<int>((kRockHoldMs - held_ms + 999) / 1000);
                    setUiMessage("Keep rock...", "Starting in " + std::to_string(remaining));
                    _next_action_ms = now + kDetectIntervalMs;
                }
            } else {
                _rock_started_ms = 0;
                setUiMessage("Show rock for 3 sec", "Detected: " + std::string(handName(detected)));
                _next_action_ms = now + kDetectIntervalMs;
            }
            break;
        }
        case Phase::SayJanken:
            setUiMessage("Jan ken...", "Get ready.");
            _phase = Phase::SayPon;
            _next_action_ms = now + 900;
            break;
        case Phase::SayPon: {
            setUiMessage("Pon!", "Taking photo...");
            _player_hand = classifyHand();
            _stackchan_hand = static_cast<Hand>((esp_random() % 3) + 1);
            _phase = Phase::ShowResult;
            _next_action_ms = now;
            break;
        }
        case Phase::ShowResult: {
            std::string result = judge(_player_hand, _stackchan_hand);
            setUiMessage(result, "You: " + std::string(handName(_player_hand)) +
                                     " / StackChan: " + std::string(handName(_stackchan_hand)));
            _phase = Phase::WaitingRock;
            _rock_started_ms = 0;
            _next_action_ms = now + kResultHoldMs;
            break;
        }
    }
}

AppJanken::Hand AppJanken::classifyHand()
{
    auto url = local_control::get_janken_vision_url();
    if (url.empty()) {
        setUiMessage("No vision URL", "Set Janken Vision URL from local web UI.");
        return Hand::None;
    }

    auto camera = hal_bridge::board_get_camera();
    if (camera == nullptr) {
        setUiMessage("No camera", "Camera is not available.");
        return Hand::None;
    }

    try {
        camera->SetExplainUrl(url, "");
        if (!camera->Capture()) {
            setUiMessage("Capture failed", "Could not take a photo.");
            return Hand::None;
        }

        std::string response = camera->Explain(
            "Classify the hand gesture in this image. Reply with only one lowercase word: rock, paper, scissors, or none.");
        std::string text = lower_ascii(response);

        if (contains_any(text, "scissors", "choki", "peace") || text.find("チョキ") != std::string::npos) {
            return Hand::Scissors;
        }
        if (contains_any(text, "paper", "paa", "open") || text.find("パー") != std::string::npos) {
            return Hand::Paper;
        }
        if (contains_any(text, "rock", "guu", "fist") || text.find("グー") != std::string::npos) {
            return Hand::Rock;
        }
    } catch (const std::exception& e) {
        setUiMessage("Vision error", e.what());
        return Hand::None;
    }

    return Hand::None;
}

void AppJanken::setUiMessage(const std::string& status, const std::string& detail)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _status = status;
    _detail = detail;
    _ui_dirty = true;
}

void AppJanken::updateUi()
{
    std::string status;
    std::string detail;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_ui_dirty) {
            return;
        }
        status = _status;
        detail = _detail;
        _ui_dirty = false;
    }

    if (_status_label) {
        _status_label->setText(status);
    }
    if (_detail_label) {
        _detail_label->setText(detail);
    }
    if (GetStackChan().hasAvatar()) {
        GetStackChan().avatar().setSpeech(status);
        GetStackChan().avatar().setEmotion(status == "You win!" ? Emotion::Sad : Emotion::Happy);
    }
}

const char* AppJanken::handName(Hand hand) const
{
    switch (hand) {
        case Hand::Rock:
            return "rock";
        case Hand::Scissors:
            return "scissors";
        case Hand::Paper:
            return "paper";
        case Hand::None:
        default:
            return "none";
    }
}

std::string AppJanken::judge(Hand player, Hand stackchan) const
{
    if (player == Hand::None) {
        return "Could not see your hand";
    }
    if (player == stackchan) {
        return "Draw!";
    }
    bool player_wins = (player == Hand::Rock && stackchan == Hand::Scissors) ||
                       (player == Hand::Scissors && stackchan == Hand::Paper) ||
                       (player == Hand::Paper && stackchan == Hand::Rock);
    return player_wins ? "You win!" : "StackChan wins!";
}
