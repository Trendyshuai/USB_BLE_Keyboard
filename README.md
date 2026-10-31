# ESP32-S3 USB 转 BLE 键盘

这是一个基于 Arduino 的 ESP32-S3 固件：通过 USB Host 读取有线 USB HID
键盘的 boot keyboard 报告，并将按键状态转发给已连接的 BLE HID 主机。
设备提供三个可切换的 BLE 主机槽位，每个槽位可以保存一个配对主机。

## 硬件与接线

- 主控：ESP32-S3-WROOM-1 N16R8。
- USB 键盘：连接到开发板的 USB-OTG Host 接口，不要连接到 USB-UART 接口。
- USB Host 接口必须为键盘提供 5 V VBUS，并具备足够的供电能力。不要从 ESP32
  的 3.3 V 电源轨直接给键盘供电。
- 三个 LED 为低电平点亮，按槽位对应如下。接线方式为阳极经限流电阻接 3.3 V、
  阴极接对应 GPIO。

  | 槽位 | LED GPIO | BLE 广播名称 |
  | --- | --- | --- |
  | 1 | GPIO48 | `ESP32-BLE-1` |
  | 2 | GPIO47 | `ESP32-BLE-2` |
  | 3 | GPIO21 | `ESP32-BLE-3` |

- 槽位选择按键接在 GPIO4 与 GND 之间，使用内部上拉，无需外部上拉电阻。
- GPIO4、GPIO21、GPIO47、GPIO48 不应再连接与其功能冲突的外部电路。

当前选中槽位的 LED 在 BLE 已连接时常亮，在等待连接或配对时以约 500 ms
间隔闪烁；其他槽位的 LED 熄灭。

## Arduino IDE 配置与编译

1. 安装 Arduino IDE，并在开发板管理器中安装 **esp32 by Espressif Systems
   3.3.12**。
2. 在库管理器中安装 **NimBLE-Arduino 2.3.6**。
3. 在开发板选项中选择对应的 ESP32-S3 开发板。对于 N16R8 模组，在选项可用时
   将 Flash Size 设为 16 MB，并启用 OPI PSRAM。
4. 将 USB 模式配置为 USB-OTG Host。USB Host 与 USB Device/串口功能共用
   ESP32-S3 的原生 USB 资源；如使用 USB Host，请关闭会占用该接口的 USB CDC、
   MSC、DFU 等启动功能，并使用 UART（或其他独立串口）查看日志。
5. 在 Arduino IDE 中打开 `USB_BLE_Keyboard.ino` 并编译、上传。`.ino` 文件及
   同目录下的 `.h`、`.cpp` 文件需要保持在同一个 Arduino 草图目录中。

USB 键盘支持由 Arduino-ESP32 3.3.12 自带的 USB Host 库提供。不要再安装提供
同名 `USBHostHIDKeyboard` 类的独立库。串口日志波特率为 **115200**，不会输出
键盘输入内容。

## 使用方法

- 首次启动时选择槽位 1 并开始 BLE 广播。通过电脑、平板或其他 BLE 主机的蓝牙
  设置搜索对应名称并完成配对。
- 短按按键切换到下一个槽位（1 → 2 → 3 → 1）。切换时会断开当前 BLE 连接，
  然后使用新槽位对应的名称重新广播。每次只能连接一个 BLE 主机。
- 空槽位允许新主机配对；已保存配对信息的槽位仅允许对应的已配对主机连接。
  选中槽位会保存到非易失存储，重启后恢复。
- 按住按键 **2 秒**可清除当前槽位的配对信息，并使该槽位重新允许配对。需要时，
  也请在主机的蓝牙设置中删除（忽略）旧的键盘记录，再重新配对。
- 按键消抖时间为 30 ms。长按只触发长按操作，不会同时触发短按。上电时如果按键
  已经按下，固件会忽略这次按压，直到按键释放。
- 插入 USB 键盘后，固件转发其 boot-protocol 键盘报告。键盘拔出时会向已连接的
  BLE 主机发送全键释放报告；USB Host 会继续运行，可再次热插拔键盘。
- BLE 连接建立时会补发当前 USB 键盘状态，避免连接期间状态变化造成按键状态不同步。

某些操作系统在槽位切换或清除配对后可能需要用户重新选择键盘，或手动删除主机端
保存的旧配对记录。设备端槽位配对信息与主机端保存的蓝牙记录是两处独立的状态。

## 支持范围与排查

- 固件使用 BLE HID 键盘服务及 boot keyboard 格式：包含修饰键和最多 6 个同时按下
  的普通键。仅支持被 Arduino-ESP32 自带 boot-keyboard 类识别的键盘；部分只支持
  report protocol 的键盘可能无法使用。
- 如果 USB Host 初始化失败，检查开发板的 USB 模式/启动选项，以及 USB-OTG 接口
  是否提供 VBUS。USB Host 与原生 USB 串口功能冲突时，可使用独立 UART 查看日志。
- 如果无法配对，确认已选中正确槽位及对应 BLE 名称；对于已配对槽位，确认连接的
  是该槽位保存的主机。重新配对时，可清除设备端槽位配对并删除主机端旧记录。

---

# ESP32-S3 USB-to-BLE Keyboard

Arduino firmware for the ESP32-S3 that reads boot-keyboard reports from a wired
USB HID keyboard through USB Host and forwards the key state to a connected BLE
HID host. The device provides three switchable BLE host slots, each of which can
store one paired host.

## Hardware and Wiring

- Board module: ESP32-S3-WROOM-1 N16R8.
- USB keyboard: connect it to the board's USB-OTG Host port, not the USB-UART port.
- The USB Host port must provide 5 V VBUS with sufficient current for the keyboard.
  Do not power the keyboard directly from the ESP32's 3.3 V rail.
- The three LEDs are active-low and correspond to the slots below. Wire each LED's
  anode to 3.3 V through a current-limiting resistor and its cathode to the
  corresponding GPIO.

  | Slot | LED GPIO | BLE advertising name |
  | --- | --- | --- |
  | 1 | GPIO48 | `ESP32-BLE-1` |
  | 2 | GPIO47 | `ESP32-BLE-2` |
  | 3 | GPIO21 | `ESP32-BLE-3` |

- Connect the slot-selection button between GPIO4 and GND. The firmware uses the
  internal pull-up, so no external pull-up resistor is required.
- Do not connect external circuitry to GPIO4, GPIO21, GPIO47, or GPIO48 if it
  conflicts with their assigned functions.

The LED for the selected slot is solid while connected over BLE and blinks at
approximately 500 ms intervals while waiting for a connection or pairing. The
other slot LEDs remain off.

## Arduino IDE Setup and Build

1. Install Arduino IDE and install **esp32 by Espressif Systems 3.3.12** through
   the Boards Manager.
2. Install **NimBLE-Arduino 2.3.6** through the Library Manager.
3. Select the appropriate ESP32-S3 board. For the N16R8 module, set Flash Size to
   16 MB and enable OPI PSRAM when those options are available.
4. Configure the USB mode as USB-OTG Host. USB Host and USB Device/serial
   functions share the ESP32-S3's native USB resources. When using USB Host,
   disable USB CDC, MSC, DFU, and other startup functions that use that interface;
   use UART or another separate serial interface for logs.
5. Open `USB_BLE_Keyboard.ino` in Arduino IDE and build and upload the sketch.
   Keep the `.ino` file and its `.h` and `.cpp` files together in the same Arduino
   sketch directory.

USB keyboard support is provided by the USB Host library bundled with
Arduino-ESP32 3.3.12. Do not install a separate library that defines another
`USBHostHIDKeyboard` class. Serial logs use **115200 baud** and do not include
keyboard input.

## Operation

- On first boot, the device selects slot 1 and starts BLE advertising. Find the
  corresponding name in the Bluetooth settings of a computer, tablet, or other
  BLE host, then pair with it.
- Short-press the button to advance to the next slot (1 → 2 → 3 → 1). Switching
  disconnects the current BLE connection and then advertises under the new slot's
  name. Only one BLE host can be connected at a time.
- Empty slots allow a new host to pair. A slot with saved pairing information
  accepts connections only from its paired host. The selected slot is saved in
  non-volatile storage and restored after reboot.
- Hold the button for **2 seconds** to clear pairing information for the selected
  slot and allow it to pair again. If needed, also remove (forget) the old
  keyboard entry in the host's Bluetooth settings before pairing again.
- Button debounce is 30 ms. A long press triggers only the long-press action and
  does not also trigger a short press. If the button is already held when the
  device powers on, that press is ignored until the button is released.
- When a USB keyboard is connected, its boot-protocol reports are forwarded. When
  the keyboard is unplugged, an all-keys-released report is sent to the connected
  BLE host. USB Host remains active for subsequent hot-plugging.
- When a BLE connection is established, the current USB keyboard state is sent
  again to avoid stale key state if the keyboard changed state during the
  connection interval.

After switching slots or clearing pairing, some operating systems may require
you to select the keyboard again or manually remove the old pairing record on
the host. The device's per-slot pairing data and the host's saved Bluetooth
record are separate.

## Support and Troubleshooting

- The firmware uses a BLE HID keyboard service and the boot-keyboard format:
  modifier keys and up to 6 simultaneously pressed regular keys. It supports
  keyboards recognized by the Arduino-ESP32 boot-keyboard class; some keyboards
  that support only report protocol may not work.
- If USB Host initialization fails, check the board's USB mode and startup
  options, and verify that the USB-OTG port supplies VBUS. If USB Host conflicts
  with native USB serial, use a separate UART to view logs.
- If pairing fails, verify the selected slot and its BLE name. For a slot that
  has already been paired, make sure you are connecting from its saved host. To
  pair again, clear the device's pairing for that slot and remove the old record
  from the host.
