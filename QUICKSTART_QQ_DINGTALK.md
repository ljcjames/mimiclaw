# MimiClaw QQ & DingTalk 集成快速指南

## 📋 概述

MimiClaw 现已支持 QQ 和钉钉 机器人接入，可以通过这些平台与 ESP32 AI 助手进行交互。

## ✨ 功能特性

### QQ Bot
- ✅ 接收群聊消息
- ✅ 发送文本回复
- ✅ 自动消息分块（4096字符）
- ✅ 消息去重
- ✅ URL验证支持

### DingTalk Bot
- ✅ 接收群聊和私聊消息
- ✅ 发送文本回复
- ✅ 自动消息分块（4096字符）
- ✅ 消息去重
- ✅ 自动Token刷新
- ✅ URL验证支持

## 🚀 快速开始

### 1. 配置凭证

#### 方式 A: 编译时配置（推荐）

编辑 `main/mimi_secrets.h`:

```c
#define MIMI_SECRET_QQ_APP_ID     "your_app_id"
#define MIMI_SECRET_QQ_TOKEN      "your_bot_token"
#define MIMI_SECRET_DINGTALK_APP_KEY     "your_app_key"
#define MIMI_SECRET_DINGTALK_APP_SECRET  "your_app_secret"
```

#### 方式 B: 运行时配置（CLI）

```bash
# 连接到ESP32 CLI
mimi> set_qq_creds <APP_ID> <TOKEN>
mimi> set_dingtalk_creds <APP_KEY> <APP_SECRET>
```

### 2. 获取机器人凭证

#### QQ Bot
1. 访问 [QQ Bot 平台](https://bot.q.qq.com/)
2. 创建机器人
3. 获取 **App ID** 和 **Token**
4. 启用权限：`Send and receive messages`
5. 配置事件订阅：
   - URL: `http://<ESP32_IP>:18791/qq/events`
   - 订阅事件：`group_at_msg`

#### 钉钉 Bot
1. 访问 [钉钉开发者平台](https://open.dingtalk.com/)
2. 创建自定义机器人
3. 获取 **App Key** 和 **App Secret**
4. 启用权限：`Send messages`, `Receive messages`
5. 配置事件订阅：
   - URL: `http://<ESP32_IP>:18792/dingtalk/events`
   - 订阅事件：`message.p2p`, `message.group`

### 3. 编译和烧录

```bash
cd /home/chao/Develop/mimiclaw
idf.py build
idf.py flash monitor
```

### 4. 配置 Webhook

烧录完成后，在 QQ/DingTalk 机器人设置中配置 Webhook URL：

- **QQ**: `http://<ESP32_IP>:18791/qq/events`
- **钉钉**: `http://<ESP32_IP>:18792/dingtalk/events`

## 📡 端口说明

| 服务 | 端口 | 用途 |
|------|------|------|
| WebSocket | 18790 | WebSocket 通信 |
| QQ Webhook | 18791 | QQ 机器人事件接收 |
| 钉钉 Webhook | 18792 | 钉钉机器人事件接收 |

## 🔄 消息流程

```
用户发送消息
    ↓
[QQ/DingTalk Webhook Server]
    ↓
[消息解析 & 去重]
    ↓
[消息总线 - 入站]
    ↓
[AI Agent 处理]
    ↓
[消息总线 - 出站]
    ↓
[QQ/DingTalk Bot - 发送回复]
    ↓
用户收到回复
```

## 📝 CLI 命令

### QQ Bot 命令

```bash
# 设置凭证
mimi> set_qq_creds <APP_ID> <TOKEN>

# 查看凭证状态
mimi> get_qq_creds

# 测试发送消息
mimi> test_qq_send <GROUP_ID> "Hello"
```

### 钉钉 Bot 命令

```bash
# 设置凭证
mimi> set_dingtalk_creds <APP_KEY> <APP_SECRET>

# 查看凭证状态
mimi> get_dingtalk_creds

# 测试发送消息
mimi> test_dingtalk_send <CHAT_ID> "Hello"
```

## 🔧 配置选项

### mimi_config.h 配置项

```c
/* QQ Bot */
#define MIMI_QQ_MAX_MSG_LEN              4096
#define MIMI_QQ_WEBHOOK_PORT             18791
#define MIMI_QQ_WEBHOOK_PATH             "/qq/events"

/* DingTalk Bot */
#define MIMI_DINGTALK_MAX_MSG_LEN        4096
#define MIMI_DINGTALK_WEBHOOK_PORT       18792
#define MIMI_DINGTALK_WEBHOOK_PATH       "/dingtalk/events"

/* Webhook Server */
#define MIMI_WEBHOOK_PORT                18790
```

## 🐛 故障排查

### QQ Bot 问题

**问题**: 消息无法接收
- 检查 Webhook URL 是否正确配置
- 确认 ESP32 IP 地址可访问
- 查看日志中的 `qq_webhook` 标签

**问题**: 消息无法发送
- 验证 App ID 和 Token 是否正确
- 确认群组 ID 是否正确
- 检查网络连接

### 钉钉 Bot 问题

**问题**: Token 刷新失败
- 验证 App Key 和 App Secret
- 检查网络连接
- 查看日志中的 `dingtalk` 标签

**问题**: 消息无法接收
- 确认 Webhook URL 配置正确
- 检查签名验证是否通过
- 查看日志中的 `dingtalk_webhook` 标签

## 📚 API 参考

### QQ Bot API

```c
// 初始化
esp_err_t qq_bot_init(void);

// 启动 Webhook 服务器
esp_err_t qq_bot_start(void);

// 发送消息到群组
esp_err_t qq_send_message(const char *chat_id, const char *text);

// 设置凭证
esp_err_t qq_set_credentials(const char *app_id, const char *token);
```

### 钉钉 Bot API

```c
// 初始化
esp_err_t dingtalk_bot_init(void);

// 启动 Webhook 服务器
esp_err_t dingtalk_bot_start(void);

// 发送消息到聊天
esp_err_t dingtalk_send_message(const char *chat_id, const char *text);

// 设置凭证
esp_err_t dingtalk_set_credentials(const char *app_key, const char *app_secret);
```

## 📂 文件结构

```
main/
├── channels/
│   ├── qq/
│   │   ├── qq_bot.h
│   │   ├── qq_bot.c
│   │   └── README.md
│   ├── dingtalk/
│   │   ├── dingtalk_bot.h
│   │   ├── dingtalk_bot.c
│   │   └── README.md
│   ├── telegram/
│   │   └── ...
│   └── feishu/
│       └── ...
├── gateway/
│   ├── ws_server.c
│   ├── ws_server.h
│   ├── webhook_server.c
│   └── webhook_server.h
├── mimi.c
├── mimi_config.h
└── CMakeLists.txt
```

## 🎯 下一步

1. **测试基本功能**: 发送消息并接收回复
2. **配置 AI 模型**: 设置 LLM 提供商和 API Key
3. **添加技能**: 安装自定义技能
4. **优化配置**: 调整消息长度、超时等参数

## 📖 相关文档

- [MimiClaw 主文档](../README.md)
- [QQ Bot 详细文档](channels/qq/README.md)
- [钉钉 Bot 详细文档](channels/dingtalk/README.md)
- [集成总结](channels/README_QQ_DINGTALK.md)

## 💡 提示

- 消息会自动分块，无需手动处理长消息
- 系统会自动去重，避免重复处理
- 钉钉 Token 会自动刷新，无需手动管理
- QQ 仅支持群聊，不支持私聊

## 🆘 获取帮助

- 查看 ESP32 日志: `idf.py monitor`
- 检查网络连接: `ping <ESP32_IP>`
- 验证 Webhook URL: `curl -X POST http://<ESP32_IP>:18791/qq/events -d '{}'`

---

**祝使用愉快！** 🎉
