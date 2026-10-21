#include "USBManager.h"

#include <USBHost.h>
#include <USBHostHIDKeyboard.h>

// 先让 USB HID 基类认领接口，再保存设备标识以匹配后续卸载事件。
bool USBManager::Keyboard::claim(
    uint8_t devAddr,
    uint8_t interfaceIndex,
    uint8_t protocol,
    const uint8_t* reportDescriptor,
    uint16_t descriptorLength
) {
    if (!USBHostHIDKeyboard::claim(
            devAddr, interfaceIndex, protocol,
            reportDescriptor, descriptorLength
        )) {
        return false;
    }

    _devAddr = devAddr;
    _interfaceIndex = interfaceIndex;
    _mounted = true;
    return true;
}

// 仅处理当前实例认领的接口；卸载标记由主机任务循环消费。
void USBManager::Keyboard::onUnmount(uint8_t devAddr, uint8_t interfaceIndex) {
    if (_mounted && devAddr == _devAddr && interfaceIndex == _interfaceIndex) {
        USBHostHIDKeyboard::onUnmount(devAddr, interfaceIndex);
        _mounted = false;
        _unmounted = true;
    }
}

// 消费一次卸载通知，避免同一拔出事件被重复处理。
bool USBManager::Keyboard::consumeUnmount() {
    const bool unmounted = _unmounted;
    _unmounted = false;
    return unmounted;
}

// 注册键盘驱动并启动 USB 主机栈。
bool USBManager::begin() {
    _keyboard.registerWithHost();
    _keyboard.setNotifyOnChangeOnly(true);

    _started = USBHost.begin();

    if (_started) {
        Serial.println("[USB] Host started");
    } else {
        Serial.println("[USB] Host start failed");
    }

    return _started;
}

// USB 主机库要求周期性处理任务；此处也收集设备拔出通知。
void USBManager::task() {
    if (_started) {
        USBHost.task();
        if (_keyboard.consumeUnmount()) {
            _releasePending = true;
        }
    }
}

// 优先发出拔出后的全释放报告，否则读取并清除一份新键盘报告。
bool USBManager::readReport(KeyboardReport& report) {
    if (_releasePending) {
        report.modifiers = 0;
        for (uint8_t& key : report.keys) {
            key = 0;
        }
        _releasePending = false;
        return true;
    }

    if (_started && _keyboard.available()) {
        report.modifiers = _keyboard.getModifiers();
        _keyboard.getKeys(report.keys);
        _keyboard.clear();
        return true;
    }

    return false;
}
