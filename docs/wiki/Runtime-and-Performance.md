# Runtime и производительность

Chompo использует оптимизированный tree-walk backend. Синтаксис и host API остаются расширяемыми, а основные операции исполнения избегают повторного строкового lookup и лишних выделений памяти.

## Pipeline

```text
source -> Lexer -> Pratt Parser -> AST -> Resolver -> Interpreter
```

## Реализованные оптимизации

- identifiers интернируются в `SymbolId`;
- Resolver вычисляет `Global` или `Local(depth, slot)`;
- локальные переменные и параметры хранятся в плотных slots;
- блоки без собственных локальных объявлений не выделяют `Environment`;
- block, iteration и function environments переиспользуются, если не захвачены closure;
- литералы декодируются один раз и кешируются в AST;
- присваивания и `++/--` переменных работают через прямой `Value&`;
- integer/integer операции имеют отдельный fast path;
- argument vectors переиспользуются по глубине вызова;
- `return`, `break`, `continue` не используют C++ exceptions;
- глобальный root environment кешируется;
- `push` сохраняет геометрический рост `std::vector`, поэтому одиночное добавление амортизированно O(1);
- Release использует `-O3`/`/O2`, IPO/LTO и dead-section elimination;
- опционально доступны native CPU tuning и GCC/Clang PGO.

## Расширяемость

Глобальные и native-значения остаются в динамическом реестре по `SymbolId`:

```cpp
interpreter.install_collection_builtins();
interpreter.install_io_builtins(io_manager);
interpreter.install_network_builtins(network_manager);
interpreter.install_system_builtins(arguments);
```

Новый native-модуль не требует изменения Lexer, Parser, Resolver или формата local slots.

## Performance/TLE suite

```bash
cmake -S . -B build-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHOMPO_ENABLE_PERFORMANCE_TESTS=ON
cmake --build build-perf --parallel
ctest --test-dir build-perf -L performance --output-on-failure
```

Suite проверяет checksum и individual TLE для:

- арифметики и циклов;
- пользовательских функций;
- массовых `push/pop`;
- глубоких scope lookup;
- ранних `return`.

В GitHub Actions Release-бинарник собирается отдельно. Job `execution-only TLE` скачивает готовый binary и измеряет только процесс исполнения `.chmp`.

## Измеренный baseline

Один подтверждённый Actions Release artifact показал:

| Сценарий | Время |
|---|---:|
| 300 000 арифметических итераций | ~0.051 с |
| 75 000 пользовательских вызовов | ~0.029 с |
| 50 000 `push` + 25 000 `pop` | ~0.026 с |
| 200 000 глубоких lookup | ~0.025 с |
| 200 000 вызовов с ранним `return` | ~0.071 с |

Это baseline конкретного hosted runner, а не обещание абсолютной скорости на любом CPU.

## Ограничения runtime

- максимальная глубина вызовов — `ChompoConfig::MaxCallDepth`, сейчас 512;
- циклические ссылки массивов запрещены;
- строки и `char` работают с байтами;
- массивы имеют ссылочную семантику;
- `for-in` обходит snapshot;
- tree-walk dispatch остаётся дороже bytecode VM, но для текущего чата и benchmark suite производительности достаточно.
