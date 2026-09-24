## ESP32 心率感应灯（任意心率广播穿戴设备 + OLED 心电图）

**作者 / Author：抖音 sxsxhhi @没你好果汁吃  
哔哩哔哩 UID：510717943 @装作有网名丶**

**演示视频：**  
抖音：<https://v.douyin.com/Nna1Fs_QYVk/>  
B站：<https://b23.tv/7hVzfUw>

![实物效果图](CRm13_20260209_225450301.jpg)

本项目使用 ESP32 通过 BLE 连接支持“心率广播”的穿戴设备（智能手环 / 手表等），读取实时心率数据，用 LED、蜂鸣器和 0.96 寸 OLED 显示心率数字和模拟心电图（ECG）波形。  
只要设备支持标准 **Heart Rate Service (0x180D)** 并开启心率广播，即可使用；**默认无需修改代码**，小米、华为、OPPO、Vivo 等品牌手环/手表在开启心率广播后即可直接连接。

### 功能简介

- **BLE 连接穿戴设备**
  - 默认按 **心率服务 UUID 0x180D** 匹配，扫描到任意广播该服务的设备即尝试连接，无需填写设备名称或 MAC
  - 使用标准 **心率特征 UUID：0x2A37** 订阅实时心率
  - 兼容常见支持心率广播的手环/手表（如小米、华为、OPPO、Vivo、华米等）

- **心率显示**
  - 数字模式：OLED 显示大号心率数字
  - 波形模式：OLED 显示滚动 ECG 心电图波形（包含早搏、心律不齐、心率过缓、房颤等随机事件模拟）
  - 无心率时：顶部显示 `HR: 0 BPM`，底部显示闪烁的 `!warning!` 提示

- **LED + 蜂鸣器反馈**
  - 未连接：板载 + 外接 LED 每 2 秒闪一次，蜂鸣器不响
  - 已连接且有有效心率：LED 和蜂鸣器按心跳频率闪烁 / “滴”
  - 已连接但无心率：LED 常亮，蜂鸣器持续报警

- **按键切换显示模式**
  - GPIO13 上拉输入，按钮另一端接 GND
  - 短按在 “数字模式 / 波形模式” 之间切换
  - 使用 `millis()` 实现消抖和单次触发（无阻塞）

### 硬件连接

- **开发板**：ESP32 DevKit V1

- **OLED 屏幕（SSD1306 128x64 I2C，0.96 寸）**
  - `VCC` / `VDD` → 3.3V
  - `GND` → GND
  - `SCL` / `SCK` → GPIO22 (`OLED_SCL`)
  - `SDA` → GPIO21 (`OLED_SDA`)

- **板载 LED**：GPIO2（`LED_PIN = 2`）

- **外接 LED**
  - 正极（长脚） → GPIO4 (`EXTERNAL_LED_PIN`)
  - 负极（短脚） → 电阻（330Ω ~ 1kΩ）→ GND

- **蜂鸣器（3V 有源蜂鸣器）**
  - `+` → GPIO15 (`BUZZER_PIN`)
  - `-` → GND

- **模式切换按钮**
  - 一端 → GPIO13 (`BUTTON_PIN`)
  - 一端 → GND（内部上拉）

### 软件依赖

- Arduino IDE + ESP32 开发板支持包
- 库：
  - ESP32 BLE（`BLEDevice.h` 等）
  - `U8g2`（OLED 显示）
  - `Wire`（I2C）

### 设备配置说明（可选）

默认配置下**无需改代码**即可连接任意一台广播心率服务的手环/手表。若周围有多台设备，可在 `sxsxhh1.ino` 中按需设置：

```cpp
#define TARGET_DEVICE_NAME ""   // 留空=匹配任意广播 0x180D 的设备；填写则仅连接名称包含此字符串的设备
#define TARGET_MAC_ADDRESS "xx:xx:xx:xx:xx:xx"   // 仅当 USE_MAC_MATCH 为 true 时生效
#define USE_MAC_MATCH false   // 默认关闭 MAC 严格匹配，按名称/服务 UUID 即可
```

- **不填 `TARGET_DEVICE_NAME`**：自动连接第一个扫描到的、带心率服务 (0x180D) 的设备  
- **填写设备名称**：仅连接名称中包含该字符串的设备（如 `"HUAWEI Band"`、`"Mi Band"`）  
- **需要固定某台设备时**：在串口日志中记下 MAC，填入 `TARGET_MAC_ADDRESS`，并将 `USE_MAC_MATCH` 改为 `true`

### 使用方法
1. 安装 ESP32 开发板支持和 U8g2 等库。
2. 打开 `sxsxhh1.ino`，默认无需修改配置；若有需要再按上面说明填写设备名称或 MAC。
3. 烧录到 ESP32。
4. 在手环/手表上开启心率广播（持续测量），并确保**未连接手机 App**。
5. 上电 ESP32，串口监视器（115200）可看到扫描 / 连接 / 心率日志，OLED、LED、蜂鸣器按逻辑工作。

### 署名 / Attribution
- 抖音：**sxsxhhi @没你好果汁吃**
- 哔哩哔哩：**UID 510717943 @装作有网名丶**

### 许可证 / License
本项目使用 **MIT License**，详见仓库中的 `LICENSE` 文件。
