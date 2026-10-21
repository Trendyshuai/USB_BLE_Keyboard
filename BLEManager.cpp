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
constexpr uint32_t DISCONNECT_RETRY_MS = 1000;
constexpr uint32_t CONFIGURATION_RETRY_MS = 1000;
constexpr uint8_t MAX_DISCONNECT_ATTEMPTS = 3;

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
    const uint8_t savedSlot = preferences.getUChar("slot", 0);
    if (savedSlot < DEVICE_SLOT_COUNT) {
        _activeSlot = savedSlot;
    } else {
        Serial.println("[BLE] Saved slot is invalid; using slot 1");
    }
    _pendingSlot = _activeSlot;
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
    if (_inputReport == nullptr) {
        Serial.println("[BLE] Failed to create HID input report");
        return false;
    }

    if (!applySlotConfiguration()) {
        _configurationRetryPending = true;
        _lastConfigurationAttemptMs = millis();
        return false;
    }
    Serial.println("[BLE] HID keyboard ready");
    _hidReadyLogged = true;
    return true;
}

void BLEManager::update() {
    // BLE 回调仅记录事件；存储、断开和重配广播统一在主循环中执行。
    bool saveAddress = false;
    char address[sizeof(_pendingAddress)] = {};
    uint8_t addressType = 0;
    uint8_t addressSlot = 0;
    bool rejectConnection = false;
    uint16_t rejectedHandle = 0;
    bool connected = false;
    bool disconnectRequestPending = false;
    uint8_t disconnectAttempts = 0;

    portENTER_CRITICAL(&_stateMux);
    if (_addressPending && _pendingPeerBonded) {
        memcpy(address, _pendingAddress, sizeof(address));
        addressType = _pendingAddressType;
        addressSlot = _pendingAddressSlot;
        saveAddress = true;
        _addressPending = false;
        _pendingPeerBonded = false;
    } else if (_addressPending && !_connected) {
        _addressPending = false;
        _pendingPeerBonded = false;
    }

    if (_rejectConnectionPending) {
        _rejectConnectionPending = false;
        rejectConnection = true;
        rejectedHandle = _rejectConnectionHandle;
    }

    connected = _connected;
    disconnectRequestPending = _disconnectRequestPending;
    disconnectAttempts = _disconnectAttempts;
    portEXIT_CRITICAL(&_stateMux);

    if (saveAddress) {
        savePeer(addressSlot, address, addressType);
    }

    if (rejectConnection && _server != nullptr) {
        _server->disconnect(rejectedHandle);
    }

    if (_clearPending && !connected) {
        processPendingClear();
    }

    if (_slotChangePending && !connected) {
        portENTER_CRITICAL(&_stateMux);
        _activeSlot = _pendingSlot;
        const uint8_t slotToSave = _activeSlot;
        portEXIT_CRITICAL(&_stateMux);
        _slotPersistencePending = !persistActiveSlot(slotToSave);
        _lastSlotPersistenceAttemptMs = millis();

        _slotChangePending = false;
        if (applySlotConfiguration()) {
            _configurationRetryPending = false;
        } else {
            _configurationRetryPending = true;
            _lastConfigurationAttemptMs = millis();
        }
    } else if ((_slotChangePending || _clearPending) && connected &&
               disconnectRequestPending &&
               (uint32_t)(millis() - _lastDisconnectRequestMs) >=
                   DISCONNECT_RETRY_MS) {
        if (disconnectAttempts >= MAX_DISCONNECT_ATTEMPTS) {
            portENTER_CRITICAL(&_stateMux);
            _disconnectRequestPending = false;
            _disconnectAttempts = 0;
            portEXIT_CRITICAL(&_stateMux);
            _slotChangePending = false;
            _clearPending = false;
            Serial.println("[BLE] Disconnect timed out; pending operation cancelled");
        } else if (!requestDisconnect()) {
            Serial.println("[BLE] Disconnect request failed; will retry");
        }
    }

    if (_configurationRetryPending && !connected &&
        (uint32_t)(millis() - _lastConfigurationAttemptMs) >=
            CONFIGURATION_RETRY_MS) {
        if (applySlotConfiguration()) {
            _configurationRetryPending = false;
            if (!_hidReadyLogged) {
                Serial.println("[BLE] HID keyboard ready");
                _hidReadyLogged = true;
            }
        } else {
            _lastConfigurationAttemptMs = millis();
        }
    }

    if (_slotPersistencePending &&
        (uint32_t)(millis() - _lastSlotPersistenceAttemptMs) >=
            CONFIGURATION_RETRY_MS) {
        if (persistActiveSlot(activeSlot())) {
            _slotPersistencePending = false;
        } else {
            _lastSlotPersistenceAttemptMs = millis();
        }
    }
}

void BLEManager::sendKeyboardReport(const KeyboardReport& report) {
    portENTER_CRITICAL(&_stateMux);
    const bool connected = _connected;
    portEXIT_CRITICAL(&_stateMux);
    if (!connected || _inputReport == nullptr) {
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

bool BLEManager::setActiveSlot(uint8_t slot) {
    if (slot >= DEVICE_SLOT_COUNT) {
        return false;
    }

    portENTER_CRITICAL(&_stateMux);
    if (slot == _activeSlot && !_slotChangePending) {
        portEXIT_CRITICAL(&_stateMux);
        return true;
    }
    const bool connected = _connected;
    _pendingSlot = slot;
    if (!connected) {
        _activeSlot = slot;
    }
    portEXIT_CRITICAL(&_stateMux);

    // 连接状态下先保留当前槽位，收到断开确认后再切换并持久化。
    _slotChangePending = true;
    if (connected) {
        if (!requestDisconnect()) {
            Serial.println("[BLE] Disconnect request failed; will retry");
        }
    } else {
        update();
    }
    return true;
}

void BLEManager::clearActivePairing() {
    portENTER_CRITICAL(&_stateMux);
    const bool connected = _connected;
    _clearSlot = _activeSlot;
    portEXIT_CRITICAL(&_stateMux);

    _clearPending = true;
    if (connected) {
        if (!requestDisconnect()) {
            Serial.println("[BLE] Disconnect request failed; will retry");
        }
    } else {
        update();
    }
}

uint8_t BLEManager::activeSlot() const {
    portENTER_CRITICAL(&_stateMux);
    const uint8_t slot = _activeSlot;
    portEXIT_CRITICAL(&_stateMux);
    return slot;
}

bool BLEManager::isConnected() const {
    portENTER_CRITICAL(&_stateMux);
    const bool connected = _connected;
    portEXIT_CRITICAL(&_stateMux);
    return connected;
}

void BLEManager::handleConnect(
    const char* address,
    uint8_t addressType,
    uint16_t handle
) {
    // 已配对设备只能连接其所属槽位；拒绝连接到其他槽位的已知主机。
    char savedAddresses[DEVICE_SLOT_COUNT][18];
    uint8_t savedAddressTypes[DEVICE_SLOT_COUNT];
    uint8_t selectedSlot;
    portENTER_CRITICAL(&_stateMux);
    memcpy(savedAddresses, _peerAddresses, sizeof(savedAddresses));
    memcpy(savedAddressTypes, _peerAddressTypes, sizeof(savedAddressTypes));
    selectedSlot = _activeSlot;
    portEXIT_CRITICAL(&_stateMux);

    int8_t matchedSlot = -1;
    for (uint8_t slot = 0; slot < DEVICE_SLOT_COUNT; ++slot) {
        if (savedAddresses[slot][0] == '\0') {
            continue;
        }

        const NimBLEAddress savedAddress(
            std::string(savedAddresses[slot]),
            savedAddressTypes[slot]
        );
        const NimBLEAddress incomingAddress(std::string(address), addressType);
        if (savedAddress == incomingAddress) {
            matchedSlot = static_cast<int8_t>(slot);
            break;
        }
    }

    portENTER_CRITICAL(&_stateMux);
    if ((_connected && _connectionHandle != handle) ||
        selectedSlot != _activeSlot ||
        (matchedSlot >= 0 && matchedSlot != _activeSlot)) {
        _rejectConnectionHandle = handle;
        _rejectConnectionPending = true;
    } else {
        _connectionHandle = handle;
        _connectionSlot = _activeSlot;
        _connected = true;
    }
    portEXIT_CRITICAL(&_stateMux);
}

void BLEManager::handleAuthenticatedPeer(
    const char* address,
    uint8_t addressType,
    bool bonded
) {
    // 只保存已建立连接且完成绑定的主机，避免记录未配对设备。
    if (!bonded) {
        return;
    }

    portENTER_CRITICAL(&_stateMux);
    if (!_connected) {
        portEXIT_CRITICAL(&_stateMux);
        return;
    }
    strncpy(_pendingAddress, address, sizeof(_pendingAddress) - 1);
    _pendingAddress[sizeof(_pendingAddress) - 1] = '\0';
    _pendingAddressType = addressType;
    _pendingAddressSlot = _connectionSlot;
    _pendingPeerBonded = true;
    _addressPending = true;
    portEXIT_CRITICAL(&_stateMux);
}

void BLEManager::handleDisconnect() {
    portENTER_CRITICAL(&_stateMux);
    _connected = false;
    _disconnectRequestPending = false;
    _disconnectAttempts = 0;
    portEXIT_CRITICAL(&_stateMux);
}

bool BLEManager::requestDisconnect() {
    uint16_t handle;
    portENTER_CRITICAL(&_stateMux);
    if (!_connected) {
        portEXIT_CRITICAL(&_stateMux);
        return true;
    }
    handle = _connectionHandle;
    _disconnectRequestPending = true;
    if (_disconnectAttempts == 0) {
        _disconnectAttempts = 1;
    } else if (_disconnectAttempts < MAX_DISCONNECT_ATTEMPTS) {
        ++_disconnectAttempts;
    }
    _lastDisconnectRequestMs = millis();
    portEXIT_CRITICAL(&_stateMux);

    if (_server != nullptr && _server->disconnect(handle)) {
        return true;
    }

    return false;
}

bool BLEManager::applySlotConfiguration() {
    char peerAddress[18];
    uint8_t peerAddressType;
    uint8_t slot;
    bool connected;
    portENTER_CRITICAL(&_stateMux);
    slot = _activeSlot;
    connected = _connected;
    memcpy(peerAddress, _peerAddresses[slot], sizeof(peerAddress));
    peerAddressType = _peerAddressTypes[slot];
    portEXIT_CRITICAL(&_stateMux);

    if (_server == nullptr || connected) {
        return false;
    }

    // 重建广播配置，确保设备名称、HID 服务和当前槽位过滤规则一致。
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (!advertising->reset()) {
        Serial.println("[BLE] Failed to reset advertising configuration");
        return false;
    }

    const char* deviceName = BLE_DEVICE_NAMES[slot];
    if (!NimBLEDevice::setDeviceName(deviceName) ||
        !advertising->setAppearance(HID_KEYBOARD) ||
        !advertising->addServiceUUID("1812") ||
        !advertising->setName(deviceName)) {
        Serial.printf(
            "[BLE] Failed to configure advertising for slot %u\n",
            static_cast<unsigned>(slot + 1)
        );
        return false;
    }

    // 先清空上一个槽位留下的白名单，再按当前槽位重新添加配对主机。
    while (NimBLEDevice::getWhiteListCount() > 0) {
        const size_t lastIndex = NimBLEDevice::getWhiteListCount() - 1;
        if (!NimBLEDevice::whiteListRemove(
                NimBLEDevice::getWhiteListAddress(lastIndex)
            )) {
            Serial.println("[BLE] Failed to reset controller whitelist");
            return false;
        }
    }

    const bool hasPeer = peerAddress[0] != '\0';
    if (hasPeer) {
        const NimBLEAddress address(
            std::string(peerAddress),
            peerAddressType
        );
        if (!NimBLEDevice::whiteListAdd(address)) {
            Serial.println("[BLE] Failed to whitelist selected paired host");
            return false;
        }
    }

    // Empty slots accept a new host; populated slots only accept their bonded host.
    advertising->setScanFilter(false, hasPeer);
    if (!advertising->start()) {
        Serial.println("[BLE] Failed to start advertising");
        return false;
    }

    Serial.printf(
        "[BLE] Advertising for slot %u (%s)\n",
        static_cast<unsigned>(slot + 1),
        hasPeer ? "paired host only" : "pairing available"
    );
    return true;
}

void BLEManager::processPendingClear() {
    if (!forgetPeer(_clearSlot)) {
        Serial.printf("[BLE] Pairing clear failed for slot %u\n",
                      static_cast<unsigned>(_clearSlot + 1));
    }
    _clearPending = false;
    _slotChangePending = true;
}

void BLEManager::savePeer(uint8_t slot, const char* address, uint8_t addressType) {
    if (slot >= DEVICE_SLOT_COUNT || address[0] == '\0') {
        return;
    }

    char savedAddress[sizeof(_peerAddresses[slot])];
    strncpy(savedAddress, address, sizeof(savedAddress) - 1);
    savedAddress[sizeof(savedAddress) - 1] = '\0';

    if (!preferences.begin(BLE_PREFERENCES_NAMESPACE, false)) {
        Serial.println("[BLE] Failed to persist paired host");
        return;
    }

    char addressKey[8];
    char typeKey[8];
    snprintf(addressKey, sizeof(addressKey), "peer%u", slot);
    snprintf(typeKey, sizeof(typeKey), "type%u", slot);
    const size_t savedAddressLength =
        preferences.putString(addressKey, savedAddress);
    const size_t savedAddressType =
        preferences.putUChar(typeKey, addressType);
    preferences.end();
    if (savedAddressLength == 0 || savedAddressType == 0) {
        Serial.println("[BLE] Failed to persist complete pairing data");
        return;
    }

    portENTER_CRITICAL(&_stateMux);
    memcpy(_peerAddresses[slot], savedAddress, sizeof(savedAddress));
    _peerAddressTypes[slot] = addressType;
    portEXIT_CRITICAL(&_stateMux);
}

bool BLEManager::persistActiveSlot(uint8_t slot) {
    if (slot >= DEVICE_SLOT_COUNT) {
        return false;
    }

    if (!preferences.begin(BLE_PREFERENCES_NAMESPACE, false)) {
        Serial.println("[BLE] Failed to open storage for selected slot");
        return false;
    }
    const size_t saved = preferences.putUChar("slot", slot);
    preferences.end();
    if (saved == 0) {
        Serial.println("[BLE] Failed to persist selected slot");
        return false;
    }
    return true;
}

bool BLEManager::forgetPeer(uint8_t slot) {
    if (slot >= DEVICE_SLOT_COUNT) {
        return false;
    }

    char peerAddress[sizeof(_peerAddresses[slot])];
    uint8_t peerAddressType;
    portENTER_CRITICAL(&_stateMux);
    memcpy(peerAddress, _peerAddresses[slot], sizeof(peerAddress));
    peerAddressType = _peerAddressTypes[slot];
    portEXIT_CRITICAL(&_stateMux);

    if (!preferences.begin(BLE_PREFERENCES_NAMESPACE, false)) {
        Serial.println("[BLE] Failed to update pairing storage");
        return false;
    }

    char addressKey[8];
    char typeKey[8];
    snprintf(addressKey, sizeof(addressKey), "peer%u", slot);
    snprintf(typeKey, sizeof(typeKey), "type%u", slot);
    const bool removedAddress =
        !preferences.isKey(addressKey) || preferences.remove(addressKey);
    const bool removedType =
        !preferences.isKey(typeKey) || preferences.remove(typeKey);
    preferences.end();
    if (!removedAddress || !removedType) {
        Serial.println("[BLE] Failed to remove pairing data from storage");
        if (peerAddress[0] != '\0' &&
            !restorePairingRecord(slot, peerAddress, peerAddressType)) {
            Serial.println("[BLE] Failed to restore pairing data after storage error");
        }
        return false;
    }

    if (peerAddress[0] != '\0') {
        const NimBLEAddress address(
            std::string(peerAddress),
            peerAddressType
        );
        if (!NimBLEDevice::deleteBond(address)) {
            Serial.println("[BLE] Could not remove the selected host bond");
            if (!restorePairingRecord(slot, peerAddress, peerAddressType)) {
                Serial.println("[BLE] Failed to restore pairing data after bond error");
            }
            return false;
        }
    }

    portENTER_CRITICAL(&_stateMux);
    _peerAddresses[slot][0] = '\0';
    _peerAddressTypes[slot] = BLE_ADDR_PUBLIC;
    portEXIT_CRITICAL(&_stateMux);

    Serial.printf("[BLE] Pairing cleared for slot %u\n",
                  static_cast<unsigned>(slot + 1));
    return true;
}

bool BLEManager::restorePairingRecord(
    uint8_t slot,
    const char* address,
    uint8_t addressType
) {
    if (slot >= DEVICE_SLOT_COUNT ||
        !preferences.begin(BLE_PREFERENCES_NAMESPACE, false)) {
        return false;
    }

    char addressKey[8];
    char typeKey[8];
    snprintf(addressKey, sizeof(addressKey), "peer%u", slot);
    snprintf(typeKey, sizeof(typeKey), "type%u", slot);
    const size_t savedAddress = preferences.putString(addressKey, address);
    const size_t savedType = preferences.putUChar(typeKey, addressType);
    preferences.end();
    return savedAddress > 0 && savedType > 0;
}
