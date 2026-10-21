#include "ButtonManager.h"

// 初始化按键状态，避免启动时把已按下的按键误判为新事件。
void ButtonManager::begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    _lastReading = digitalRead(BUTTON_PIN);
    _stableState = _lastReading;
    _lastChangeMs = millis();
    _pressedAtMs = (_stableState == LOW) ? _lastChangeMs : 0;
    _longTriggered = false;
}

// 以稳定电平过滤抖动；长按在按住期间触发，短按在释放时触发。
ButtonEvent ButtonManager::update() {
    const uint32_t now = millis();
    const bool reading = digitalRead(BUTTON_PIN);

    if (reading != _lastReading) {
        _lastReading = reading;
        _lastChangeMs = now;
    }

    // 输入持续稳定达到消抖时长后，才接受新的按下或释放状态。
    if ((uint32_t)(now - _lastChangeMs) >= BUTTON_DEBOUNCE_MS &&
        reading != _stableState) {
        _stableState = reading;

        if (_stableState == LOW) {
            // 按下时开始计时，并允许本次按压触发长按事件。
            _pressedAtMs = now;
            _longTriggered = false;
        } else {
            // 仅未触发长按的按压，才在释放时报告为短按。
            if (!_longTriggered) {
                return BUTTON_SHORT_PRESS;
            }
        }
    }

    // 按住达到阈值时立即报告长按，且同一次按压不重复报告。
    if (_stableState == LOW &&
        !_longTriggered &&
        (uint32_t)(now - _pressedAtMs) >= BUTTON_LONG_PRESS_MS) {
        _longTriggered = true;
        return BUTTON_LONG_PRESS;
    }

    return BUTTON_NONE;
}
