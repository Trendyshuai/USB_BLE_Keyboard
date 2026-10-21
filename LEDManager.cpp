#include "LEDManager.h"

// LED 为低电平有效，初始化时设置为高电平熄灭。
void LEDManager::begin() {
    for (uint8_t pin : _pins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH); // Active-low LED: HIGH = OFF.
    }
}

// 只显示选中槽位的状态；对越界槽位回退到第一个槽位。
void LEDManager::update(uint8_t slot, bool connected) {
    if (slot >= DEVICE_SLOT_COUNT) {
        slot = 0;
    }

    const uint32_t now = millis();

    if (connected) {
        _blinkOn = true;
    } else if ((uint32_t)(now - _lastBlinkMs) >= LED_BLINK_INTERVAL_MS) {
        _lastBlinkMs = now;
        _blinkOn = !_blinkOn;
    }

    for (uint8_t i = 0; i < DEVICE_SLOT_COUNT; ++i) {
        const bool on = (i == slot) && (connected || _blinkOn);
        digitalWrite(_pins[i], on ? LOW : HIGH);
    }
}
