# MimiClaw AGENTS.md

AI 助理运行在 $5 ESP32-S3 芯片上。纯 C，无 Linux，无 Node.js。

## Build Commands

```bash
# 激活 ESP-IDF 环境（必须先激活）
source /home/chao/.espressif/tools/activate_idf_v5.5.3.sh

# 编译
idf.py build

# 烧录并监控
idf.py -p /dev/ttyUSB0 flash monitor

# 清理重新编译
idf.py fullclean && idf.py build

# 重新配置
idf.py menuconfig
```

## Project Structure

```
main/
├── mimi.c              # 入口文件
├── mimi_config.h       # 配置常量（引脚、超时、缓冲区大小）
├── mimi_secrets.h      # 密钥（WiFi, API keys, 不提交）
├── bus/                # 消息总线
├── channels/           # 通信通道（Telegram, Feishu, QQ, DingTalk）
├── hardware/           # 硬件驱动（AHT10, DHT, ILI9341, MQTT）
├── tools/              # MCP 工具注册和实现
├── agent/              # Agent 循环和上下文构建
├── memory/             # SPIFFS 存储和会话管理
├── llm/                # LLM API 代理（Anthropic/OpenAI）
├── wifi/               # WiFi 管理
├── gateway/            # WebSocket 和 Webhook 服务器
├── cli/                # 串口命令行
└── skills/             # 动态技能加载
```

## Code Style

### 基本规则

- **语言**: 纯 C（ESP-IDF 风格）
- **命名**: `snake_case` 用于函数、变量、文件
- **静态变量**: `s_` 前缀（如 `s_mqtt_client`）
- **常量/宏**: `UPPER_SNAKE_CASE`，`MIMI_` 前缀
- **类型**: `typedef struct { ... } name_t;` 以 `_t` 结尾

### 头文件

```c
#pragma once  // 不用 #ifndef/#define/#endif

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
```

### 函数签名

```c
// 返回 esp_err_t，指针参数在前
esp_err_t sensor_read(sensor_t *sensor, float *value);

// 初始化函数命名
esp_err_t module_init(void);
esp_err_t module_start(void);
void module_stop(void);

// 检查函数
bool module_is_ready(void);
```

### 错误处理

```c
// 使用 ESP_ERROR_CHECK 检查初始化函数
ESP_ERROR_CHECK(nvs_flash_init());

// 函数内返回错误
if (!ptr) return ESP_ERR_INVALID_ARG;
if (ret != ESP_OK) return ret;

// 日志错误
ESP_LOGE(TAG, "Failed to init: %s", esp_err_to_name(ret));
```

### 日志

```c
static const char *TAG = "module";  // 每个文件定义

ESP_LOGI(TAG, "Started");           // 信息
ESP_LOGW(TAG, "Retry %d", count);   // 警告
ESP_LOGE(TAG, "Error: %s", msg);    // 错误
ESP_LOGD(TAG, "Debug: %d", value);  // 调试
```

### Imports 顺序

```c
#include "local_header.h"        // 本模块头文件
#include "other_local.h"         // 其他项目头文件

#include <stdlib.h>              // 标准库
#include <string.h>

#include "freertos/FreeRTOS.h"   // ESP-IDF 组件
#include "esp_log.h"
#include "driver/gpio.h"
```

### 配置（mimi_config.h）

```c
// 可覆盖的默认值
#ifndef MIMI_I2C_SDA_GPIO
#define MIMI_I2C_SDA_GPIO    GPIO_NUM_5
#endif

// 密钥从 mimi_secrets.h 读取
#ifndef MIMI_SECRET_WIFI_SSID
#define MIMI_SECRET_WIFI_SSID    ""
#endif
```

### 工具注册（tools/）

```c
static esp_err_t tool_execute(const char *input_json, char *output, size_t output_size);

mimi_tool_t my_tool = {
    .name = "tool_name",
    .description = "Tool description for LLM",
    .input_schema_json = "{\"type\":\"object\",\"properties\":{\"param\":{\"type\":\"string\"}}}",
    .execute = tool_execute,
};
register_tool(&my_tool);
```

## Hardware Drivers

新硬件驱动放在 `main/hardware/`：

```c
// hardware/xxx.h
#pragma once
#include "esp_err.h"

esp_err_t xxx_init(void);
esp_err_t xxx_read(xxx_data_t *data);
bool xxx_is_ready(void);
```

## MCP Tools

MCP 工具通过 `tool_registry` 注册，LLM 可调用。

现有工具：
- `web_search` - 网络搜索
- `get_current_time` - 获取当前时间
- `read_file` / `write_file` - 文件操作
- `led_control` - LED 控制
- `buzzer_control` - 蜂鸣器控制
- `sensor_read` - 温湿度传感器
- `mqtt_read_temp` / `mqtt_read_humidity` - MQTT 温湿度
- `tft_show` / `tft_notification` - TFT 显示

## Commit Style

```
feat: add MQTT temperature subscription
fix: correct SPI clock speed for ILI9341
docs: update README for build instructions
refactor: simplify tool registration
```

## Testing

项目无自动化测试。验证方式：
1. `idf.py build` 编译通过
2. 烧录到开发板验证功能
3. 串口监控输出 `idf.py monitor`

## Common Issues

| 问题 | 解决 |
|------|------|
| CMake generator 不匹配 | `rm -rf build/CMakeCache.txt build/CMakeFiles` |
| NVS 分区损坏 | 自动擦除重初始化 |
| PSRAM 分配失败 | 检查 `CONFIG_SPIRAM_*` 配置 |
| Python 环境不匹配 | `idf.py fullclean` 后重新编译 |