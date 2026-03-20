# Draft: TFT 屏幕 + MQTT 温湿度功能

## 需求理解

用户想为 mimiclaw 项目添加：
1. **2.8寸 TFT 屏幕** - 显示信息
2. **MQTT 温湿度读取** - 通过 MQTT 订阅获取温湿度数据
3. **MCP Tools 能力** - 类似小智那边能通过 MCP 调用的能力

## 现有架构分析

### Mimiclaw 架构 (纯 C)

**飞书连接实现** (`main/channels/feishu/feishu_bot.c`):
- 使用 **WebSocket 长连接** 模式
- Protobuf 编码的帧格式
- 自动获取 tenant_access_token
- 消息去重机制
- 通过 `message_bus` 与 Agent Loop 通信

**Tools 机制** (`main/tools/`):
- `tool_registry.h/c` - 工具注册中心
- 每个工具定义 `mimi_tool_t` 结构：
  - `name` - 工具名
  - `description` - 描述
  - `input_schema_json` - JSON Schema
  - `execute` - 执行函数
- 现有工具：`web_search`, `get_current_time`, `read_file`, `write_file`, `led_control`, `buzzer_control`, `sensor_read`, `cron_add/list/remove`

**硬件外设** (`main/hardware/`):
- 已有：AHT10, DHT 温湿度传感器
- LED (WS2812), Buzzer

### 小智 ESP32 架构 (C++)

**MQTT 协议** (`main/protocols/mqtt_protocol.h/cc`):
- 使用 `mqtt` 组件
- 支持 UDP 音频通道
- AES 加密音频流

**MCP Server** (`main/mcp_server.h/cc`):
- C++ 实现，支持 JSON-RPC 2.0
- 工具定义：`McpTool` 类
- 属性定义：`Property`, `PropertyList`
- 支持 `self.xxx` 格式的工具名

**TFT 屏幕** (`main/display/`):
- LVGL 驱动
- `LcdDisplay` 基类
- 支持 SPI/RGB/MIPI 接口

## 技术决策

### 1. MQTT 客户端

**选项 A**: 使用 ESP-IDF 的 `esp_mqtt` 组件
- 纯 C 实现，与 mimiclaw 风格一致
- 官方支持，稳定可靠

**选项 B**: 移植小智的 MQTT 实现
- C++ 实现，需要适配
- 功能更完整

**推荐**: 选项 A - 使用 ESP-IDF esp_mqtt

### 2. TFT 屏幕驱动

**选项 A**: 使用 LVGL + ESP-IDF LCD 组件
- 功能强大，UI 丰富
- 内存占用较大

**选项 B**: 使用 TFT_eSPI 或简单的 SPI 驱动
- 轻量级
- 需要自己实现 UI

**推荐**: 根据屏幕分辨率和内存选择

### 3. MCP Tools 扩展

在 mimiclaw 的 `tool_registry` 中添加新工具：
- `mqtt_subscribe` - 订阅 MQTT 主题
- `mqtt_publish` - 发布 MQTT 消息
- `display_show` - 在 TFT 上显示内容
- `get_temperature` - 获取温度（本地或 MQTT）
- `get_humidity` - 获取湿度（本地或 MQTT）

## 已确认需求

1. **TFT 屏幕**:
   - 型号: **ILI9341** SPI 接口
   - 分辨率: 240x320
   - 无触摸功能
   - 显示内容: 温湿度 + 设备状态 + 消息通知

2. **MQTT Broker**:
   - 服务器: **阿里云 IoT**
   - 认证: **设备证书** (一机一密)
   - 数据格式: 阿里云属性格式 `{"temperature": 25.5, "humidity": 60}`

3. **温湿度数据来源**:
   - **已有设备**通过 MQTT 发布
   - 本设备订阅并显示

4. **MCP Tools 能力**:
   - ✅ 读取温湿度
   - ✅ 控制 TFT 显示
   - ✅ 发布 MQTT 消息
   - ✅ 订阅 MQTT 消息

5. **消息通知**:
   - 飞书/Telegram 新消息时屏幕显示通知
   - 无蜂鸣器提示

6. **历史数据**:
   - 仅显示当前值，无需历史曲线

7. **开发板**:
   - 其他 ESP32-S3 板
   - 引脚需要可配置

## 参考文件

### Mimiclaw
- `main/channels/feishu/feishu_bot.c` - WebSocket 连接模式
- `main/tools/tool_registry.c` - 工具注册
- `main/tools/tool_hardware.c` - 硬件工具实现
- `main/hardware/aht10.c` - 温湿度传感器驱动
- `main/mimi_config.h` - 配置定义

### 小智 ESP32
- `main/protocols/mqtt_protocol.cc` - MQTT 实现
- `main/mcp_server.cc` - MCP 服务
- `main/display/lcd_display.h` - TFT 显示
- `main/boards/xiao-esp32s3-sense/config.h` - 板子配置