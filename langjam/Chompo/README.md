# Chompo — LangJam submission

Chompo is a dynamically typed language implemented by a C++23 interpreter. The chat server and client in this directory are written entirely in Chompo.

## Security model

**Default mode is encrypted.** All chat traffic uses AES-256-GCM with a PBKDF2-derived key from a shared room password. The password is never sent over the wire after the secure handshake; it is used only for key derivation.

| Platform | Crypto backend |
|---|---|
| Windows | CNG / bcrypt |
| Linux | OpenSSL `libcrypto` |

Supported platforms for the secure chat build: **Windows and Linux**.

Password requirements:

- Minimum length: **12 bytes**
- Shared-password designs are guessable offline from a captured handshake; use a long random secret for real use
- There is **no automatic fallback** from secure to plaintext if the handshake fails

Explicit plaintext mode (local demos only):

```bash
./build/Chompo langjam/Chompo/chat_server.chmp --plaintext 127.0.0.1 4040 50
./build/Chompo langjam/Chompo/chat_client.chmp --plaintext 127.0.0.1 4040
```

## Build

From the repository root:

```bash
cmake -S . -B build && cmake --build build --parallel
```

On Windows with a multi-config generator, use `build\Debug\Chompo.exe` (or Release) instead of `./build/Chompo`.

## Launch (secure default)

Terminal 1 — server (host, port, history limit, **password**):

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 127.0.0.1 4040 50 'your-long-password'
```

Terminal 2+ — client (host, port, password optional; if omitted, the client prompts with hidden input):

```bash
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040 'your-long-password'
```

## Features

- Secure transport (AES-256-GCM) with non-blocking server handshake
- Interactive terminal UI (hidden password, editable UTF-8 input)
- Rooms with per-room history (`/rooms`, `/room`, `/join`)
- Roles: first registered user becomes **admin** (demo model; not production-safe)
- Admin moderation: `/kick`, `/ban`, `/unban`, `/bans`, `/whitelist`
- Local mute on the client: `/mute`, `/unmute`, `/mutes`
- Statuses, DMs, nick changes, server console (`/say`, `/kick`, `/stop`)
- UTF-8 nicknames and messages (including Cyrillic); names up to 48 bytes, no controls or `:`
- Remote text sanitization (control characters / ESC stripped; UTF-8 preserved)

## Client commands

```text
/help
/history
/users
/rooms
/room
/join <room>
/status [online|away|busy|dnd]
/nick <name>
/me <action>
/msg <name> <message>
/ping
/kick <name>          (admin)
/ban <name>           (admin)
/unban <name>         (admin)
/bans                 (admin)
/whitelist on|off|add|remove|list   (admin)
/mute <name>          (local)
/unmute <name>        (local)
/mutes                (local)
/clear
/quit
```

## Server console commands

```text
/help
/users
/say <text>
/kick <name>
/clear
/stop
```

## Files

- `chat_server.chmp` — multi-user server
- `chat_client.chmp` — interactive client
- `LANGUAGE.md` — short language and runtime description

## Tests

The automated `langjam_chat` CTest runs an integration suite (`tests/chat_smoke.py`) against a real server and clients, covering secure handshake, wrong password, name retry, rooms, moderation, mute, hung-handshake non-blocking behaviour, and no plaintext downgrade.
