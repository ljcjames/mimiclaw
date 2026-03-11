# QQ Bot Integration

This directory contains the QQ bot integration for MimiClaw.

## Features

- Send text messages to QQ groups
- Receive messages via webhook (HTTP event subscription)
- Automatic message chunking (4096 chars per message)
- Message deduplication
- Support for group chats only

## Configuration

### Option 1: Build-time Configuration

1. Copy the secrets template:
```bash
cp main/mimi_secrets.h.example main/mimi_secrets.h
```

2. Edit `main/mimi_secrets.h`:
```c
#define MIMI_SECRET_QQ_APP_ID     "your_app_id"
#define MIMI_SECRET_QQ_TOKEN      "your_bot_token"
```

3. Rebuild:
```bash
idf.py fullclean && idf.py build
```

### Option 2: Runtime Configuration (CLI)

```
mimi> set_qq_creds <APP_ID> <TOKEN>
```

## QQ Bot Setup

1. Go to [QQ Bot Platform](https://bot.q.qq.com/)
2. Create a bot and get **App ID** and **Token**
3. Enable permissions:
   - `Send and receive messages`
4. Configure Event Subscription:
   - Request URL: `http://<ESP32_IP>:18791/qq/events`
   - Subscribe to: `group_at_msg` (for group mentions)
5. The ESP32 will auto-respond to the URL verification challenge

## Architecture

```
QQ Server
    |
    v  (HTTP POST /qq/events)
[ESP32 Webhook Server :18791]
    |
    v  (message_bus_push_inbound)
[Message Bus] --> [Agent Loop] --> [Message Bus]
    |                                    |
    v  (outbound dispatch)               |
[qq_send_message] <-----------------+
    |
    v  (POST /v2/groups/{group_id}/messages)
QQ API
```

## API Reference

| Function | Description |
|----------|-------------|
| `qq_bot_init()` | Load credentials from NVS/build-time |
| `qq_bot_start()` | Start webhook HTTP server |
| `qq_send_message(chat_id, text)` | Send text message to group |
| `qq_set_credentials(app_id, token)` | Save credentials to NVS |

## Notes

- QQ bot currently supports **group chats only** (not DMs)
- Messages are automatically chunked if they exceed 4096 characters
- Message deduplication prevents processing the same message multiple times
- Webhook server needs to be implemented (currently a placeholder task)

## References

- [QQ Bot Platform](https://bot.q.qq.com/)
- [QQ Bot API Documentation](https://bot.q.qq.com/docs/1-0/developer-reference/introduction)
