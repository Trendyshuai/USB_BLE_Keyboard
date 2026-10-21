#pragma once
#include <Arduino.h>
#include "Config.h"

enum ButtonEvent : uint8_t {
    BUTTON_NONE,
    BUTTON_SHORT_PRESS,
    BUTTON_LONG_PRESS
};

class ButtonManager {
public:
    // 初始化上拉输入，并以当前引脚电平建立去抖状态。
    void begin();
    // 轮询按键并返回本次检测到的事件；每次事件只报告一次。
    ButtonEvent update();

private:
    // 分别记录最近一次采样、确认后的稳定电平和长按是否已触发。
    bool _lastReading = HIGH;
    bool _stableState = HIGH;
    bool _longTriggered = false;

    // 使用 millis() 记录电平变化和按下时间，避免阻塞等待。
    uint32_t _lastChangeMs = 0;
    uint32_t _pressedAtMs = 0;
};
