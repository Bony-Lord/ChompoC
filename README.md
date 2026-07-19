<div align="center">

# Chompo

### Динамический язык, оптимизированный интерпретатор и многопользовательский TCP-чат на C++23

[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![CMake](https://img.shields.io/badge/CMake-4.2%2B-064F8C?logo=cmake)](https://cmake.org/)
[![CI](https://github.com/Bony-Lord/ChompoC/actions/workflows/ci.yml/badge.svg)](https://github.com/Bony-Lord/ChompoC/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-MIT-2ea44f)](LICENSE)

**Chompo** — динамически типизированный язык с функциями первого класса, замыканиями, изменяемыми массивами и строками, файловым I/O и неблокирующим TCP API.

[Wiki](docs/wiki/Home.md) · [Синтаксис](docs/wiki/Language-Syntax.md) · [Built-ins](docs/wiki/Built-in-Functions.md) · [Network API](docs/wiki/Network-API.md) · [LangJam Chat](docs/wiki/LangJam-Chat.md)

</div>

> [!IMPORTANT]
> Актуальная ветка завершения языка и чата — `feature/langjam-chat`. В ней находятся сервер, клиент, submission-папка и end-to-end тест. Базовая `feature/perf-wiki-push-pop` остаётся стабильным слоем оптимизированного runtime.

## Возможности

| Подсистема | Поддержка |
|---|---|
| Типы | `NULL`, `bool`, `integer`, `double`, `char`, `string`, `array`, `callable` |
| Управление | `if`, `else`, `while`, `for-in`, `break`, `continue`, `return` |
| Функции | рекурсия, closures, функции как значения |
| Коллекции | индексация, мутация, `len`, `in`, `push`, `pop`, `removeAt`, конкатенация, повторение |
| I/O | `input`, `inputPoll`, `flush`, `istream`, `ostream`, `iostream` |
| Система | `args()` для аргументов Chompo-программы |
| TCP | listener/client, `netPoll`, partial send, guaranteed line send, receive statuses, close |
| Runtime | Resolver, `SymbolId`, плотные local slots, pools, cached literals, integer fast paths |
| Чат | уникальные имена, broadcast, последние N сообщений, `/help`, `/history`, `/quit`, disconnect cleanup |
| Проверки | Windows/Linux CTest, TCP loopback, end-to-end chat smoke test, execution-only Release TLE |

## Сборка

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Release:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
```

Запуск обычной программы:

```bash
./build/Chompo program.chmp argument1 argument2
```

`args()` возвращает только аргументы после имени `.chmp`-файла.

## LangJam Chat

Сервер:

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 0.0.0.0 4040 50
```

Аргументы: `host`, `port`, `historyLimit`. Порт `0` выбирает свободный порт ОС и печатает его как `LISTENING <port>`.

Клиент:

```bash
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040
```

Windows с multi-config сборкой:

```powershell
.\build\Debug\Chompo.exe langjam\Chompo\chat_server.chmp 0.0.0.0 4040 50
.\build\Debug\Chompo.exe langjam\Chompo\chat_client.chmp 127.0.0.1 4040
```

Команды клиента:

```text
/help
/history
/quit
```

Сервер и клиент полностью написаны на Chompo. C++ предоставляет только интерпретатор и host API для TCP/I/O.

## Chat-ready API

```javascript
var arguments = args();
var clients = Array{};
push(clients, socket);
removeAt(clients, 0);

var console = inputPoll(0);       // {"data", line}, {"wait"}, {"closed"}
flush();

var sent = netSendAll(socket, "hello\n", 2000);
// {"sent", bytes}, {"timeout", bytes}, {"error", bytes, message}
```

`netSend` является низкоуровневой неблокирующей операцией и может отправить только часть строки. Для сообщений протокола используется `netSendAll`.

`netReceiveLine` возвращает `Array{"data", line}`, `Array{"wait"}`, `Array{"closed"}` или `Array{"error", message}`.

## Runtime

```text
source -> Lexer -> Parser -> Resolver -> optimized Interpreter
```

Resolver один раз переводит локальные имена в `(depth, slot)`. Литералы кешируются, блоки без локальных объявлений не создают окружения, окружения переиспользуются, а `return`/`break`/`continue` не используют C++ exceptions. Глобальные и native-функции остаются в расширяемом реестре по `SymbolId`.

## Тестирование

```bash
ctest --test-dir build --output-on-failure
```

`langjam_chat` поднимает настоящий сервер, подключает нескольких TCP-клиентов, проверяет конфликт имён, broadcast, историю, команды, корректное удаление клиента после RST и запуск самого `chat_client.chmp`.

Текущий CI прошёл на Windows и Ubuntu; Release/TLE suite также зелёный.

## Статус LangJam

Готово:

- язык и интерпретатор;
- необходимые типы, условия, циклы, функции и коллекции;
- TCP/I/O API;
- многопользовательский сервер и клиент на Chompo;
- регистрация пользователей и уникальные имена;
- broadcast;
- последние N сообщений;
- выход и удаление отключившихся пользователей;
- инструкции запуска и описание языка;
- автоматический end-to-end тест.

Остаётся организационный шаг: перенести `langjam/Chompo` в fork `langdev-jam/plic` и открыть submission PR.

## Лицензия

MIT — see [LICENSE](LICENSE).
