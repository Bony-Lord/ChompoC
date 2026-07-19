# LangJam Chat

Сервер и клиент:

```text
langjam/Chompo/chat_server.chmp
langjam/Chompo/chat_client.chmp
```

Оба приложения написаны на Chompo. C++ runtime даёт TCP, terminal UI и secure channel (AES-256-GCM + PBKDF2).

## Secure mode (по умолчанию)

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 127.0.0.1 4040 50 'your-long-password'
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040 'your-long-password'
```

Аргументы сервера:

1. bind host (`0.0.0.0`);
2. port (`4040`; `0` = эфемерный);
3. history limit (`50`);
4. **room password** (минимум 12 байт).

Аргументы клиента: host, port, password (если нет — скрытый prompt).

Сервер печатает:

```text
LISTENING 54321 SECURE AES-256-GCM
```

Пароль **не** передаётся как строка приложения: только PBKDF2 на handshake. Автоматический downgrade в plaintext **запрещён**.

### Explicit plaintext

```bash
./build/Chompo langjam/Chompo/chat_server.chmp --plaintext 127.0.0.1 4040 50
./build/Chompo langjam/Chompo/chat_client.chmp --plaintext 127.0.0.1 4040
```

## Возможности

- AES-256-GCM (Windows bcrypt / Linux OpenSSL);
- неблокирующий secure handshake на сервере (`pending` + deadline);
- terminal UI, скрытый ввод пароля;
- комнаты и отдельная история (`/rooms`, `/room`, `/join`);
- роли admin/member (первый зарегистрированный = admin; demo-модель);
- `/kick`, `/ban`, `/unban`, `/bans`, `/whitelist` (admin);
- локальный `/mute` / `/unmute` / `/mutes`;
- `/status`, `/nick`, `/me`, `/msg`, `/ping`;
- server console: `/say`, `/kick`, `/stop`;
- username: UTF-8 (в т.ч. кириллица), до 48 байт; без control-символов и `:`;
- sanitization удалённого текста (C0 / DEL / ESC; UTF-8 сохраняется).

## Протокол (упрощённо)

После secure handshake сервер:

```text
NAME choose a unique name
```

Успех:

```text
OK NAME Alice
ROLE admin
ROOM lobby
HISTORY N
...
END
```

Ошибка имени (сокет остаётся открытым — клиент повторяет ввод):

```text
ERROR NAME is already in use
```

Системные и chat-строки:

```text
* Alice joined #lobby as admin
Alice: hello
[DM from Alice] secret
KICKED by server
BYE
```

## Тесты

```bash
ctest --test-dir build -R langjam_chat --output-on-failure
```

`tests/chat_smoke.py` покрывает пароль, retry имени, rooms, ban/whitelist, mute, hung handshake и отсутствие plaintext downgrade.
