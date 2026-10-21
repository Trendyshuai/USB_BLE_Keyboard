#include "Config.h"
#include "ButtonManager.h"
#include "BLEManager.h"
#include "LEDManager.h"
#include "USBManager.h"

ButtonManager buttonManager;
LEDManager ledManager;
USBManager usbManager;
BLEManager bleManager;

// 初始化串口及各功能模块；USB 或 BLE 初始化失败时保留错误日志。
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("================================");
    Serial.println("ESP32-S3 USB to BLE Keyboard");
    Serial.println("Arduino-ESP32 Core 3.3.12");
    Serial.println("================================");

    buttonManager.begin();
    ledManager.begin();

    if (!usbManager.begin()) {
        Serial.println("[ERROR] USB Host initialization failed");
    }

    if (!bleManager.begin()) {
        Serial.println("[ERROR] BLE HID initialization failed");
    }

    Serial.println("[READY] Insert a USB keyboard");
    Serial.println("[READY] Short press: next Bluetooth host slot");
    Serial.println("[READY] Long press: clear pairing for the selected slot");
}

// 每轮先处理 USB 主机事件，再转发键盘报告并处理 BLE、按键和 LED 状态。
void loop() {
    usbManager.task();

    KeyboardReport report;
    if (usbManager.readReport(report)) {
        bleManager.sendKeyboardReport(report);
    }

    bleManager.update();

    const ButtonEvent event = buttonManager.update();

    if (event == BUTTON_SHORT_PRESS) {
        const uint8_t nextSlot =
            (bleManager.activeSlot() + 1) % DEVICE_SLOT_COUNT;
        bleManager.setActiveSlot(nextSlot);
        Serial.printf("[BUTTON] Selected slot %u\n",
                      static_cast<unsigned>(nextSlot + 1));
    } else if (event == BUTTON_LONG_PRESS) {
        bleManager.clearActivePairing();
    }

    ledManager.update(bleManager.activeSlot(), bleManager.isConnected());
}
