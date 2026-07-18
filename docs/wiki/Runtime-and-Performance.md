# Runtime и производительность

Chompo остаётся расширяемым tree-walk интерпретатором, но основные горячие пути больше не используют строковый lookup, повторный разбор литералов, исключения для обычного control flow и постоянные временные аллокации.

## Реализованные оптимизации

- identifiers интернируются lexer-ом в `SymbolId`;
- Resolver один раз вычисляет `Global` или `Local(depth, slot)`;
- локальные переменные и параметры хранятся в плотных slots;
- `get_ref()` даёт прямой доступ для assignment и update;
- блоки без собственных объявлений не создают runtime scope;
- block, iteration и function environments переиспользуются, если не захвачены closure;
- литералы декодируются один раз и кешируются в AST;
- `return`, `break`, `continue` не используют C++ exceptions;
- integer arithmetic имеет специализированный `int64_t` fast path;
- argument vectors переиспользуются по call depth;
- global root кешируется в каждом `Environment`;
- `push` использует естественный геометрический рост `std::vector` и амортизированное O(1);
- `pop` перемещает последний элемент;
- lexer заранее резервирует память под tokens;
- keyword lookup выполняется через `string_view`;
- исходный файл читается одним выделением памяти;
- Release включает `-O3`/`/O2`, IPO/LTO и удаление неиспользуемых sections;
- доступны опциональные native CPU flags и GCC/Clang PGO.

Подробнее: [Архитектура runtime](Runtime-Architecture).

## Почему slots быстрее

Map-based lookup требует хеширования имени и часто прохода по цепочке scope. Resolver переносит эту работу из каждой итерации программы в однократную фазу до исполнения.

Во время исполнения локальная ссылка содержит готовый адрес:

```text
(depth, slot)
```

Глобальный registry не удалён: он нужен для встроенных модулей, динамического host API и будущих import.

## Release режимы

Обычный portable Release:

```bash
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
```

Оптимизация под текущий CPU:

```bash
cmake -S . -B build-native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHOMPO_NATIVE_OPTIMIZATIONS=ON
cmake --build build-native --parallel
```

Для GCC/Clang предусмотрены два PGO-режима:

```text
CHOMPO_PGO_GENERATE=ON
CHOMPO_PGO_USE=ON
```

Их нельзя включать одновременно. Сначала собирается instrumented binary и выполняется репрезентативная нагрузка, затем проект пересобирается с использованием полученного profile.

## Performance/TLE suite

```bash
cmake -S . -B build-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHOMPO_ENABLE_PERFORMANCE_TESTS=ON
cmake --build build-perf --parallel
ctest --test-dir build-perf -L performance --output-on-failure
```

Набор проверяет:

- арифметику и циклы;
- пользовательские функции;
- массовые `push/pop`;
- lookup через глубокие scope;
- функции с частыми ранними `return`.

Каждый сценарий проверяет checksum и индивидуальный лимит выполнения.

В GitHub Actions компиляция вынесена в отдельный job. `execution-only TLE` скачивает уже собранный Release-бинарник и измеряет только запуск Chompo на тяжёлых `.chmp`.

## Измеренные результаты

На Release artifact из GitHub Actions:

| Сценарий | Нагрузка | Время |
|---|---:|---:|
| Arithmetic | 300 000 итераций | ~0.051 с |
| Functions | 75 000 вызовов | ~0.029 с |
| Arrays | 50 000 `push` + 25 000 `pop` | ~0.026 с |
| Scope lookup | 200 000 обращений | ~0.025 с |
| Control flow | 200 000 вызовов с ранним `return` | ~0.071 с |

До исправления `push` выполнял `reserve(size + 1)` перед каждым добавлением. Это уничтожало геометрический рост `std::vector` и превращало 50 000 добавлений в квадратичную операцию с TLE более 20 секунд. После удаления точечного `reserve` тот же сценарий занимает около 0.026 секунды — ускорение более чем в 700 раз на воспроизведённом runner-бинарнике.

Абсолютные времена зависят от CPU и нагрузки CI. Их основная задача — обнаруживать крупные регрессии, а не служить универсальным рейтингом языка.

## Ограничения runtime

- максимальная глубина вызовов задаётся `ChompoConfig::MaxCallDepth`;
- циклические ссылки массивов запрещены;
- строки и `char` работают с байтами, не с Unicode code points;
- массивы имеют ссылочную семантику;
- `for-in` обходит snapshot последовательности;
- tree-walk dispatch остаётся дороже полноценного bytecode dispatch; для следующего слоя оптимизации предусмотрен отдельный backend поверх того же Resolver и native registry.
