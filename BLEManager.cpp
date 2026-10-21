#include "BLEManager.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
BLEManager* activeManager = nullptr;
Preferences preferences;

// BLE HID 键盘描述符：8 个修饰键位、保留字节和 6 个普通键位。
uint8_t keyboardReportMap[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Left Control)
    0x29, 0xE7,       //   Usage Maximum (Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute): modifiers
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant): reserved byte
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x65,       //   Usage Maximum (101)
    0x81, 0x00,       //   Input (Data, Array, Absolute): keys
    0xC0              // End Collection
};
}

class KeyboardBLEServerCallbacks final : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer*, NimBLEConnInfo& connInfo) override {
        if (activeManager == nullptr) {
            return;
        }

        // 优先使用身份地址，使配对白名单与随机可解析地址保持一致。
        const NimBLEAddress idAddress = connInfo.getIdAddress();
        const NimBLEAddress peerAddress =
            idAddress.isNull() ? connInfo.getAddress() : idAddress;
        const std::string address = peerAddress.toString();
        activeManager->handleConnect(
            address.c_str(),
            peerAddress.getType(),
            connInfo.getConnHandle()
        );
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        if (activeManager == nullptr) {
            return;
        }

        const NimBLEAddress idAddress = connInfo.getIdAddress();
        const NimBLEAddress peerAddress =
            idAddress.isNull() ? connInfo.getAddress() : idAddress;
        const std::string address = peerAddress.toString();
        activeManager->handleAuthenticatedPeer(
            address.c_str(),
            peerAddress.getType(),
            connInfo.isBonded()
        );
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        if (activeManager != nullptr) {
            activeManager->handleDisconnect();
        }
    }
};

namespace {
KeyboardBLEServerCallbacks serverCallbacks;
}

bool BLEManager::begin() {
    activeManager = this;

    // 从非易失存储恢复各槽位的主机地址及地址类型。
    if (!preferences.begin(BLE_PREFERENCES_NAMESPACE, false)) {
        Serial.println("[BLE] Failed to open pairing storage");
        return false;
    }

    for (uint8_t slot = 0; slot < DEVICE_SLOT_COUNT; ++slot) {
        char addressKey[8];
        char typeKey[8];
        snprintf(addressKey, sizeof(addressKey), "peer%u", slot);
        snprintf(typeKey, sizeof(typeKey), "type%u", slot);
        const String address = preferences.getString(addressKey, "");
        if (address.length() > 0 && address.length() < sizeof(_peerAddresses[slot])) {
            address.toCharArray(_peerAddresses[slot], sizeof(_peerAddresses[slot]));
            _peerAddressTypes[slot] = preferences.getUChar(typeKey, BLE_ADDR_PUBLIC);
        }
    }
    preferences.end();

    if (!NimBLEDevice::init(BLE_DEVICE_NAMES[_activeSlot])) {
        Serial.println("[BLE] NimBLE initialization failed");
        return false;
    }

    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    _server = NimBLEDevice::createServer();
    if (_server == nullptr) {
        Serial.println("[BLE] Failed to create GATT server");
        return false;
    }
    _server->setCallbacks(&serverCallbacks);
    _server->advertiseOnDisconnect(false);

    // 创建 BLE HID 键盘服务，并设置主机识别设备所需的 HID 信息。
    _hid = new NimBLEHIDDevice(_server);
    _hid->setManufacturer("ESP32-S3");
    _hid->setPnp(0x02, 0x303A, 0x1001, 0x0100);
    _hid->setHidInfo(0, 1);
    _hid->setReportMap(keyboardReportMap, sizeof(keyboardReportMap));
    _server->start();
    _inputReport = _hid->getInputReport(1);

    applySlotConfiguration();
    Serial.println("[BLE] HID keyboard ready");
    return true;
}

void BLEManager::update() {
    // BLE 回调仅记录事件；存储、断开和重配广播统一在主循环中执行。
    if (_addressPending && _pendingPeerBonded) {
        _addressPending = false;
        savePeer(_pendingAddressSlot, _pendingAddress, _pendingAddressType);
        _pendingPeerBonded = false;
    } else if (_addressPending && !_connected) {
        _addressPending = false;
        _pendingPeerBonded = false;
    }

    if (_rejectConnectionPending) {
        _rejectConnectionPending = false;
        if (_server != nullptr) {
            _server->disconnect(_connectionHandle);
        }
    }

    if (_disconnectPending) {
        _disconnectPending = false;
        if (_clearPending) {
            processPendingClear();
        }
        _slotChangePending = true;
    }

    if (_clearPending && !_connected) {
        processPendingClear();
    }

    if (_slotChangePending && !_connected) {
        _slotChangePending = false;
        applySlotConfiguration();
    }
}

void BLEManager::sendKeyboardReport(const KeyboardReport& report) {
    if (!_connected || _inputReport == nullptr) {
        return;
    }

    // BLE HID 报告包含修饰键、一个保留字节和六个普通键位。
    const uint8_t hidReport[8] = {
        report.modifiers,
        0,
        report.keys[0],
        report.keys[1],
        report.keys[2],
        report.keys[3],
        report.keys[4],
        report.keys[5]
    };
    _inputReport->setValue(hidReport, sizeof(hidReport));
    _inputReport->notify();
}

void BLEManager::setActiveSlot(uint8_t slot) {
    if (slot >= DEVICE_SLOT_COUNT || slot == _activeSlot) {
        return;
    }

    // 切换槽位需要断开现有主机后才能修改设备名称和广播过滤规则。
    _activeSlot = slot;
    _slotChangePending = true;
    if (_connected && _server != nullptr) {
        if (!_server->disconnect(_connectionHandle)) {
            Serial.println("[BLE] Failed to request host disconnect");
        }
    } else {
        update();
    }
}

void BLEManager::clearActivePairing() {
    _clearPending = true;
    if (_connected && _server != nullptr) {
        if (!_server->disconnect(_connectionHandle)) {
            Serial.println("[BLE] Failed to disconnect before clearing pairing");
        }
    } else {
        update();
    }
}

uint8_t BLEManager::activeSlot() const {
    return _activeSlot;
}

bool BLEManager::isConnected() const {
    return _connected;
}

void BLEManager::handleConnect(
    const char* address,
    uint8_t addressType,
    uint16_t handle
) {
    // 已配对设备只能连接其所属槽位；拒绝连接到其他槽位的已知主机。
    for (uint8_t slot = 0; slot < DEVICE_SLOT_COUNT; ++slot) {
        if (_peerAddresses[slot][0] == '\0') {
            continue;
        }

        const NimBLEAddress savedAddress(
            std::string(_peerAddresses[slot]),
            _peerAddressTypes[slot]
        );
        const NimBLEAddress incomingAddress(std::string(address), addressType);
        if (savedAddress == incomingAddress) {
            if (slot == _activeSlot) {
                break;
            }
            _connectionHandle = handle;
            _rejectConnectionPending = true;
            return;
        }
    }

    _connectionHandle = handle;
    _connectionSlot = _activeSlot;
    _connected = true;
}

void BLEManager::handleAuthenticatedPeer(
    const char* address,
    uint8_t addressType,
    bool bonded
) {
    // 只保存已建立连接且完成绑定的主机，避免记录未配对设备。
    if (!bonded || !_connected) {
        return;
    }

    strncpy(_pendingAddress, address, sizeof(_pendingAddress) - 1);
    _pendingAddress[sizeof(_pendingAddress) - 1] = '\0';
    _pendingAddressType = addressType;
    _pendingAddressSlot = _connectionSlot;
    _pendingPeerBonded = true;
    _addressPending = true;
}

void BLEManager::handleDisconnect() {
    _connected = false;
    _disconnectPending = true;
}

void BLEManager::applySlotConfiguration() {
    if (_server == nullptr || _connected) {
        return;
    }

    // 重建广播配置，确保设备名称、HID 服务和当前槽位过滤规则一致。
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (!advertising->reset()) {
        Serial.println("[BLE] Failed to reset advertising configuration");
        return;
    }

    const char* deviceName = BLE_DEVICE_NAMES[_activeSlot];
    if (!NimBLEDevice::setDeviceName(deviceName) ||
        !advertising->setAppearance(HID_KEYBOARD) ||
        !advertising->addServiceUUID("1812") ||
        !advertising->setName(deviceName)) {
        Serial.printf(
            "[BLE] Failed to configure advertising for slot %u\n",
            static_cast<unsigned>(_activeSlot + 1)
        );
        return;
    }

    // 先清空上一个槽位留下的白名单，再按当前槽位重新添加配对主机。
    while (NimBLEDevice::getWhiteListCount() > 0) {
        const size_t lastIndex = NimBLEDevice::getWhiteListCount() - 1;
        if (!NimBLEDevice::whiteListRemove(
                NimBLEDevice::getWhiteListAddress(lastIndex)
            )) {
            Serial.println("[BLE] Failed to reset controller whitelist");
            return;
        }
    }

    const bool hasPeer = _peerAddresses[_activeSlot][0] != '\0';
    if (hasPeer) {
        const NimBLEAddress address(
            std::string(_peerAddresses[_activeSlot]),
            _peerAddressTypes[_activeSlot]
        );
        if (!NimBLEDevice::whiteListAdd(address)) {
            Serial.println("[BLE] Failed to whitelist selected paired host");
            return;
        }
    }

    // Empty slots accept a new host; populated slots only accept their bonded host.
    advertising->setScanFilter(false, hasPeer);
    if (!advertising->start()) {
        Serial.println("[BLE] Failed to start advertising");
        return;
    }

    Serial.printf(
        "[BLE] Advertising for slot %u (%s)\n",
        static_cast<unsigned>(_activeSlot + 1),
        hasPeer ? "paired host only" : "pairing available"
    );
}

void BLEManager::processPendingClear() {
    forgetActivePeer();
    _clearPending = false;
    _slotChangePending = true;
}

void BLEManager::savePeer(uint8_t slot, const char* address, uint8_t addressType) {
    if (slot >= DEVICE_SLOT_COUNT || address[0] == '\0') {
        return;
    }

    strncpy(_peerAddresses[slot], address, sizeof(_peerAddresses[slot]) - 1);
    _peerAddresses[slot][sizeof(_peerAddresses[slot]) - 1] = '\0';
    _peerAddressTypes[slot] = addressType;

    // 内存状态用于当前运行；Preferences 保存信息供下次启动恢复。
    if (!preferences.begin(BLE_PREFERENCES_NAMESPACE, false)) {
        Serial.println("[BLE] Failed to persist paired host");
        return;
    }

    char addressKey[8];
    char typeKey[8];
    snprintf(addressKey, sizeof(addressKey), "peer%u", slot);
    snprintf(typeKey, sizeof(typeKey), "type%u", slot);
    const size_t savedAddressLength =
        preferences.putString(addressKey, _peerAddresses[slot]);
    const size_t savedAddressType =
        preferences.putUChar(typeKey, addressType);
    preferences.end();
    if (savedAddressLength == 0 || savedAddressType == 0) {
        Serial.println("[BLE] Failed to persist complete pairing data");
    }
}

void BLEManager::forgetActivePeer() {
    // 同时删除 NimBLE 绑定和持久化槽位记录，避免旧主机仍可重连。
    const bool hadPeer = _peerAddresses[_activeSlot][0] != '\0';
    if (hadPeer) {
        const NimBLEAddress address(
            std::string(_peerAddresses[_activeSlot]),
            _peerAddressTypes[_activeSlot]
        );
        if (!NimBLEDevice::deleteBond(address)) {
            Serial.println("[BLE] Could not remove the selected host bond");
        }
    }

    _peerAddresses[_activeSlot][0] = '\0';
    _peerAddressTypes[_activeSlot] = BLE_ADDR_PUBLIC;

    if (!preferences.begin(BLE_PREFERENCES_NAMESPACE, false)) {
        Serial.println("[BLE] Failed to update pairing storage");
        return;
    }

    char addressKey[8];
    char typeKey[8];
    snprintf(addressKey, sizeof(addressKey), "peer%u", _activeSlot);
    snprintf(typeKey, sizeof(typeKey), "type%u", _activeSlot);
    bool removedAddress = true;
    bool removedType = true;
    if (hadPeer) {
        removedAddress = preferences.remove(addressKey);
        removedType = preferences.remove(typeKey);
    }
    preferences.end();
    if (!removedAddress || !removedType) {
        Serial.println("[BLE] Failed to remove pairing data from storage");
    }

    Serial.printf("[BLE] Pairing cleared for slot %u\n",
                  static_cast<unsigned>(_activeSlot + 1));
}
