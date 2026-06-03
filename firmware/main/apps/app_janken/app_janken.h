/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <memory>
#include <mutex>
#include <string>

namespace smooth_ui_toolkit::lvgl_cpp {
class Button;
class Label;
}  // namespace smooth_ui_toolkit::lvgl_cpp

class AppJanken : public mooncake::AppAbility {
public:
    AppJanken();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Phase {
        WaitingRock,
        SayJanken,
        SayPon,
        ShowResult,
    };

    enum class Hand {
        None,
        Rock,
        Scissors,
        Paper,
    };

    Hand classifyHand();
    void runStateMachine();
    void setUiMessage(const std::string& status, const std::string& detail);
    void updateUi();
    const char* handName(Hand hand) const;
    std::string judge(Hand player, Hand stackchan) const;

    Phase _phase = Phase::WaitingRock;
    Hand _last_detected = Hand::None;
    Hand _player_hand = Hand::None;
    Hand _stackchan_hand = Hand::None;
    uint32_t _next_action_ms = 0;
    uint32_t _rock_started_ms = 0;
    bool _ui_dirty = false;
    std::string _status;
    std::string _detail;
    std::mutex _mutex;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _detail_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _quit_button;
};
