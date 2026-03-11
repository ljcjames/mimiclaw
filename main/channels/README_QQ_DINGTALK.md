# QQ and DingTalk Integration for MimiClaw

## Overview

This document describes the QQ and DingTalk bot integrations added to MimiClaw.

## What Was Added

### 1. QQ Bot Integration
- **Location**: `main/channels/qq/`
- **Files**:
  - `qq_bot.h` - Header file with API declarations
  - `qq_bot.c` - Implementation file
  - `README.md` - Usage documentation

### 2. DingTalk Bot Integration
- **Location**: `main/channels/dingtalk/`
- **Files**:
  - `dingtalk_bot.h` - Header file with API declarations
  - `dingtalk_bot.c` - Implementation file
  - `README.md` - Usage documentation

### 3. Configuration Updates
- Updated `main/mimi_config.h` with QQ and DingTalk configuration
- Updated `main/bus/message_bus.h` with new channel identifiers
- Updated `main/CMakeLists.txt` to include new source files
- Updated `main/mimi.c` to initialize and route messages for QQ and DingTalk

## Features Implemented

### QQ Bot
- ✅ Send text messages to QQ groups
- ✅ Message deduplication
- ✅ Automatic message chunking (4096 chars)
- ✅ Credentials storage in NVS
- ⏳ Webhook server (placeholder task)
- ⏳ Message parsing (placeholder task)

### DingTalk Bot
- ✅ Send text messages to DingTalk chats
- ✅ Automatic access token refresh
- ✅ Message deduplication
- ✅ Automatic message chunking (4096 chars)
- ✅ Credentials storage in NVS
- ⏳ Webhook server (placeholder task)
- ⏳ Message parsing (placeholder task)

## How to Use

### Step 1: Configure Credentials

**Option A: Build-time (in mimi_secrets.h)**
```c
#define MIMI_SECRET_QQ_APP_ID     "your_app_id"
#define MIMI_SECRET_QQ_TOKEN      "your_bot_token"
#define MIMI_SECRET_DINGTALK_APP_KEY     "your_app_key"
#define MIMI_SECRET_DINGTALK_APP_SECRET  "your_app_secret"
```

**Option B: Runtime (via CLI)**
```
mimi> set_qq_creds <APP_ID> <TOKEN>
mimi> set_dingtalk_creds <APP_KEY> <APP_SECRET>
```

### Step 2: Build and Flash

```bash
cd /home/chao/Develop/mimiclaw
idf.py build
idf.py flash monitor
```

### Step 3: Configure Webhooks

After flashing, configure the webhook URLs in your QQ/DingTalk bot settings:

- **QQ**: `http://<ESP32_IP>:18791/qq/events`
- **DingTalk**: `http://<ESP32_IP>:18792/dingtalk/events`

### Step 4: Test

Send a message to your bot in QQ/DingTalk, and it should respond via the AI agent.

## Message Flow

```
User Message
    ↓
[QQ/DingTalk Webhook Server]
    ↓
[Message Bus - Inbound]
    ↓
[Agent Loop - AI Processing]
    ↓
[Message Bus - Outbound]
    ↓
[QQ/DingTalk Bot - Send Message]
    ↓
User Response
```

## Channel Identifiers

The following channel identifiers are now available:

- `telegram` - Telegram bot
- `feishu` - Feishu/Lark bot
- `qq` - QQ bot (NEW)
- `dingtalk` - DingTalk bot (NEW)
- `websocket` - WebSocket gateway
- `cli` - Serial CLI
- `system` - System messages

## API Reference

### QQ Bot

```c
// Initialize
esp_err_t qq_bot_init(void);

// Start webhook server
esp_err_t qq_bot_start(void);

// Send message to group
esp_err_t qq_send_message(const char *chat_id, const char *text);

// Set credentials
esp_err_t qq_set_credentials(const char *app_id, const char *token);
```

### DingTalk Bot

```c
// Initialize
esp_err_t dingtalk_bot_init(void);

// Start webhook server
esp_err_t dingtalk_bot_start(void);

// Send message to chat
esp_err_t dingtalk_send_message(const char *chat_id, const char *text);

// Set credentials
esp_err_t dingtalk_set_credentials(const char *app_key, const char *app_secret);
```

## Next Steps

### To Complete the Implementation

1. **Implement Webhook Server**:
   - Add HTTP server for receiving webhook events
   - Parse incoming JSON messages
   - Push messages to the inbound message bus

2. **Add Message Parsing**:
   - Extract text content from webhook payloads
   - Handle different message types (text, images, etc.)
   - Support mentions and commands

3. **Add Webhook Verification**:
   - Handle URL verification challenges
   - Implement secure webhook authentication

4. **Testing**:
   - Test with real QQ/DingTalk bots
   - Verify message routing
   - Test message chunking
   - Test deduplication

## Troubleshooting

### QQ Bot Issues

- **Message not received**: Check webhook URL configuration
- **Message not sent**: Verify bot token and group ID
- **Connection errors**: Check network connectivity

### DingTalk Bot Issues

- **Token refresh errors**: Verify app key and secret
- **Message not received**: Check webhook URL and permissions
- **Access denied**: Verify bot permissions in DingTalk

## References

- [QQ Bot Platform](https://bot.q.qq.com/)
- [DingTalk Developer Platform](https://open.dingtalk.com/)
- [MimiClaw Documentation](../README.md)
