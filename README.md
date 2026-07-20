<div align="center">

# Chompo

### A dynamic language and tree-walk interpreter in C++23

[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![CMake](https://img.shields.io/badge/CMake-4.2%2B-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![CI](https://github.com/Bony-Lord/ChompoC/actions/workflows/ci.yml/badge.svg?branch=dev)](https://github.com/Bony-Lord/ChompoC/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-MIT-2ea44f)](LICENSE)
![Runtime](https://img.shields.io/badge/runtime-tree--walk-7c3aed)
![LangJam](https://img.shields.io/badge/LangJam-complete-10b981)

**Chompo** is a dynamically typed language with `.chmp` files, first-class functions, closures, mutable arrays and strings, file I/O, and a complete TCP networking API (including a working multi-user chat implementation).

[Features](#-features) · [Quick Start](#-quick-start) · [I/O](#-input-and-output) · [Network API](#-network-api) · [LangJam](#-langjam-readiness) · [Roadmap](#-roadmap)

</div>

> [!IMPORTANT]
> The active development branch is `dev`. LangJam requirements (language + multi-user chat) are now fully satisfied.

**Русская версия** → [README_RU.md](README_RU.md)

## ✨ Features

| Subsystem      | Status | Capabilities |
|----------------|--------|--------------|
| Values         | ✅     | `NULL`, `bool`, `integer`, `double`, `char`, `string`, `array`, `callable` |
| Variables      | ✅     | `var`, nested scopes, regular and compound assignments |
| Control Flow   | ✅     | `if`/`else`, `while`, `for-in`, `break`, `continue` |
| Functions      | ✅     | parameters, `return`, recursion, first-class functions, **closures** |
| Collections    | ✅     | arrays, indexing, mutation, `len`, `in`, repetition and concatenation |
| Strings        | ✅     | byte `char`, indexing and mutation |
| I/O            | ✅     | `input`, `istream`, `ostream`, `iostream` |
| TCP            | ✅     | listener, client socket, `netPoll`, accept, send, receive, close |
| Chat           | ✅     | multi-user chat server + client implemented in Chompo |
| Reliability    | ✅     | Runtime StackOverflow protection, cyclic array prevention, full test suite |
| LangJam        | ✅     | All mandatory requirements completed |

## 🚀 Quick Start

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

**Run:**

```bash
./build/Chompo program.chmp
```

**Windows:**

```powershell
.\build\Debug\Chompo.exe program.chmp
```

## ⚡ Example
```
fun makeCounter(start) {
    var value = start;

    fun next() {
        value++;
        return value;
    }

    return next;
}

var counter = makeCounter(10);
var values = Array{counter(), counter()};
push(values, 13);
print(values, "\n"); // {11, 12, 13}
```
## 🧩 Core Syntax

Chompo uses a clean, expression-oriented syntax close to JavaScript/C with first-class functions and closures.
Core constructs:
fun name(params) { ... } — function declaration (closures supported)
var x = ... — variable declaration with lexical scoping
if / else, while, for-in, break, continue, return
Arrays: Array{1, 2, 3}, indexing, push, pop, removeAt, len, in, concatenation and repetition
Strings are mutable byte sequences with char indexing
Everything is an expression where possible; functions are values
Full syntax reference: docs/wiki/Language-Syntax.md
Built-in functions: docs/wiki/Built-in-Functions.md

## 📥 Input and Output / 🌐 Network API

Chompo ships with powerful console/file I/O and a complete non-blocking TCP stack available directly from the language.
I/O:
input(), inputPoll(fd) — console input (pollable)
istream / ostream / iostream — file streams (support "append" mode)
flush()
TCP (event-driven via netPoll):
Listener and client sockets
netSend / netSendAll (with timeout) returning {"sent", n}, {"timeout", n}, {"error", n, msg}
netReceiveLine returning {"data", line}, {"wait"}, {"closed"} or {"error", msg}
Full multi-user chat (server + client) implemented 100% in Chompo
See detailed protocol handling and examples in langjam/Chompo/chat_server.chmp / chat_client.chmp.
Complete reference: docs/wiki/Network-API.md and docs/wiki/LangJam-Chat.md

## 🏁 LangJam Readiness

**✅ All requirements fulfilled**

- Language with full syntax and semantics
- Multi-user chat room (server + client) implemented entirely in Chompo
- TCP foundation with `netPoll`-based event loop
- Automatic tests on Windows and Linux

**Bonus features implemented** (for extra points):
- Commands `/help`, `/history`, `/quit`
- Timestamps
- Graceful client disconnect handling
- History persistence via `ostream(..., "append")`

## 🗺 Roadmap

### Before LangJam (completed)
- All control flow, I/O, TCP, **multi-user chat**

### After LangJam
- `Map`, modules, exceptions, Unicode, GC, bytecode VM, async runtime, REPL, LSP и т.д.

## 📄 License

MIT — see [LICENSE](LICENSE).