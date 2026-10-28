#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include "Config.h"
#include "USBManager.h"

class NimBLEHIDDevice;
class NimBLEServer;
class NimBLECharacteristic;

class BLEManager {
public:
    // 加载配对槽位并初始化 BLE HID 服务及广播。
    bool begin();
    // 在主循环中处理 BLE 回调交由其排队的状态变更。
    void update();
    // 将 USB 键盘报告编码为 BLE HID 输入报告并通知主机。
    void sendKeyboardReport(const KeyboardReport& report);
    // 切换广播槽位；若当前已连接，先请求断开连接。
    bool setActiveSlot(uint8_t slot);
    // 清除当前槽位的配对信息，连接中则先请求断开。
    void clearActivePairing();

    // 返回当前槽位（从 0 开始）及 BLE 连接状态。
    uint8_t activeSlot() const;
    bool isConnected() const;

private:
    friend class KeyboardBLEServerCallbacks;

    // BLE 回调中更新连接状态；实际需要的操作由 update() 执行。
    void handleConnect(
        const char* address,
        uint8_t addressType,
        uint16_t handle
    );
    void handleAuthenticatedPeer(
        const char* address,
        uint8_t addressType,
        bool bonded
    );
    void handleDisconnect();
    // 根据当前槽位的配对状态重建广播和控制器白名单。
    bool applySlotConfiguration();
    bool persistActiveSlot(uint8_t slot);
    bool requestDisconnect();
    void processPendingClear();
    // 保存认证成功的主机地址及其地址类型。
    void savePeer(uint8_t slot, const char* address, uint8_t addressType);
    bool forgetPeer(uint8_t slot);

    NimBLEServer* _server = nullptr;
    NimBLEHIDDevice* _hid = nullptr;
    NimBLECharacteristic* _inputReport = nullptr;

    // 每个槽位保存一个已配对主机的地址及地址类型。
    char _peerAddresses[DEVICE_SLOT_COUNT][18] = {};
    uint8_t _peerAddressTypes[DEVICE_SLOT_COUNT] = {};
    // 临界区保护回调任务与 Arduino 主循环共享的 BLE 状态。
    mutable portMUX_TYPE _stateMux = portMUX_INITIALIZER_UNLOCKED;
    char _pendingAddress[18] = {};
    uint8_t _pendingAddressType = 0;
    uint8_t _pendingAddressSlot = 0;
    // 记录连接建立时的槽位，保证配对数据写入正确槽位。
    uint8_t _connectionSlot = 0;
    uint8_t _activeSlot = 0;
    uint8_t _pendingSlot = 0;
    uint8_t _clearSlot = 0;
    uint16_t _connectionHandle = 0;
    uint16_t _rejectConnectionHandle = 0;
    bool _pendingPeerBonded = false;
    bool _connected = false;
    bool _advertisingRestartPending = false;
    bool _rejectConnectionPending = false;
    bool _addressPending = false;
    bool _disconnectRequestPending = false;
    bool _slotChangePending = false;
    bool _clearPending = false;
    bool _configurationRetryPending = false;
    bool _slotPersistencePending = false;
    bool _hidReadyLogged = false;
    uint8_t _disconnectAttempts = 0;
    uint32_t _lastDisconnectRequestMs = 0;
    uint32_t _lastConfigurationAttemptMs = 0;
    uint32_t _lastSlotPersistenceAttemptMs = 0;
};
