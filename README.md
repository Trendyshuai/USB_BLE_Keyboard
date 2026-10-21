# ESP32-S3 USB-to-BLE Keyboard

Firmware for an ESP32-S3-WROOM-1 N16R8 that reads a wired USB HID keyboard and
forwards its boot-keyboard reports to a Bluetooth Low Energy HID host.

## Hardware

- Module: ESP32-S3-WROOM-1 N16R8
- Arduino-ESP32 core: 3.3.12
- LED1 / LED2 / LED3: GPIO48 / GPIO47 / GPIO21
- Device-selection button: GPIO4
- USB keyboard: connect to the board's USB-OTG host port, not its USB-UART port.
- BLE names by slot: GPIO48 / slot 1 = `ESP32-BLE-1`, GPIO47 / slot 2 =
  `ESP32-BLE-2`, GPIO21 / slot 3 = `ESP32-BLE-3`.

USB host mode must provide 5 V on VBUS at the keyboard connector with suitable
current capability. Do not connect a keyboard directly to the ESP32's 3.3 V
rail.

LEDs are configured active-low (anode to 3.3 V through a resistor, cathode to
GPIO). Each LED corresponds to the BLE name with the same slot number. The
selected slot LED is solid while a BLE host is connected and blinks while the
keyboard is waiting for that host to connect or pair. Other slot LEDs remain
off. The button is wired between GPIO4 and GND, using the internal pull-up.

## Build setup

1. Install/select Espressif's Arduino-ESP32 core **3.3.12** and an ESP32-S3
   board definition.
2. Select USB-OTG / Host mode and disable USB CDC, MSC, and DFU boot functions
   that use the native USB port. Use UART or another serial interface for logs.
3. Install **NimBLE-Arduino 2.3.6** in Arduino IDE's Library Manager.
4. Open `USB_BLE_Keyboard.ino` and build for the ESP32-S3. For an N16R8 module,
   select 16 MB flash and OPI PSRAM when those board options are available.

The USB keyboard class is provided by the USB Host library bundled with
Arduino-ESP32 3.3.12; do not install a separate library that also defines
`USBHostHIDKeyboard`.

## Operation

- First boot starts advertising for slot 1. Pair the computer/tablet using its
  Bluetooth settings.
- Short press advances through the three host slots and restarts advertising
  with that slot's Bluetooth name. A slot with a saved host remains discoverable
  but only accepts that host; an empty slot accepts a new pairing.
- The selected slot is saved in non-volatile storage and restored after reboot.
- Hold the button for 2 seconds to remove the pairing for the selected slot.
  Pair that slot again from the host's Bluetooth settings.
- Button debounce is 30 ms. A long press does not also trigger a short press.
- USB keyboard reports are forwarded as BLE HID keyboard reports. Keystrokes
  are not written to the serial log.
- USB keyboard unplug sends an all-keys-released report to the connected host;
  the USB host remains active for a subsequent hot-plug.

Only one BLE host is connected at a time. The computer/tablet still controls
when it reconnects after switching slots; some operating systems may require
selecting the keyboard again in Bluetooth settings. Pairing is cleared on the
ESP32 for the selected slot, and the host may also need to forget its saved
keyboard entry before pairing again.

## Notes

- GPIO4, GPIO21, GPIO47, and GPIO48 should not be shared with external circuitry
  that conflicts with their assigned functions.
- USB host and USB device/serial modes share ESP32-S3 USB resources. If USB
  Host initialization fails, verify the board's USB mode/menu options and use a
  separate UART adapter for serial output.
- The USB HID handler supports boot-protocol keyboards. Some non-boot/report-
  protocol keyboards may not be recognized by the core's boot-keyboard class.
