#pragma once

#include <Arduino.h>
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
    void setActiveSlot(uint8_t slot);
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
    void applySlotConfiguration();
    void processPendingClear();
    // 保存认证成功的主机地址及其地址类型。
    void savePeer(uint8_t slot, const char* address, uint8_t addressType);
    void forgetActivePeer();

    NimBLEServer* _server = nullptr;
    NimBLEHIDDevice* _hid = nullptr;
    NimBLECharacteristic* _inputReport = nullptr;

    // 每个槽位保存一个已配对主机的地址及地址类型。
    char _peerAddresses[DEVICE_SLOT_COUNT][18] = {};
    uint8_t _peerAddressTypes[DEVICE_SLOT_COUNT] = {};
    char _pendingAddress[18] = {};
    uint8_t _pendingAddressType = 0;
    uint8_t _pendingAddressSlot = 0;
    // 记录连接建立时的槽位，保证配对数据写入正确槽位。
    uint8_t _connectionSlot = 0;
    // 回调与主循环共享的状态使用 volatile 标记，跨任务传递待处理事件。
    volatile bool _pendingPeerBonded = false;
    uint8_t _activeSlot = 0;
    volatile uint16_t _connectionHandle = 0;
    volatile bool _connected = false;
    volatile bool _rejectConnectionPending = false;
    volatile bool _addressPending = false;
    volatile bool _disconnectPending = false;
    bool _slotChangePending = false;
    bool _clearPending = false;
};
