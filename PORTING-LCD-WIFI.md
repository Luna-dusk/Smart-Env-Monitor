# 从 AI-Safety-Inspection 移植 WIFI + LCD 模块

移植日期：本次会话 · 源工程：`/home/violet/esp/AI-Safety-Inspection` · 目标：本工程
构建验证：`idf.py build` 从零构建通过（ESP-IDF v5.4 / esp32s3），
`Smart-Env-Monitor.bin` = 0x1A3330（1.72 MB），app 分区 4 MB，余 59%。

---

## 1. 拷了什么

| 源路径 | 目标路径 | 说明 |
|---|---|---|
| `components/BSP/WIFI/` | `components/BSP/WIFI/` | `wifi.c/.h`、`wifi_provision.c/.h`、`portal.html`、`README.md`，**代码零改动** |
| `components/BSP/LCD/` | `components/BSP/LCD/` | 移植层 `lcd_ui.c/.h`、`lv_mem_psram.h`、主题/控件/两屏、三套中文字库、`README.md`。逻辑未改，仅引脚（见第 3 节） |

两个模块之间没有相互依赖：WIFI 只依赖 `LED_Driver`，LCD 只依赖 LVGL + `esp_lcd`。

## 2. 改了哪些构建/配置（不改就跑不起来）

| 文件 | 改动 |
|---|---|
| `components/BSP/CMakeLists.txt` | `src_dirs`/`include_dirs` 加 `WIFI`、`LCD`；`requires` 加 `nixy4__u8g2`（原本漏了，OLED 编不过）、`nvs_flash`、`esp_netif`、`esp_wifi`、`esp_event`、`esp_http_server`、`lwip`、`esp_timer`、`esp_lcd`、`lvgl__lvgl`；加 `EMBED_TXTFILES "WIFI/portal.html"` |
| `CMakeLists.txt`（根） | `project()` 之前加 `include_directories(components/BSP/LCD)` + `add_compile_definitions(LV_MEM_CUSTOM_ALLOC/FREE/REALLOC=lv_mem_psram_*)`，让 LVGL 内存池走 PSRAM |
| `main/idf_component.yml` | 加 `lvgl/lvgl: "~8.3.0"`（实际拉到 8.3.11） |
| `sdkconfig` / `sdkconfig.defaults` | `CONFIG_LV_COLOR_16_SWAP=y`、`LV_FONT_MONTSERRAT_16/20=y`、`LV_FONT_FMT_TXT_LARGE=y`（全量字库要）、`LV_MEM_CUSTOM=y` + `LV_MEM_CUSTOM_INCLUDE="lv_mem_psram.h"` |
| `partitions-16MiB.csv` | factory `0x1F0000` → `0x400000`（1.9 MB → 4 MB），vfs `0xA00000` → `0x7F0000`；不放大装不下 16px 全量字库 |
| `components/Middlewares/` | 新建空目录（根 CMakeLists 的 `EXTRA_COMPONENT_DIRS` 指向它，缺目录 CMake 直接报错） |

## 3. LCD 引脚（唯一动过的模块代码）

原板 LCD 占 GPIO9~14，本工程 **GPIO10/11 已被 I2C（OLED）占用**
（`components/BSP/I2C/iic.h`：SDA=11、SCL=10），所以整体后移：

| 信号 | 原工程 | 本工程 |
|---|---|---|
| SCLK（模块丝印 SCL） | GPIO9 | **GPIO12** |
| MOSI（模块丝印 SDA） | GPIO10 | **GPIO13** |
| RST | GPIO11 | **GPIO14** |
| DC | GPIO12 | **GPIO15** |
| CS | GPIO13 | **GPIO16** |
| BLK 背光 | GPIO14 | **GPIO17** |

`lcd_ui.c` 里这些宏以及 `LCD_SPI_HOST` / `LCD_PIXEL_CLK` / `LCD_RGB_ORDER` /
`LCD_INVERT_COLOR` / `LCD_BLK_ACTIVE_HIGH` 全部用 `#ifndef` 包住，换板时用
`add_compile_definitions(LCD_PIN_DC=GPIO_NUM_x ...)` 覆盖即可，不用改文件。

## 4. main.c 的接线

```c
lcd_ui_init();                                   // SPI2 + esp_lcd ST7789 + LVGL + 主屏
xTaskCreate(lcd_ui_task, "lcd_ui", 8192, NULL, 8, NULL);   // 所有 lv_* 只在这个任务里跑
lcd_ui_set_status(LCD_STATUS_CONNECTING);
esp_create_wifi_task();                          // 独立任务连接，不阻塞启动
xTaskCreate(wifi_status_task, ...);              // 等到 WIFI_CONNECTED_BIT → 状态栏切"监护中"
xTaskCreate(boot_key_task, ...);                 // BOOT(GPIO0) 长按 3s → wifi_provision_request()
```

- **配网触发**：源工程的 BOOT 长按在 `BSP/KEY`，本工程没有按键模块，
  因此在 `main.c` 里用 20ms 轮询单独实现，只调 `wifi_provision_request()` 一个接口
  （真正的 WiFi 模式切换仍在 wifi 任务里，避免多任务改驱动状态）。
- 首次上电 NVS 无凭证 → 自动开热点 `ESP配网服务` → 手机连上后打开 `http://192.168.4.1`。
- 原来 `app_main` 里的 `LED_TOGGLE()` 心跳循环已删：LED 现在归配网模块用
  （配网中快闪、联网成功常亮），两个任务抢同一 GPIO 会互相打架。

## 5. 顺手修掉的两个原有编译错误

这两个错误和移植无关，但会让本工程**根本编译不过**（之前没构建过）：

1. `components/BSP/I2C/iic.h`：`#include "FreeRTOS/task.h"` → `"freertos/task.h"`
   （IDF5 没有前一个路径）。
2. `components/BSP/OLED/oled.h`：`#include "u8g2_port_esp32_hw_i2c.h"` →
   `"esp32_hw_i2c.h"`（nixy4/u8g2 0.1.4 里没有前者）；另外 `main.c` 的
   `"UART.h"` → `"uart.h"`（文件名是小写，Linux 下大小写敏感）。

## 6. 业务侧怎么用

```c
#include "lcd_ui.h"     // 业务层只认这个，不要 include lvgl.h

lcd_ui_set_status(LCD_STATUS_LISTENING);   // 任意任务、任意时刻，下一帧生效
lcd_ui_set_emotion(LCD_EMO_SMILE);
lcd_ui_chat_add(LCD_CHAT_AI, "室内温度 26.3℃");   // 想显示文本就入队
lcd_ui_chat_show();                        // 切到对话屏
```

```c
#include "wifi.h"

EventGroupHandle_t ev = wifi_get_event_group();
if (ev && (xEventGroupGetBits(ev) & WIFI_CONNECTED_BIT)) { /* 可以发网络请求了 */ }
```

## 7. 上板前要自己确认的三件事

1. **屏是不是同一块面板**：`LCD_RGB_ORDER`（当前 RGB）与 `LCD_INVERT_COLOR`（当前 1）
   是原板实测值。红蓝反了改 RGB 序，颜色像负片把 `LCD_INVERT_COLOR` 设 0。
2. **GPIO12~17 在你板上是否空着**：确认没接其他外设（尤其 12/13 某些板子接 JTAG/传感器）。
   花屏先降 `LCD_PIXEL_CLK` 到 20MHz。
3. **PSRAM 必须是 8MB Octal**（`CONFIG_SPIRAM_MODE_OCT`）：LVGL 对象池在 PSRAM 上跑；
   没有 PSRAM 时 `lv_mem_psram.h` 会退回内部 RAM，但内部 RAM 会很紧。

## 8. 文档

- `components/BSP/LCD/README.md`：LCD 移植层原理、五个"为什么"、排障表（原工程原文 + 顶部加了移植差异表）
- `components/BSP/WIFI/README.md`：配网流程图、已知限制（原工程原文 + 顶部加了触发方式差异）