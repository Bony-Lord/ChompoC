# Chompo — LangJam submission

Chompo is a dynamically typed language implemented by a C++23 interpreter. The chat server and client in this directory are written entirely in Chompo. The server supports multiple simultaneous TCP users, unique names, message broadcast, a bounded history of the last N user messages, optional room password (`AUTH` + client-side message encryption), `/help`, `/history`, `/quit`, and removal of normally or abruptly disconnected users. The interactive client uses ANSI colors, a banner, and a framed input prompt.

Build the interpreter from the repository root with `cmake -S . -B build && cmake --build build --parallel`.

## Files

- `chat_server.chmp` — multi-user server;
- `chat_client.chmp` — interactive client (ANSI UI + optional crypto);
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

## Chat commands

```text
/help       show commands
/history    receive the current bounded history
/quit       leave the chat
```

## Room password and encryption

When the server is started with a password:

1. Clients answer `AUTH` via the interactive `Password: ` field (same password as the server).
2. Non-command chat lines are encrypted on the client before send (`#E#` + hex).
3. The server stores and broadcasts the wire form unchanged.
4. Clients with the matching password decrypt for display.

This is a simple educational byte cipher (Vigenère-style modular add + hex), not production cryptography.

## Automated test

The `langjam_chat` CTest runs both plaintext and encrypted protocol paths against a real server and launches the Chompo client.
