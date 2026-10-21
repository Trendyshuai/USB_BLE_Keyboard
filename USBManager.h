#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <USBHostHIDKeyboard.h>

struct KeyboardReport {
    // Boot 键盘报告中的修饰键位图及最多六个同时按下的普通键。
    uint8_t modifiers;
    uint8_t keys[6];
};

class USBManager {
public:
    // 注册键盘驱动并启动 USB 主机。
    bool begin();
    // 轮询 USB 主机事件，并记录键盘拔出状态。
    void task();

    // 有新报告或待发送的全释放报告时返回 true。
    bool readReport(KeyboardReport& report);
    // 复制最近一次键盘状态；从未收到报告时返回全释放状态。
    void currentReport(KeyboardReport& report) const;

private:
    class Keyboard final : public USBHostHIDKeyboard {
    public:
        // 仅在基类确认支持该设备接口后记录挂载信息。
        bool claim(uint8_t devAddr, uint8_t interfaceIndex, uint8_t protocol,
                   const uint8_t* reportDescriptor, uint16_t descriptorLength) override;
        // 匹配当前键盘的卸载事件，并留待主循环处理。
        void onUnmount(uint8_t devAddr, uint8_t interfaceIndex) override;
        // 读取并清除一次性卸载标记。
        bool consumeUnmount();

    private:
        // 用设备地址和接口号识别本实例所管理的键盘。
        portMUX_TYPE _stateMux = portMUX_INITIALIZER_UNLOCKED;
        uint8_t _devAddr = 0;
        uint8_t _interfaceIndex = 0;
        bool _mounted = false;
        bool _unmounted = false;
    };

    Keyboard _keyboard;
    bool _started = false;
    // 拔出键盘后先发送全释放报告，避免主机保留卡住的按键状态。
    bool _releasePending = false;
    KeyboardReport _currentReport = {};
};
