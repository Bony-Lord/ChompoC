# Быстрый старт

## Требования

- компилятор C++23;
- CMake 4.2+;
- Ninja, Make или генератор IDE.

## Сборка и тесты

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

## Запуск программы

```bash
./build/Chompo program.chmp first second
```

В Chompo-вызове `args()` результатом будет `Array{"first", "second"}`.

Windows multi-config:

```powershell
.\build\Debug\Chompo.exe program.chmp first second
```

## Первая программа

```javascript
var name = "Chompo";

fun greet(value) {
    print("Hello, ", value, "\n");
}

greet(name);
```

Инструкции обычно заканчиваются `;`. Блоки записываются в `{ ... }`. Комментарии начинаются с `//` и продолжаются до конца строки.

## Запуск чата

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 127.0.0.1 4040 50
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040
```

Сервер и каждый клиент запускаются в отдельных терминалах.
