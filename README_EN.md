ESP32 Heart-Rate Lamp (Wearable Devices with Heart-Rate Broadcast + OLED ECG)

Author: Douyin / TikTok: sxsxhh1 @没你好果汁吃

Bilibili: UID 510717943 @装作有网名丶

Demo videos:  
Douyin: <https://v.douyin.com/Nna1Fs_QYVk/>  
Bilibili: <https://b23.tv/7hVzfUw>

This project uses an ESP32 to connect via BLE to any wearable device that supports heart-rate broadcast (smart bands, watches, etc.), read real-time heart rate data, and visualize it using LEDs, a buzzer, and a 0.96" OLED display (numeric HR + simulated ECG waveform).

As long as the device implements the standard Heart Rate Service (UUID 0x180D) and enables HR broadcasting, it can work with this project.

Features

• BLE connection to wearables

• Scans nearby BLE devices and matches by name keyword or full name

• Uses standard Heart Rate Service UUID 0x180D and Heart Rate Measurement UUID0x2A37

• In principle compatible with most HR-capable bands/watches (Huawei, Amazfit, Xiaomi, etc.)

• Heart-rate display

• Numeric mode: large-font heart rate on OLED

• Waveform mode: scrolling ECG-style waveform with random events (PVC, arrhythmia, bradycardia, AFib)

• No HR: shows HR: 0 BPM on top and a blinking !warning! at the bottom

• LED + buzzer feedback

• Not connected: LEDs blink every 2 seconds, buzzer off

• Connected with valid HR: LEDs and buzzer follow the beat

• Connected but no HR: LEDs stay ON, buzzer alarms continuously

• Mode switch button

• Short press toggles between numeric mode and ECG mode

• Implemented with millis()-based debouncing and edge-trigger logic

Hardware

• Board: ESP32 DevKit V1

• OLED (SSD1306 128x64 I2C, 0.96")

• VCC/ VDD → 3.3V

• GND → GND

• SCL / SCK → GPIO22 (OLED\_SCL)

• SDA → GPIO21 (OLED\_SDA)

• On-board LED: GPIO2 (LED\_PIN = 2)

• External LED

• Anode → GPIO4 (EXTERNAL\_LED\_PIN)

• Cathode→ resistor (330Ω–1kΩ) → GND

• Buzzer (3V active)

• + → GPIO15 (BUZZER\_PIN)

• - → GND

• Mode button

• One side → GPIO13 (BUTTON\_PIN)

• Other side → GND (internal pull-up)

Configuration

In `sxsxhh1.ino`:

```cpp
#define TARGET_DEVICE_NAME "YourDeviceName"
#define TARGET_MAC_ADDRESS "xx:xx:xx:xx:xx:xx"
#define USE_MAC_MATCH true  // set false to match only by name/UUID
```

Recommended:

1. Start with `USE_MAC_MATCH = false` to connect by name + UUID only.
2. After a stable connection is confirmed, read the real MAC address from the serial log,  
   fill it into `TARGET_MAC_ADDRESS`, and set `USE_MAC_MATCH = true` for stricter matching.

### Usage

1\. Install ESP32 core and required libraries in Arduino IDE.

2\. Open sxsxhh1.ino and adjust the configuration macros to match your device name / MAC and wiring.

3\. Upload to the ESP32 board.

4\. Enable continuous heart-rate measurement / broadcast on your band/watch and ensure it is not connected to a phone app.

5\. Power the ESP32 and monitor the serial output (115200 baud).

The OLED, LEDs, and buzzer should react according to HR and connection status.

Attribution

Please keep the attribution in the source header when using or modifying this project:

&nbsp;\* 署名 / Attribution：

&nbsp;\*   本项目源代码作者：

&nbsp;\*     抖音：sxsxhh1 @没你好果汁吃

&nbsp;\*     哔哩哔哩：UID 510717943 @装作有网名丶

&nbsp;\*   Author:

&nbsp;\*     Douyin / TikTok: sxsxhh1 @没你好果汁吃

&nbsp;\*     Bilibili: UID 510717943 @装作有网名丶

License

Released under the MIT License. See LICENSE for details.

