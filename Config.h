#pragma once
#include <Arduino.h>

// LED 阴极接 GPIO，低电平点亮。
constexpr uint8_t LED1_PIN = 48;
constexpr uint8_t LED2_PIN = 47;
constexpr uint8_t LED3_PIN = 21;

// 按键接在 GPIO4 与 GND 之间，使用内部上拉输入。
constexpr uint8_t BUTTON_PIN = 4;

// 按键消抖时长，以及判定长按所需的持续时间。
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 2000;

// 可独立保存配对信息并切换的 BLE 主机槽位数。
constexpr uint8_t DEVICE_SLOT_COUNT = 3;

// 未连接时的 LED 闪烁周期，以及各槽位对应的广播名称。
constexpr uint32_t LED_BLINK_INTERVAL_MS = 500;
constexpr char BLE_DEVICE_NAMES[DEVICE_SLOT_COUNT][14] = {
    "ESP32-BLE-1",
    "ESP32-BLE-2",
    "ESP32-BLE-3"
};
// Preferences 中存储配对地址及地址类型的命名空间。
constexpr char BLE_PREFERENCES_NAMESPACE[] = "blekbd";
