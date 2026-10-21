#pragma once
#include <Arduino.h>
#include "Config.h"

class LEDManager {
public:
    // 配置各指示灯输出并关闭所有 LED。
    void begin();

    // 点亮当前槽位：已连接时常亮，等待连接时闪烁，其余槽位熄灭。
    void update(uint8_t slot, bool connected);

private:
    // LED 与 BLE 槽位按相同顺序一一对应。
    const uint8_t _pins[DEVICE_SLOT_COUNT] = {
        LED1_PIN, LED2_PIN, LED3_PIN
    };

    // 保存闪烁节拍及当前相位，避免使用阻塞式延时。
    uint32_t _lastBlinkMs = 0;
    bool _blinkOn = false;
};
