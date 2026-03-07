/*
 * ESP32 心率感应灯项目
 * 功能：连接手环/手表心率广播（标准 0x180D），读取心率并通过 LED/蜂鸣器/OLED 反馈
 * 硬件：ESP32 DevKit V1
 * 板载 LED：GPIO 2
 * 蜂鸣器预留：GPIO 18
 *
 * 署名 / Attribution：
 *   本项目源代码作者：
 *     抖音：sxsxhh1 @没你好果汁吃
 *     哔哩哔哩：UID 510717943 @装作有网名丶
 *   Author:
 *     Douyin / TikTok: sxsxhh1 @没你好果汁吃
 *     Bilibili: UID 510717943 @装作有网名丶
 *   GitHub: https://github.com/sxsxhhh/Wearable-Device-Heart-Rate-Broadcast-Interaction-Project
 */

 #include <BLEDevice.h>
 #include <BLEScan.h>
 #include <BLEAdvertisedDevice.h>
 #include <BLEUtils.h>
 #include <BLEClient.h>
 #include <math.h>  // 用于 fmod 函数
 #include <string.h>  // 用于 memset 函数
 #include <Wire.h>
 #include <U8g2lib.h>  // 仅需安装 "U8g2" 库（库管理器搜索 U8g2 安装）
 
// ==================== 配置参数 ====================
#define LED_PIN 2
#define EXTERNAL_LED_PIN 4
#define BUTTON_PIN 13
#define BUZZER_PIN 15
#define BUZZER_BEEP_MS 55
 // 常见3V有源蜂鸣器模块多为低电平触发（LOW=响）。若你的模块是高电平触发，改为 BUZZER_ON HIGH、BUZZER_OFF LOW
 #define BUZZER_ON   HIGH        // 蜂鸣器“响”时的电平（低电平触发）
 #define BUZZER_OFF  LOW       // 蜂鸣器“不响”时的电平
 #define TARGET_DEVICE_NAME ""   // 留空=匹配任意广播心率服务(0x180D)的设备；填写则仅连接名称包含此字符串的设备
#define TARGET_MAC_ADDRESS "xx:xx:xx:xx:xx:xx"   // 仅当 USE_MAC_MATCH 为 true 时生效
#define USE_MAC_MATCH false   // 默认关闭：开源项目设备 MAC 不统一，先用名称/服务 UUID 匹配
#define HEART_RATE_SERVICE_UUID "0000180d-0000-1000-8000-00805f9b34fb"
#define HEART_RATE_CHAR_UUID "00002a37-0000-1000-8000-00805f9b34fb"
#define DEBUG_SCAN true
#define ENABLE_PLOTTER true
#define HEART_RATE_TIMEOUT 2000
#define PLOTTER_UPDATE_INTERVAL 100
#define MAX_VALID_HEART_RATE 250

// 心率有效性检查宏
#define isValidHeartRate() (heartRateValid && lastHeartRate > 0 && lastHeartRate <= MAX_VALID_HEART_RATE)
 #define ECG_WAVEFORM_ENABLED true  // 是否启用心电图波形（true=心电图波形，false=简单数值）
 #define ECG_TIME_SCALE 0.3  // 心电图时间缩放因子
#define ECG_RANDOM_EVENTS true
// 熬夜、作息不规律人群参数调整
#define PREMATURE_BEAT_PROBABILITY 0.002  // 早搏概率（每帧0.2%，约每小时10-15次）
#define ARRHYTHMIA_PROBABILITY 0.008      // 窦性心律不齐概率（每帧0.8%）
#define WAVEFORM_VARIATION_PROBABILITY 0.08  // 波形变化概率（每帧8%）
#define BRADYCARDIA_PROBABILITY 0.0005    // 心率过缓触发概率（每帧0.05%）
#define BRADYCARDIA_DURATION_MS 4000     // 心率过缓持续时长（4秒）
#define BRADYCARDIA_SLOWDOWN 1.20        // 心率过缓时周期延长倍数
#define AFIB_PROBABILITY 0.0001          // 房颤触发概率（每帧0.01%）
#define AFIB_DURATION_MS 3500            // 房颤持续时长（3.5秒）
#define DATA_BUFFER_SIZE 350
#define DISPLAY_INTERVAL 5000
#define DATA_PER_LINE 70
 
// ==================== OLED 屏幕配置 ====================
#define OLED_SDA 21
#define OLED_SCL 22
#define DISPLAY_UPDATE_INTERVAL 300
#define DISPLAY_UPDATE_INTERVAL_SCAN 800
#define WAVEFORM_UPDATE_INTERVAL 50

bool displayReady = false;
bool lastDisplayedConnectedState = false;
int displayMode = 0;  // 0=数字模式，1=波形模式

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ==================== 按钮消抖相关变量 ====================
#define BUTTON_DEBOUNCE_MS 50
bool buttonLastState = HIGH;
unsigned long buttonPressStartTime = 0;
bool buttonPressed = false;
bool buttonHandled = false;

// ==================== ECG波形显示缓冲区 ====================
#define ECG_DISPLAY_WIDTH 128
#define ECG_DISPLAY_HEIGHT 64
#define ECG_GRAPH_HEIGHT 48
#define ECG_GRAPH_TOP 8
#define ECG_GRAPH_BOTTOM (ECG_GRAPH_TOP + ECG_GRAPH_HEIGHT)
#define ECG_INVALID_VALUE 0xFF

uint16_t ecgDisplayBuffer[ECG_DISPLAY_WIDTH];
uint16_t ecgDisplayIndex = 0;

// ==================== 全局变量 ====================
BLEScan* pBLEScan;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
BLEAddress* targetDeviceAddress = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;
bool heartRateValid = false;
bool isScanning = false;
bool needConnect = false;
bool warningVisible = true;

uint32_t lastHeartRate = 0;
uint32_t lastDisplayedHeartRate = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastLedBeatTime = 0;
unsigned long lastHeartRateTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long scanStartTime = 0;
unsigned long connectStartTime = 0;
unsigned long lastWarningBlink = 0;
unsigned long lastPlotterTime = 0;
unsigned long ecgVirtualTime = 0;

float heartRatePeriod = 0.0;

const unsigned long BLINK_DURATION = 100;
const unsigned long CONNECT_TIMEOUT = 20000;
const unsigned long WARNING_BLINK_INTERVAL = 500;

// 数据缓冲区（存储350个数据点）
uint16_t dataBuffer[DATA_BUFFER_SIZE] = {0};
uint16_t bufferIndex = 0;
bool bufferFull = false;
unsigned long lastDisplayTime = 0;
 
// ==================== 数据缓冲区管理函数 ====================
void addDataToBuffer(uint16_t value) {
   dataBuffer[bufferIndex] = value;
   bufferIndex++;
   
   // 如果缓冲区已满，循环覆盖
   if (bufferIndex >= DATA_BUFFER_SIZE) {
     bufferIndex = 0;
     bufferFull = true;
   }
 }
 
void displayDataBuffer() {
   Serial.println("\n========================================");
   Serial.print("数据缓冲区内容（最新 ");
   
   uint16_t count = bufferFull ? DATA_BUFFER_SIZE : bufferIndex;
   uint16_t displayCount = (count >= DATA_BUFFER_SIZE) ? DATA_BUFFER_SIZE : count;
   
   Serial.print(displayCount);
   Serial.println(" 个数据点）：");
   Serial.println("========================================");
   
   if (displayCount == 0) {
     Serial.println("缓冲区为空，暂无数据");
     Serial.println("========================================\n");
     return;
   }
   
   // 显示数据点（每行显示更多数据点，横向显示更宽）
   // 如果缓冲区已满，从bufferIndex开始显示（最新的数据）
   // 如果缓冲区未满，从0开始显示（所有数据）
   for (uint16_t i = 0; i < displayCount; i++) {
     uint16_t idx;
     if (bufferFull) {
       // 缓冲区已满，从bufferIndex开始循环显示最新的350个数据点
       idx = (bufferIndex + i) % DATA_BUFFER_SIZE;
     } else {
       // 缓冲区未满，从0开始显示所有数据
       idx = i;
     }
     
     // 紧凑格式输出（数字+逗号，不右对齐，节省空间）
     Serial.print(dataBuffer[idx]);
     
     // 每DATA_PER_LINE个数据点换行
     if ((i + 1) % DATA_PER_LINE == 0) {
       Serial.println();
     } else {
       Serial.print(",");  // 使用逗号分隔，不空格，更紧凑
     }
   }
   
   // 如果最后一行不满DATA_PER_LINE个，也要换行
   if (displayCount % DATA_PER_LINE != 0) {
     Serial.println();
   }
   
   Serial.print("\n总计: ");
   Serial.print(displayCount);
   Serial.print(" 个数据点");
   if (bufferFull) {
     Serial.print(" (缓冲区已满，显示最新350个数据点)");
   } else {
     Serial.print(" (缓冲区未满，显示所有数据)");
   }
   Serial.println();
   Serial.println("========================================\n");
 }
 
// ==================== 心电图波形生成函数 ====================
float generateECGWaveform(float timeInCycle) {
   // 标准心电图波形时间分配（归一化到0-1周期）
   // P波：0.0 - 0.15（心房去极化，小幅度正向波）
   // PQ段：0.15 - 0.25（房室传导延迟，基线）
   // QRS复合波：0.25 - 0.35（心室去极化，大幅度尖锐波形）
   // ST段：0.35 - 0.60（心室复极化开始，基线）
   // T波：0.60 - 0.85（心室复极化，中等幅度正向波）
   // TP段：0.85 - 1.0（等电位期，基线+轻微波动）
   
   float value = 0.0;
   
   // P波（平滑的正向波）
   if (timeInCycle >= 0.0 && timeInCycle < 0.15) {
     float pPos = (timeInCycle - 0.0) / 0.15;
     value = 0.3 * sin(PI * pPos);  // 小幅度正向波
   }
   // PQ段（基线）
   else if (timeInCycle >= 0.15 && timeInCycle < 0.25) {
     value = 0.0;
   }
   // QRS复合波（大幅度的尖锐波形）
   else if (timeInCycle >= 0.25 && timeInCycle < 0.35) {
     float qrsPos = (timeInCycle - 0.25) / 0.10;
     // Q波（负向）
     if (qrsPos < 0.2) {
       value = -0.8 * qrsPos / 0.2;
     }
     // R波（正向尖峰）
     else if (qrsPos < 0.6) {
       value = -0.8 + 1.6 * (qrsPos - 0.2) / 0.4;
     }
     // S波（负向）
     else {
       value = 0.8 - 1.6 * (qrsPos - 0.6) / 0.4;
     }
   }
   // ST段（基线）
   else if (timeInCycle >= 0.35 && timeInCycle < 0.60) {
     value = 0.0;
   }
   // T波（平滑的正向波，更宽）
   else if (timeInCycle >= 0.60 && timeInCycle < 0.85) {
     float tPos = (timeInCycle - 0.60) / 0.25;
     // 使用更平滑的波形
     value = 0.4 * sin(PI * tPos) * (1.0 - 0.3 * tPos);  // 中等幅度正向波，逐渐衰减
   }
   // TP段（基线，可能有轻微波动，模拟呼吸等影响）
   else {
     float tpPos = (timeInCycle - 0.85) / 0.15;
     // 轻微的基线波动（模拟呼吸、肌肉活动等）
     value = 0.05 * sin(2 * PI * tpPos) + 0.02 * sin(4 * PI * tpPos);
   }
   
   return value;
 }
 
uint16_t getECGValue(uint16_t currentBPM) {
   if (currentBPM == 0 || !heartRateValid) {
     // 没有心率数据，返回0（基线）
     return 0;
   }
   
   // 计算心跳周期（秒）
   float period = 60.0 / currentBPM;
   
   // 房颤状态（先声明，用于与心率过缓互斥）
   static unsigned long afibUntil = 0;
   static float afibPhase = 0.0;
   
  // 心率过缓（Bradycardia）- 仅在不处于房颤时出现（窦性心动过缓与房颤互斥）
  // 医学统计：正常人夜间心率可降至35-60bpm，2-8%正常人可出现短暂性房室阻滞
  static unsigned long bradycardiaUntil = 0;
  bool inAfib = (millis() < afibUntil);
  if (ECG_RANDOM_EVENTS && !inAfib && (millis() - bradycardiaUntil > 60000)) {  // 至少间隔60秒（模拟偶发的心率下降）
    if (random(1000) < (BRADYCARDIA_PROBABILITY * 1000)) {
      bradycardiaUntil = millis() + BRADYCARDIA_DURATION_MS;
    }
  }
   bool inBradycardia = (millis() < bradycardiaUntil);
   if (inBradycardia) {
     period *= BRADYCARDIA_SLOWDOWN;  // 周期变长，心率变慢
   }
   
   // 使用虚拟时间计数器，减慢波形显示速度
   static unsigned long lastUpdateTime = 0;
   unsigned long currentTime = millis();
   
   if (lastUpdateTime == 0) {
     lastUpdateTime = currentTime;
     ecgVirtualTime = 0;
   }
   
   unsigned long realTimeDelta = currentTime - lastUpdateTime;
   if (realTimeDelta > 0) {
     ecgVirtualTime += (unsigned long)(realTimeDelta * ECG_TIME_SCALE);
     lastUpdateTime = currentTime;
   }
   
   // 计算在周期中的位置（0.0 到 1.0）
   float cyclePosition = fmod((ecgVirtualTime / 1000.0) / period, 1.0);
   
  // 房颤（Atrial Fibrillation）- 仅在不处于心率过缓时触发（与窦性心动过缓互斥）
  // 医学统计：60岁以上人群房颤发病率约5.2/1000人年，健康年轻人极罕见
  if (ECG_RANDOM_EVENTS && !inBradycardia && (millis() - afibUntil > 300000)) {  // 至少间隔5分钟（模拟极罕见的房颤发作）
    if (random(10000) < (AFIB_PROBABILITY * 10000)) {  // 使用10000作为基数以提高精度
      afibUntil = millis() + AFIB_DURATION_MS;
      afibPhase = (float)(millis() % 1000) / 1000.0;
    }
  }
   if (inAfib) {
     // 房颤：对周期位置加随机扰动，使RR间期不规则
     cyclePosition += (random(200) - 100) / 1000.0;
     if (cyclePosition < 0) cyclePosition = 0;
     if (cyclePosition >= 1.0) cyclePosition = fmod(cyclePosition, 1.0);
   }
   
   // 随机事件处理（让心电图更真实）
   static unsigned long lastPrematureBeat = 0;
   static unsigned long prematureBeatStart = 0;  // 早搏开始时间
   static bool inPrematureBeat = false;  // 是否在早搏期间
   static unsigned long lastArrhythmia = 0;
   static float arrhythmiaOffset = 0.0;
   static float waveformVariation = 1.0;
   
   float ecgWave = 0.0;
   
   if (ECG_RANDOM_EVENTS) {
    // 1. 早搏（Premature Beat）- 仅在窦性节律时出现（房颤时RR已不规则，不再单独触发室早）
    // 熬夜人群：早搏更频繁，24小时内可达数百次，间隔可能缩短至5-10秒
    if (!inPrematureBeat && !inAfib && (millis() - lastPrematureBeat > 8000)) {  // 早搏间隔至少8秒（熬夜人群更频繁）
      if (random(1000) < (PREMATURE_BEAT_PROBABILITY * 1000)) {
        if (cyclePosition > 0.6 && cyclePosition < 0.85) {  // 在T波后触发（符合医学：早搏多在T波后）
          prematureBeatStart = millis();
          inPrematureBeat = true;
          lastPrematureBeat = millis();
        }
      }
    }
     
     // 如果正在早搏期间，生成早搏波形
     if (inPrematureBeat) {
       unsigned long prematureDuration = millis() - prematureBeatStart;
       if (prematureDuration < 120) {  // 早搏持续约120ms
         // 生成早搏的QRS波（更尖锐、更快速）
         float prematureProgress = prematureDuration / 120.0;
         if (prematureProgress < 0.25) {
           // Q波（负向）
           ecgWave = -0.7 * (prematureProgress / 0.25);
         } else if (prematureProgress < 0.6) {
           // R波（正向尖峰，更尖锐）
           ecgWave = -0.7 + 1.4 * ((prematureProgress - 0.25) / 0.35);
         } else {
           // S波（负向）
           ecgWave = 0.7 - 1.4 * ((prematureProgress - 0.6) / 0.4);
         }
       } else {
         // 早搏结束，恢复正常波形
         inPrematureBeat = false;
         // 调整周期位置，跳过早搏后的补偿期
         cyclePosition = 0.4;  // 跳到ST段
       }
     }
     
    // 2. 窦性心律不齐（Arrhythmia）- 仅在窦性节律时出现；房颤时RR已不规则，不再叠加
    // 熬夜、作息不规律：自主神经功能紊乱，心律不齐更明显，波动幅度增大
    if (!inAfib && (millis() - lastArrhythmia > 3000)) {  // 每3秒检查一次（更频繁，模拟自主神经不稳定）
      if (random(1000) < (ARRHYTHMIA_PROBABILITY * 1000)) {
        arrhythmiaOffset = (random(150) - 75) / 1000.0;  // ±0.075的偏移（幅度增大，模拟自主神经功能紊乱）
        lastArrhythmia = millis();
      }
    }
     arrhythmiaOffset *= 0.98;
     if (!inPrematureBeat && !inAfib) {  // 早搏期间、房颤期间均不叠加窦性心律不齐
       cyclePosition += arrhythmiaOffset;
       if (cyclePosition < 0) cyclePosition = 0;
       if (cyclePosition >= 1.0) cyclePosition = fmod(cyclePosition, 1.0);
     }
     
    // 3. 波形变化（Waveform Variation）- 模拟呼吸、体位、血糖波动等影响
    // 饮食不规律：血糖波动影响心率稳定性，波形变化更明显
    if (random(1000) < (WAVEFORM_VARIATION_PROBABILITY * 1000)) {
      // 改变波形幅度（±10%），模拟血糖波动、自主神经不稳定等影响
      waveformVariation = 0.90 + (random(20) / 100.0);  // 0.90-1.10之间（幅度增大）
    } else {
      // 逐渐恢复到正常幅度（恢复速度稍慢，模拟持续的不稳定状态）
      waveformVariation = 0.96 * waveformVariation + 0.04 * 1.0;
    }
   }
   
   // 如果不在早搏期间，生成正常ECG波形
   if (!inPrematureBeat) {
     ecgWave = generateECGWaveform(cyclePosition);
   }
   
   // 房颤期间：叠加 f 波（不规则基线颤动，P波被掩盖）
   if (ECG_RANDOM_EVENTS && inAfib) {
     afibPhase += 0.15 + (random(100) / 1000.0);  // 不规则相位推进
     float fWave = 0.12 * sin(afibPhase * 6.28) + 0.08 * sin(afibPhase * 12.5 + 1.3);  // 不规则小波
     ecgWave += fWave;  // 在QRS之间也会出现颤动，模拟房颤
   }
   
   // 应用波形变化（对所有波形都应用）
   ecgWave *= waveformVariation;
   
   // 转换为绘图仪值：基线对应实际心率值，波形在心率值上下波动
   // 像医院心电图机那样，基线在心率值附近，波形上下波动
   // 波动幅度：±30 BPM，让波形清晰可见但不会超出合理范围
   float ecgValue = (float)currentBPM + ecgWave * 30.0;
   
   // 确保值在有效范围内（Arduino绘图仪支持0-1023，心率通常在40-200之间）
   if (ecgValue < 0) ecgValue = 0;
   if (ecgValue > 1023) ecgValue = 1023;
   
   return (uint16_t)ecgValue;
 }
 
 // ==================== 资源清理函数 ====================
 void cleanupResources() {
   if (pClient != nullptr) {
     pClient->disconnect();
     delete pClient;
     pClient = nullptr;
   }
   pRemoteCharacteristic = nullptr;
   
   heartRateValid = false;
   lastHeartRate = 0;
   lastHeartRateTime = 0;
   lastHeartbeatTime = 0;
   heartRatePeriod = 0;
   ecgVirtualTime = 0;
   
   bufferIndex = 0;
   bufferFull = false;
   memset(dataBuffer, 0, sizeof(dataBuffer));
   
   if (targetDeviceAddress != nullptr) {
     delete targetDeviceAddress;
     targetDeviceAddress = nullptr;
   }
   
  lastDisplayedHeartRate = 0;
  lastDisplayedConnectedState = false;
}

// ==================== LED和蜂鸣器控制函数 ====================
void updateLEDAndBuzzer() {
  if (!deviceConnected) {
    // 未连接时：每2秒闪一次 LED，蜂鸣器不响
    digitalWrite(BUZZER_PIN, BUZZER_OFF);
    static unsigned long lastWaitBlink = 0;
    static bool waitLedOn = false;
    if (!waitLedOn && (millis() - lastWaitBlink >= 2000)) {
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(EXTERNAL_LED_PIN, HIGH);
      waitLedOn = true;
      lastWaitBlink = millis();
      Serial.print(".");
      Serial.flush();
    } else if (waitLedOn && (millis() - lastWaitBlink >= 30)) {
      digitalWrite(LED_PIN, LOW);
      digitalWrite(EXTERNAL_LED_PIN, LOW);
      waitLedOn = false;
    }
  } else if (isValidHeartRate()) {
    // 已连接且有有效心率：按心跳频率闪烁 LED，蜂鸣器按同样频率发"滴"
    unsigned long intervalMs = 60000UL / lastHeartRate;
    
    if (millis() - lastLedBeatTime >= intervalMs) {
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(EXTERNAL_LED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, BUZZER_ON);
      lastBlinkTime = millis();
      lastLedBeatTime = millis();
    }
    
    // LED 亮 BLINK_DURATION 后熄灭；蜂鸣器响 BUZZER_BEEP_MS 后关闭
    if (lastBlinkTime > 0) {
      if (millis() - lastBlinkTime >= BLINK_DURATION) {
        digitalWrite(LED_PIN, LOW);
        digitalWrite(EXTERNAL_LED_PIN, LOW);
        lastBlinkTime = 0;
      }
      if (millis() - lastLedBeatTime >= BUZZER_BEEP_MS) {
        digitalWrite(BUZZER_PIN, BUZZER_OFF);
      }
    }
  } else {
    // 已连接但无心率：LED 常亮，蜂鸣器持续发声（警报）
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(EXTERNAL_LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, BUZZER_ON);
    lastBlinkTime = 0;
  }
}

// ==================== 按钮检测函数 ====================
void checkButton() {
  bool buttonState = digitalRead(BUTTON_PIN);
  
  if (buttonState == LOW && buttonLastState == HIGH) {
    buttonPressStartTime = millis();
    buttonPressed = false;
    buttonHandled = false;
  } else if (buttonState == LOW && buttonLastState == LOW) {
    if (!buttonPressed && !buttonHandled && (millis() - buttonPressStartTime >= BUTTON_DEBOUNCE_MS)) {
      buttonPressed = true;
      buttonHandled = true;
      displayMode = (displayMode == 0) ? 1 : 0;
      
      if (displayMode == 1) {
        // 切换到波形模式时，清空显示缓冲区
        for (uint16_t i = 0; i < ECG_DISPLAY_WIDTH; i++) {
          ecgDisplayBuffer[i] = ECG_INVALID_VALUE;
        }
        ecgDisplayIndex = 0;
      }
      
      Serial.print(">>> 显示模式切换为: ");
      Serial.println(displayMode == 0 ? "数字模式" : "波形模式");
    }
  } else if (buttonState == HIGH && buttonLastState == LOW) {
    buttonPressed = false;
    buttonHandled = false;
  }
  
  buttonLastState = buttonState;
}

// ==================== ECG波形绘制函数 ====================
void drawECGWaveform() {
  uint16_t baselineY = ECG_GRAPH_TOP + ECG_GRAPH_HEIGHT / 2;
  bool noHeartRate = !isValidHeartRate();
  
  // 清空显示缓冲区
  u8g2.clearBuffer();
  
  // 显示心率值（顶部，无心率时显示0）
  u8g2.setFont(u8g2_font_6x10_tf);
  char hrText[16];
  if (noHeartRate) {
    snprintf(hrText, sizeof(hrText), "HR: 0 BPM");
  } else {
    snprintf(hrText, sizeof(hrText), "HR: %u BPM", (unsigned int)lastHeartRate);
  }
  u8g2.drawStr(0, 7, hrText);
  
  // 根据是否有心率，决定添加的数据点类型
  uint16_t newDataY;
  if (noHeartRate) {
    newDataY = baselineY;  // 无心率：添加基线值（直线）
  } else {
    // 有心率：计算波形值
    uint16_t currentECGValue = getECGValue(lastHeartRate);
    float ecgWave = ((float)currentECGValue - (float)lastHeartRate) / 30.0;
    int16_t ecgOffset = (int16_t)(ecgWave * ECG_GRAPH_HEIGHT / 2);
    uint16_t ecgY = baselineY - ecgOffset;
    
    // 限制Y坐标在显示区域内
    if (ecgY < ECG_GRAPH_TOP) ecgY = ECG_GRAPH_TOP;
    if (ecgY >= ECG_GRAPH_BOTTOM) ecgY = ECG_GRAPH_BOTTOM - 1;
    newDataY = ecgY;
  }
  
  // 将新数据点存储到缓冲区（继续滚动）
  ecgDisplayBuffer[ecgDisplayIndex] = newDataY;
  
  // 绘制ECG波形（从左到右滚动显示）
  for (uint16_t x = 0; x < ECG_DISPLAY_WIDTH; x++) {
    uint16_t bufferIdx = (ecgDisplayIndex + 1 + x) % ECG_DISPLAY_WIDTH;
    uint16_t y = ecgDisplayBuffer[bufferIdx];
    
    if (y != ECG_INVALID_VALUE && y >= ECG_GRAPH_TOP && y < ECG_GRAPH_BOTTOM) {
      u8g2.drawPixel(x, y);
      
      // 绘制连线（平滑效果）
      if (x < ECG_DISPLAY_WIDTH - 1) {
        uint16_t nextIdx = (bufferIdx + 1) % ECG_DISPLAY_WIDTH;
        uint16_t nextY = ecgDisplayBuffer[nextIdx];
        if (nextY != ECG_INVALID_VALUE && nextY >= ECG_GRAPH_TOP && nextY < ECG_GRAPH_BOTTOM) {
          u8g2.drawLine(x, y, x + 1, nextY);
        }
      }
    }
  }
  
  // 无心率时：屏幕底部显示"!warning!"（居中，闪烁）
  if (noHeartRate) {
    if (millis() - lastWarningBlink >= WARNING_BLINK_INTERVAL) {
      warningVisible = !warningVisible;
      lastWarningBlink = millis();
    }
    if (warningVisible) {
      u8g2.setFont(u8g2_font_10x20_tf);
      const char* warningText = "!warning!";
      int16_t textWidth = u8g2.getStrWidth(warningText);
      int16_t xPos = (ECG_DISPLAY_WIDTH - textWidth) / 2;  // 居中
      u8g2.drawStr(xPos, 60, warningText);
    }
  }
  
  // 更新索引（实现滚动效果）
  ecgDisplayIndex = (ecgDisplayIndex + 1) % ECG_DISPLAY_WIDTH;
}

// ==================== BLE 回调类 ====================
 class MyClientCallback : public BLEClientCallbacks {
   void onConnect(BLEClient* pclient) {
     Serial.println(">>> [回调] 设备已连接");
     deviceConnected = true;
   }
 
   void onDisconnect(BLEClient* pclient) {
     Serial.println(">>> [回调] 设备已断开");
     deviceConnected = false;
     cleanupResources();
   }
 };
 
 // 心率数据通知回调函数
 static void notifyCallback(
   BLERemoteCharacteristic* pBLERemoteCharacteristic,
   uint8_t* pData,
   size_t length,
   bool isNotify) {
   
   // 检查数据长度
   if (length > 0) {
     // 心率数据格式：第一个字节是标志位，第二个字节是心率值（BPM）
     // 如果标志位第0位为0，则心率值在第二个字节；如果为1，则心率值在第二和第三个字节（16位）
     uint8_t flags = pData[0];
     uint16_t heartRate = 0;
     
     if (length >= 2) {
       if (flags & 0x01) {
         // 16位心率值
         if (length >= 3) {
           heartRate = (pData[2] << 8) | pData[1];
         }
       } else {
         // 8位心率值
         heartRate = pData[1];
       }
       
       lastHeartRate = heartRate;
       lastHeartRateTime = millis();  // 更新最后收到心率数据的时间
       heartRateValid = true;  // 标记心率数据有效
       
       // 更新心跳周期（用于ECG波形生成）
       if (heartRate > 0) {
         heartRatePeriod = 60.0 / heartRate;  // 心跳周期（秒）
         // 重置虚拟时间，让波形从新周期开始（可选，如果希望每次收到数据都重置）
         // ecgVirtualTime = 0;  // 取消注释以在每次收到新数据时重置波形
       }
       
       // 串口输出（文本格式）- 仅在串口监视器打开时显示，不影响绘图仪
       // 如果使用绘图仪，建议注释掉下面的代码，避免干扰
       /*
       Serial.print("当前心率: ");
       Serial.print(heartRate);
       Serial.println(" BPM");
       */
       
       // 不再在这里触发 LED，改为在 loop() 中按心率频率均匀闪烁
     }
   }
 }
 
 // 扫描回调 - 检查设备名称或心率服务
 class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
   void onResult(BLEAdvertisedDevice advertisedDevice) {
     // 调试：打印所有扫描到的设备
     if (DEBUG_SCAN) {
       Serial.print("扫描到设备: ");
       if (advertisedDevice.haveName()) {
         Serial.print("名称=");
         Serial.print(advertisedDevice.getName().c_str());
         Serial.print(", ");
       }
       Serial.print("地址=");
       Serial.print(advertisedDevice.getAddress().toString().c_str());
       Serial.print(", RSSI=");
       Serial.print(advertisedDevice.getRSSI());
       if (advertisedDevice.haveServiceUUID()) {
         Serial.print(", 服务UUID=");
         Serial.print(advertisedDevice.getServiceUUID().toString().c_str());
       }
       Serial.println();
     }
     
    // 设备名称匹配：仅当用户填写了 TARGET_DEVICE_NAME 时才按名称过滤
    bool isTargetDevice = false;
    String targetName = String(TARGET_DEVICE_NAME);
    targetName.trim();
    if (targetName.length() > 0 && advertisedDevice.haveName()) {
      String deviceName = String(advertisedDevice.getName().c_str());
      deviceName.toLowerCase();
      targetName.toLowerCase();
      if (deviceName.indexOf(targetName) >= 0) {
        isTargetDevice = true;
        Serial.print(">>> 发现目标设备（名称包含）: ");
        Serial.println(advertisedDevice.getName().c_str());
      }
    }

    // 检查设备是否广播标准心率服务 UUID 0x180D（小米/华为/OPPO/Vivo 等只要支持心率广播都会带）
    // 留空 TARGET_DEVICE_NAME 时，仅靠此项即可匹配，无需改代码
     bool hasHeartRateService = false;
     if (advertisedDevice.haveServiceUUID() && 
         advertisedDevice.isAdvertisingService(BLEUUID(HEART_RATE_SERVICE_UUID))) {
       hasHeartRateService = true;
       Serial.print(">>> 设备广播中包含心率服务: ");
       if (advertisedDevice.haveName()) {
         Serial.print(advertisedDevice.getName().c_str());
         Serial.print(" ");
       }
       Serial.print("(");
       Serial.print(advertisedDevice.getAddress().toString().c_str());
       Serial.println(")");
     }
     
     // 检查MAC地址是否匹配（如果启用）
     bool macMatched = false;
     if (USE_MAC_MATCH) {
       String deviceAddr = String(advertisedDevice.getAddress().toString().c_str());
       deviceAddr.toLowerCase();
       String targetMac = String(TARGET_MAC_ADDRESS);
       targetMac.toLowerCase();
       
       if (deviceAddr.equals(targetMac)) {
         macMatched = true;
         Serial.print(">>> 发现目标设备（MAC地址匹配）: ");
         Serial.println(advertisedDevice.getAddress().toString().c_str());
       }
     }
     
     // 如果找到目标设备（通过名称、服务UUID或MAC地址任一匹配），保存地址准备连接
     // 匹配优先级：名称匹配 > 服务UUID匹配 > MAC地址匹配
     if (isTargetDevice || hasHeartRateService || macMatched) {
       // 停止扫描
       isScanning = false;
       BLEDevice::getScan()->stop();
       
       // 保存设备地址，在主循环中连接（避免阻塞）
       if (targetDeviceAddress != nullptr) {
         delete targetDeviceAddress;
       }
       targetDeviceAddress = new BLEAddress(advertisedDevice.getAddress());
       needConnect = true;
       connectStartTime = millis();
       
       Serial.print(">>> 发现目标设备，准备连接: ");
       Serial.println(targetDeviceAddress->toString().c_str());
       Serial.println(">>> 将在主循环中尝试连接...");
     }
   }
 };
 
 // ==================== 初始化函数 ====================
 void setup() {
  // 先初始化 GPIO，让LED闪烁表示代码运行
  pinMode(LED_PIN, OUTPUT);
  pinMode(EXTERNAL_LED_PIN, OUTPUT);  // 外接 LED
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // 模式切换按钮（使用内部上拉电阻）
   
   // LED 闪烁 3 次表示启动（板载 + 外接同步）
   for(int i = 0; i < 3; i++) {
     digitalWrite(LED_PIN, HIGH);
     digitalWrite(EXTERNAL_LED_PIN, HIGH);
     delay(200);
     digitalWrite(LED_PIN, LOW);
     digitalWrite(EXTERNAL_LED_PIN, LOW);
     delay(200);
   }
   
   // 初始化串口 - 等待串口就绪
   Serial.begin(115200);
   delay(1000);  // 等待串口稳定
   
   // 输出启动信息
   Serial.println("\n\n\n");
   Serial.println("========================================");
   Serial.println("ESP32 心率感应灯项目");
   Serial.println("系统启动中...");
   Serial.println("========================================");
   Serial.flush();  // 确保数据发送
   
   delay(500);
   
   Serial.println("步骤 1/5: GPIO 初始化完成");
   Serial.flush();
   
  // 初始化 I2C 与 OLED 屏幕（U8g2）
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  displayReady = true;
  
  // 初始化ECG显示缓冲区
  for (uint16_t i = 0; i < ECG_DISPLAY_WIDTH; i++) {
    ecgDisplayBuffer[i] = ECG_INVALID_VALUE;
  }
  ecgDisplayIndex = 0;
  displayMode = 0;  // 默认数字模式
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Heart Rate Monitor");
  u8g2.drawStr(0, 22, "Scanning...");
  u8g2.sendBuffer();
  Serial.println("步骤 2/5: OLED 屏幕初始化完成");
  Serial.flush();
   
   // 初始化 BLE
   Serial.println("步骤 3/5: 正在初始化 BLE...");
   Serial.flush();
   
   BLEDevice::init("");
   
   Serial.println("步骤 4/5: BLE 初始化完成");
   Serial.flush();
   
   pBLEScan = BLEDevice::getScan();
   pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
   pBLEScan->setActiveScan(true);  // 主动扫描，获取更多信息（包括服务UUID）
   pBLEScan->setInterval(50);     // 扫描间隔（毫秒），减小间隔以提高扫描频率
   pBLEScan->setWindow(49);       // 扫描窗口（毫秒），优化扫描效率
   
   Serial.println("步骤 5/5: 扫描器配置完成");
   Serial.flush();
   
   Serial.println("\n========================================");
   Serial.println("开始扫描心率设备...");
   Serial.print("目标设备名称: ");
   Serial.println(String(TARGET_DEVICE_NAME).length() > 0 ? TARGET_DEVICE_NAME : "(留空=匹配任意广播心率服务的设备)");
   Serial.print("MAC 严格匹配: ");
   Serial.println(USE_MAC_MATCH ? "是" : "否");
   Serial.println("请确保手环/手表已开启心率广播（持续测量），且未连接手机 App");
   Serial.println("========================================\n");
   Serial.flush();
   
   // 初始化随机数种子（用于随机波动）
   randomSeed(analogRead(0));  // 使用未连接的模拟引脚作为随机种子
   
   // 开始扫描
   isScanning = true;
   scanStartTime = millis();
   Serial.println(">>> 开始BLE扫描...");
   Serial.flush();
   
   pBLEScan->start(0, false);  // 持续扫描，不阻塞
   
   Serial.println(">>> 扫描已启动，等待设备...");
   Serial.flush();
 }
 
 // ==================== 主循环 ====================
 void loop() {
   // ========== 优先级1：蓝牙连接任务（最高优先级）==========
   // 在扫描/连接阶段，完全停止OLED和绘图仪输出，确保BLE有足够资源
   
   // 处理连接状态变化
   if (deviceConnected != oldDeviceConnected) {
     oldDeviceConnected = deviceConnected;
     if (!deviceConnected) {
       Serial.println("\n⚠ 设备断开，清理资源...");
       cleanupResources();
       needConnect = false;
       delay(200);
       isScanning = true;
       scanStartTime = millis();
       pBLEScan->start(0, false);
     } else {
       isScanning = false;
       lastDisplayedConnectedState = false;
     }
   }
   
   // 连接设备（最高优先级）
   if (needConnect && !deviceConnected && targetDeviceAddress != nullptr) {
     Serial.print(">>> 连接到: ");
     Serial.println(targetDeviceAddress->toString().c_str());
     
     if (pClient == nullptr) {
       pClient = BLEDevice::createClient();
       pClient->setClientCallbacks(new MyClientCallback());
     }
     
     pBLEScan->stop();
     isScanning = false;
     delay(150);
     
     bool connected = pClient->isConnected() || pClient->connect(*targetDeviceAddress);
     
     if (connected) {
       delay(500);
       if (pClient->isConnected()) {
         deviceConnected = true;
         needConnect = false;
         
         BLERemoteService* pRemoteService = pClient->getService(BLEUUID(HEART_RATE_SERVICE_UUID));
         if (pRemoteService != nullptr) {
           pRemoteCharacteristic = pRemoteService->getCharacteristic(BLEUUID(HEART_RATE_CHAR_UUID));
           if (pRemoteCharacteristic != nullptr && pRemoteCharacteristic->canNotify()) {
             pRemoteCharacteristic->registerForNotify(notifyCallback);
             Serial.println("✓ 已订阅心率通知\n");
             lastHeartRateTime = millis();
             heartRateValid = false;
           }
         }
       } else {
         connected = false;
       }
     }
     
     if (!connected) {
       Serial.println("✗ 连接失败，重新扫描");
       cleanupResources();
       delay(500);
       isScanning = true;
       scanStartTime = millis();
       pBLEScan->start(0, false);
     }
     
     return;
   }
   
   // 确保扫描持续进行
   if (!deviceConnected && !isScanning && !needConnect) {
     isScanning = true;
     scanStartTime = millis();
     pBLEScan->start(0, false);
   }
   
  // 按钮检测（使用millis()，非阻塞）
  checkButton();
  
  // OLED屏幕刷新（仅在非BLE繁忙时）
  bool isBLEBusy = isScanning || needConnect;
  if (displayReady && !isBLEBusy) {
    static unsigned long lastDisplayUpdate = 0;
    unsigned long displayInterval;
    
    // 根据显示模式选择刷新间隔
    if (displayMode == 0) {
      // 数字模式：使用较慢的刷新间隔
      displayInterval = deviceConnected ? DISPLAY_UPDATE_INTERVAL : DISPLAY_UPDATE_INTERVAL_SCAN;
    } else {
      // 波形模式：使用较快的刷新间隔，实现平滑扫描效果
      displayInterval = WAVEFORM_UPDATE_INTERVAL;
    }
    
    bool needRefresh = (deviceConnected != lastDisplayedConnectedState) ||
                       (displayMode == 0 && deviceConnected && heartRateValid && lastHeartRate != lastDisplayedHeartRate) ||
                       (displayMode == 1) ||  // 波形模式需要持续刷新
                       (millis() - lastDisplayUpdate >= displayInterval);
    
    if (needRefresh) {
      u8g2.clearBuffer();
      
      if (!deviceConnected) {
        // 未连接状态：显示扫描提示
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 10, "Heart Rate Monitor");
        u8g2.drawStr(0, 22, "Status: Scanning...");
        u8g2.drawStr(0, 36, "Connect band &");
        u8g2.drawStr(0, 48, "turn on HR");
        u8g2.drawStr(0, 60, "broadcast");
      } else if (displayMode == 0) {
        // 数字模式：显示大号字体心率数字
        if (isValidHeartRate()) {
          u8g2.setFont(u8g2_font_inb49_mf);
          char buf[8];
          snprintf(buf, sizeof(buf), "%u", (unsigned int)lastHeartRate);
          u8g2.drawStr((128 - strlen(buf) * 49) / 2, 57, buf);
          lastDisplayedHeartRate = lastHeartRate;
        } else {
          // 无心率：显示闪烁的感叹号
          if (millis() - lastWarningBlink >= WARNING_BLINK_INTERVAL) {
            warningVisible = !warningVisible;
            lastWarningBlink = millis();
          }
          if (warningVisible) {
            u8g2.setFont(u8g2_font_inb49_mf);
            u8g2.drawStr(20, 57, "!");
            u8g2.drawStr(50, 57, "!");
            u8g2.drawStr(80, 57, "!");
          }
        }
      } else {
        // 波形模式：绘制动态心电图
        drawECGWaveform();
      }
      
      u8g2.sendBuffer();
      lastDisplayUpdate = millis();
      lastDisplayedConnectedState = deviceConnected;
    }
  }
  
  // LED和蜂鸣器控制
  updateLEDAndBuzzer();
   
  // 绘图仪输出（仅在已连接且非BLE繁忙时）
  if (deviceConnected && !isBLEBusy && millis() - lastPlotterTime >= PLOTTER_UPDATE_INTERVAL) {
    // 检查心率超时
    if (heartRateValid && (millis() - lastHeartRateTime > HEART_RATE_TIMEOUT)) {
      heartRateValid = false;
      lastHeartRate = 0;
      lastDisplayedHeartRate = 0;
    }
    
    if (ENABLE_PLOTTER) {
      uint16_t currentHeartRate = heartRateValid ? lastHeartRate : 0;
      uint16_t plotValue = ECG_WAVEFORM_ENABLED ? getECGValue(currentHeartRate) : currentHeartRate;
      Serial.print(plotValue);
      Serial.print(",");
      Serial.println(currentHeartRate);
      addDataToBuffer(plotValue);
    }
    lastPlotterTime = millis();
  }
  
  // 扫描状态提示
  if (!deviceConnected && isScanning && !needConnect) {
    static unsigned long lastStatusTime = 0;
    if (millis() - lastStatusTime >= 3000) {
      Serial.print("\n>>> 扫描中... (");
      Serial.print((millis() - scanStartTime) / 1000);
      Serial.println(" 秒)");
      lastStatusTime = millis();
    }
  }
   
   yield();
 }
 
