# Chompo — LangJam submission

Chompo is a dynamically typed language implemented by a C++23 interpreter. The chat server and client in this directory are written entirely in Chompo. The server supports multiple simultaneous TCP users, **rooms**, **roles/statuses**, **admin moderation** (kick/ban/whitelist/blacklist), unique names, per-room history, optional room password (`AUTH` + client-side message encryption), and timestamps on every chat line. The interactive client adds ANSI chrome, client-side **mute**, and decryption for password-protected rooms.

Build the interpreter from the repository root with `cmake -S . -B build && cmake --build build --parallel`.

## Files

- `chat_server.chmp` — multi-user server (rooms, roles, moderation);
- `chat_client.chmp` — interactive client (ANSI UI, mute, optional crypto);
- `LANGUAGE.md` — short language and runtime description.

## Quick local demo

Terminal 1 (open room, no password):

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 127.0.0.1 4040 50
```

Terminal 1 (locked room):

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 127.0.0.1 4040 50 my-secret
```

Terminal 2 and later:

```bash
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040
```

On Windows with a multi-config generator, use `build\Debug\Chompo.exe` instead. Server arguments: optional `host`, `port`, `historyLimit`, `password`. Client arguments: optional `host`, `port`. When the server requires a password, the client prompts with a `Password: ` field, then `Name: `.

## Message format

Chat lines are timestamped on the server (local wall clock via the `clockTime` builtin):

```text
[14:32:07] Alice: hello
```

The client renders the time dimmed on the left of the name.

## Rooms

Every user starts in `#lobby`. History and live traffic are **per room**.

```text
/rooms              list rooms with user counts
/room               show your current room
/join <room>        move to a room (creates it if needed)
/who                list users in your current room with roles
```

## Roles and statuses

Built-in statuses: `admin`, `member`. The **first** registered user becomes `admin`. If the last admin leaves, the first remaining registered user is promoted.

```text
/status                 show your status
/status list            list available statuses
/status create <name>   admin: create a custom status label
/status set <user> <s>  admin: assign a status to a user
```

Only the built-in `admin` status may run moderation commands. Custom statuses are cosmetic labels (and can be assigned by admins).

## Admin / server commands

```text
/kick <name>                 disconnect a user
/ban <name>                  ban a name (and disconnect if online)
/unban <name>
/bans                        list bans
/blacklist                   alias for /bans
/whitelist on|off
/whitelist add|remove <name>
/whitelist list              (or bare /whitelist)
```

## Client-only commands

```text
/mute <name>     hide that user's chat lines locally
/unmute <name>
/mutes           list muted names
```

## Shared commands

```text
/help       show server command list
/history    receive the current room's bounded history
/quit       leave the chat
```

## Room password and encryption

When the server is started with a password:

1. Clients answer `AUTH` via the interactive `Password: ` field (same password as the server). Wrong passwords re-prompt without jumping to `Name:` or closing the socket.
2. Non-command chat lines are encrypted on the client before send (`#E#` + hex).
3. The server stores and broadcasts the wire form unchanged (with a timestamp prefix).
4. Clients with the matching password decrypt for display.

This is a simple educational byte cipher (Vigenère-style modular add + hex), not production cryptography.

## Protocol extras (registration)

After a successful name:

```text
OK <name>
ROLE <status>
ROOM <room>
HISTORY <n>
...
END
* <name> joined #<room> as <status>
```

## Automated test

The `langjam_chat` CTest runs plaintext, encrypted, UI, rooms/moderation, mute, and password-retry paths against a real server and the Chompo client.
