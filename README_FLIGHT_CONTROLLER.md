# ESP32-C3 Sony ZV-E10 藍牙相機控制器 (結合 iFlight BLITZ ATF435 飛控)

本文件記載透過 **ESP32-C3 SuperMini** 模擬 Sony 原廠藍牙遙控器（RMT-P1BT），並接收 **iFlight BLITZ ATF435 飛控** 觸發訊號進行相機「**拍照**」與「**錄影啟閉**」的完整實測架構、實體焊盤定義、通訊協議、接線、Betaflight CLI 設定與除錯指南。

---

## 1. 系統整體架構 (System Architecture)

```text
遙控器 (Transmitter: ELRS / TBS)
    │  [撥動觸發式開關 / Momentary Switch / AUX Channel]
    ▼ 
飛控 (iFlight BLITZ ATF435 / AT32F435G MCU)
    │  [PINIO 模式輸出: 3.3V 高電平脈衝 (Pulse)]
    ▼ (實體線路: 僅需 4 芯線，集中於 UART3 焊盤排)
ESP32-C3 SuperMini 開發板
    │  [偵測正緣脈衝 (Rising Edge) -> 發送 BLE 控制指令]
    ▼ (BLE 藍牙無線: Sony RMT-P1BT 協議)
Sony ZV-E10 (或 A7M3/M4, A7C/C2, A6700 等)
    └─> 執行: 快門拍照 (Photo) / 錄影啟閉 (Movie REC Toggle)
```

---

## 2. 實體驗證硬體資訊

- **微控制器**：ESP32-C3 SuperMini (RISC-V 160MHz, 4MB Flash, USB-CDC)
- **飛控板**：iFlight BLITZ ATF435 (MCU: Artery AT32F435G, 韌體: Betaflight 2025.12.0-beta)
- **目標相機**：Sony ZV-E10 (Bluetooth MAC: `d0:40:ef:d7:6c:54`)
- **藍牙協定棧**：ESP-IDF Bluedroid (`platform = espressif32 @ 6.9.0`)

---

## 3. 硬體接線定義 (只需 4 條線)

飛控上的 `UART 3` 焊盤排為相鄰的 4 個焊點（`5V`, `G`, `T3`, `R3`），完全不佔用現有的 DJI 數位圖傳 (`UART 1`)、GPS (`UART 4`) 與電調遙測 (`UART 6`)：

| 飛控焊點 (BLITZ ATF435) | MCU 腳位 | ESP32-C3 接腳 | 作用說明 |
| :--- | :--- | :--- | :--- |
| **5V** | 5V 穩壓 | **5V** (VIN) | 供應 ESP32-C3 5V 電源 |
| **G** (GND) | 地線 | **G** (GND) | 電源與訊號共地 |
| **T3** | `C10` | **GPIO 21** | **拍照（Photo）** 觸發訊號線 |
| **R3** | `C11` | **GPIO 20** | **錄影（Movie REC）** 觸發訊號線 |

> **電氣安全說明**：
> - AT32F435 的 GPIO 輸出為 3.3V 邏輯電位，ESP32-C3 輸入亦為 3.3V，可**安全直連**，無須電位轉換晶片。
> - ESP32-C3 內部已啟用 `INPUT_PULLDOWN` 下拉電阻，防範懸空浮接誤觸發。

---

## 4. Betaflight 飛控 CLI 設定指令

在 Betaflight Configurator 的 **CLI 命令列** 貼上以下指令並儲存：

```bash
# 1. 釋放 UART3 預設串口資源
resource SERIAL_TX 3 NONE
resource SERIAL_RX 3 NONE

# 2. 將 T3 與 R3 焊點重新映射為 PINIO (使用者開關)
resource PINIO 2 C10   # T3: 拍照 (Photo) -> USER2
resource PINIO 3 C11   # R3: 錄影 (Movie REC) -> USER3

# 3. 啟用 USER 模式開關 (保留 USER1 原廠電源開關)
set pinio_box = 40,41,42,255

save
```

### 遙控器 AUX 模式指派：
飛控重啟後，進入 Betaflight Configurator 的 **「模式 (Modes)」** 分頁：
- **`USER2`** $\rightarrow$ 對應 **拍照（Photo）**
- **`USER3`** $\rightarrow$ 對應 **錄影（Movie REC）**
- 點選「新增範圍」，撥動遙控器上的開關（支援彈回式觸發開關），將範圍涵蓋撥動時的數值即可儲存。

---

## 5. ESP32-C3 操作邏輯與按鍵定義

### 5.1 板載按鍵 (Buttons)
- **`BOOT` 鍵 (GPIO 9)**：
  - **點按一下放開** $\rightarrow$ 觸發 **拍照（Take Photo）**（執行半按對焦 120ms $\rightarrow$ 全按快門 120ms $\rightarrow$ 釋放，板載 LED 連閃 2 下）。
  - **按住滿 1 秒** $\rightarrow$ 觸發 **錄影開關（Movie REC Toggle）**（發送 REC 按下/釋放指令，板載 LED 連閃 3 下，再長按 1 秒即停止錄影）。
- **`RST` 鍵（常被誤認為 PST）**：
  - 此為硬體重開機鍵（CHIP_EN），按下去會強制將晶片重啟，不能由程式自訂。

### 5.2 觸發式開關（Momentary Pulse）優化機制
- 程式內建**正緣偵測（Rising Edge Trigger）**與軟體防彈跳（Debounce）：
  - 拍照防彈跳間隔：800ms
  - 錄影防彈跳間隔：1200ms
- 當遙控器撥桿撥下並自動彈回時，飛控只會產生一個瞬間的脈衝，ESP32 僅會響應**上升的那一下**，彈回時絕對不會誤動作或重複連發。

---

## 6. Sony 相機 BLE 通訊協定規範

### 6.1 GATT 服務與特徵值 (UUIDs)
- **Service UUID**：`8000FF00-FF00-FFFF-FFFF-FFFFFFFFFFFF`
- **Write Characteristic UUID**：`0000FF01-0000-1000-8000-00805f9b34fb`
- **Notify Characteristic UUID**：`0000FF02-0000-1000-8000-00805f9b34fb`

### 6.2 控制指令代碼 (Hex Commands)
- **拍照流程 (Photo Sequence)**：
  1. `0x01, 0x07`（Half-press / 半按對焦）
  2. `0x01, 0x09`（Full-press / 全按快門）
  3. `0x01, 0x08`（Hold / 保持合焦）
  4. `0x01, 0x06`（Release / 釋放快門）
- **錄影切換 (Movie Record Toggle)**：
  1. `0x01, 0x0f`（REC Button Down / 按下錄影）
  2. 延遲 200ms
  3. `0x01, 0x0e`（REC Button Up / 釋放錄影）

---

## 7. 重要除錯與踩坑紀錄 (Bug Fixes)

1. **NimBLE vs Bluedroid 握手問題**：
   - Arduino-ESP32 3.x 版預設使用 NimBLE，在 Sony 相機配對流程中無法正確調用 `esp_ble_gap_set_security_param` 註冊 SMP Bonding，導致相機無法跳出配對確認。
   - **解決方式**：固定使用 `platform = espressif32 @ 6.9.0` (Arduino-ESP32 2.0.17)，採用成熟的原生 Bluedroid 協定棧。
2. **藍牙位址型態 (Address Type) 遺失**：
   - Sony 相機廣播常採用 Random/Static 地址，若直接由純 MAC 連線會拋出 `BT_BTM: Device not found`。
   - **解決方式**：啟動 `BLEScan` 獲取包含 `getAddressType()` 的 `BLEAdvertisedDevice` 物件後再進行連線，連線成功率 100%。

---

## 8. 韌體源碼檔案索引

- 本機韌體原始碼：`H:\ESP32\src\main.cpp`
- PlatformIO 設定檔：`H:\ESP32\platformio.ini`
- 官方 PR 貢獻：已將連線修復提交至原開源作者倉庫（[PR #1](https://github.com/mybabysexy/sony_esp32_remote_controller/pull/1)）。
