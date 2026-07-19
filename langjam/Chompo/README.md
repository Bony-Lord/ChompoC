# Chompo — LangJam submission

Chompo is a dynamically typed language implemented by a C++23 interpreter. The chat server and client in this directory are written entirely in Chompo. The server supports multiple simultaneous TCP users, unique names, message broadcast, a bounded history of the last N user messages, `/help`, `/history`, `/quit`, and removal of normally or abruptly disconnected users.

Build the interpreter from the repository root with `cmake -S . -B build && cmake --build build --parallel`. Start the server with `./build/Chompo langjam/Chompo/chat_server.chmp 0.0.0.0 4040 50`, then start each client with `./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040`. On Windows with a multi-config generator, use `build\Debug\Chompo.exe` instead. The server arguments are optional `host`, `port`, and `historyLimit`; the client arguments are optional `host` and `port`.

## Files

- `chat_server.chmp` — multi-user server;
- `chat_client.chmp` — interactive client;
- `LANGUAGE.md` — short language and runtime description.

## Chat commands

```text
/help       show commands
/history    receive the current bounded history
/quit       leave the chat
```

## Quick local demo

Terminal 1:

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 127.0.0.1 4040 50
```

Terminal 2 and later:

```bash
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040
```

The automated `langjam_chat` CTest executes the complete protocol against a real server and also launches the Chompo client.
