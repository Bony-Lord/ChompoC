<div align="center">

# Chompo

### Динамический язык и расширяемый tree-walk интерпретатор на C++23

[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![CMake](https://img.shields.io/badge/CMake-4.2%2B-064F8C?logo=cmake)](https://cmake.org/)
[![CI](https://github.com/Bony-Lord/ChompoC/actions/workflows/ci.yml/badge.svg)](https://github.com/Bony-Lord/ChompoC/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-MIT-2ea44f)](LICENSE)

**Chompo** поддерживает функции первого класса, closures, изменяемые массивы, файловый I/O и неблокирующий TCP API.

[Wiki](docs/wiki/Home.md) · [Синтаксис](docs/wiki/Language-Syntax.md) · [Built-ins](docs/wiki/Built-in-Functions.md) · [Network API](docs/wiki/Network-API.md) · [Runtime](docs/wiki/Runtime-Architecture.md)

</div>

> [!IMPORTANT]
> Ветка `feature/slot-runtime` содержит изолированную оптимизацию runtime. Базовая `feature/perf-wiki-push-pop` остаётся отдельным стабильным слоем с Wiki, `push/pop` и performance checker.

## Возможности

| Подсистема | Поддержка |
|---|---|
| Типы | `NULL`, `bool`, `integer`, `double`, `char`, `string`, `array`, `callable` |
| Управление | `if`, `else`, `while`, `for-in`, `break`, `continue`, `return` |
| Функции | рекурсия, closures, функции как значения |
| Коллекции | индексация, мутация, `len`, `in`, `push`, `pop`, конкатенация, повторение |
| I/O | `input`, `istream`, `ostream`, `iostream` |
| TCP | `netListen`, `netConnect`, `netAccept`, `netPoll`, `netSend`, receive API, close |
| Runtime | resolver, `SymbolId`, плотные local slots, расширяемый global/native registry |
| Проверки | CTest, Windows/Linux CI, execution-only Release TLE suite |

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

Запуск:

```bash
./build/Chompo program.chmp
```

Windows с multi-config генератором:

```powershell
.\build\Debug\Chompo.exe program.chmp
```

## Пример

```javascript
fun makeCounter(start) {
    var value = start;

    fun next() {
        value++;
        return value;
    }

    return next;
}

var counter = makeCounter(10);
print(counter(), " ", counter(), "\n"); // 11 12
```

## Быстрый и расширяемый runtime

После Parser запускается отдельный Resolver:

```text
source -> Lexer -> Parser -> Resolver -> Interpreter
```

Resolver один раз превращает локальное имя в адрес `(depth, slot)`. Во время выполнения локальные переменные и параметры читаются из плотного `vector<Value>` без хеширования строки и без поиска имени по цепочке scope.

Глобальные и native-значения остаются в реестре по `SymbolId`. Поэтому новые built-in модули по-прежнему подключаются независимо:

```cpp
interpreter.install_collection_builtins();
interpreter.install_io_builtins(io_manager);
interpreter.install_network_builtins(network_manager);
```

Resolver не знает о конкретных built-ins, I/O или сети. Это сохраняет путь к модулям, plugin API, классам, REPL и будущему bytecode backend.

Подробнее: [`docs/wiki/Runtime-Architecture.md`](docs/wiki/Runtime-Architecture.md).

## `push` и `pop`

```javascript
var values = Array{};
push(values, 10, 20, 30);
print(pop(values), "\n"); // 30
```

`push(array, values...)` мутирует массив и возвращает новую длину. `pop(array)` удаляет последний элемент; пустой массив возвращает `NULL`.

## Execution-only TLE checker

```bash
cmake -S . -B build-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHOMPO_ENABLE_PERFORMANCE_TESTS=ON
cmake --build build-perf --parallel
ctest --test-dir build-perf -L performance --output-on-failure
```

В CI Release-бинарник собирается отдельно. Индивидуальные TLE-лимиты включают только запуск интерпретатора на `.chmp`, а не CMake и C++-компиляцию.

## LangJam

Chompo уже является полноценным интерпретатором на C++. Отдельная VM для допуска не требуется. Следующий продуктовый этап — сервер и клиент многопользовательского чата на самом Chompo.

## Лицензия

MIT — см. [LICENSE](LICENSE).
