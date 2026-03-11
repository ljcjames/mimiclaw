# DingTalk Bot Integration

This directory contains the DingTalk bot integration for MimiClaw.

## Features

- Send text messages to DingTalk chats
- Receive messages via webhook (HTTP event subscription)
- Automatic message chunking (4096 chars per message)
- Message deduplication
- Support for both group and chat chats
- Automatic access token refresh

## Configuration

### Option 1: Build-time Configuration

1. Copy the secrets template:
```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

2. Edit `main/mimi_secrets.h`:
```c
#define MIMI_SECRET_DINGTALK_APP_KEY     "your_app_key"
#define MIMI_SECRET_DINGTALK_APP_SECRET  "your_app_secret"
```

3. Rebuild:
```bash
idf.py fullclean && idf.py build
```

### Option 2: Runtime Configuration (CLI)

```
mimi> set_dingtalk_creds <APP_KEY> <APP_SECRET>
```

## DingTalk Bot Setup

1. Go to [DingTalk Developer Platform](https://open.dingtalk.com/)
2. Create a custom bot and get **App Key** and **App Secret**
3. Enable permissions:
   - `Send messages`
   - `Receive messages`
4. Configure Event Subscription:
   - Request URL: `http://<ESP32_IP>:18792/dingtalk/events`
   - Subscribe to: `message.p2p`, `message.group`
5. The ESP32 will auto-respond to the URL verification challenge

## Architecture

```
DingTalk Server
    |
    v  (HTTP POST /dingtalk/events)
[ESP32 Webhook Server :18792]
    |
    v  (message_bus_push_inbound)
[Message Bus] --> [Agent Loop] --> [Message Bus]
    |                                    |
    v  (outbound dispatch)               |
[dingtalk_send_message] <-----------------+
    |
    v  (POST /robot/send?access_token=xxx)
DingTalk API
```

## API Reference

| Function | Description |
|----------|-------------|
| `dingtalk_bot_init()` | Load credentials from NVS/build-time |
| `dingtalk_bot_start()` | Start webhook HTTP server |
| `dingtalk_send_message(chat_id, text)` | Send text message to chat |
| `dingtalk_set_credentials(app_key, app_secret)` | Save credentials to NVS |

## Notes

- Messages are automatically chunked if they exceed 4096 characters
- Message deduplication prevents processing the same message multiple times
- Access token is automatically refreshed before expiration
- Webhook server needs to be implemented (currently a placeholder task)

## References

- [DingTalk Developer Platform](https://open.dingtalk.com/)
- [DingTalk Robot API Documentation](https://open.dingtalk.com/document/robots/custom-robot-access)
