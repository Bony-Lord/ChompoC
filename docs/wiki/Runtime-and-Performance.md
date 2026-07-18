# Runtime и производительность

Chompo остаётся расширяемым tree-walk интерпретатором, но горячие пути не обязаны использовать медленный динамический lookup.

## Реализованные оптимизации

- identifier интернируются lexer-ом в `SymbolId`;
- Resolver один раз вычисляет `Global` или `Local(depth, slot)`;
- локальные переменные и параметры хранятся в плотных slots;
- глобальные и native-значения остаются в расширяемом реестре;
- function frames переиспользуются, если их не удерживает closure;
- lexer заранее резервирует память под tokens;
- keyword lookup выполняется через `string_view`;
- Release включает IPO/LTO, когда toolchain это поддерживает;
- `push` использует `reserve`, `pop` перемещает последний элемент;
- исходный файл читается одним выделением памяти.

Подробнее: [Архитектура runtime](Runtime-Architecture).

## Почему slots быстрее

Map-based lookup требует хеширования имени и часто прохода по цепочке scope. Resolver переносит эту работу из каждой итерации программы в однократную фазу до исполнения.

Во время исполнения локальная ссылка содержит готовый адрес:

```text
(depth, slot)
```

Глобальный реестр не удалён: он нужен для встроенных модулей, динамического host API и будущих импортов.

## Performance/TLE suite

Локальный запуск:

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
- lookup через глубокие scope.

Каждый сценарий проверяет checksum и индивидуальный лимит выполнения.

В GitHub Actions компиляция вынесена в отдельный job. `execution-only TLE` скачивает уже собранный Release-бинарник и измеряет только запуск Chompo на тяжёлых `.chmp`.

## Ограничения runtime

- максимальная глубина вызовов задаётся `ChompoConfig::MaxCallDepth`;
- циклические ссылки массивов запрещены;
- строки и `char` работают с байтами, не с Unicode code points;
- массивы имеют ссылочную семантику;
- `for-in` обходит snapshot последовательности.
