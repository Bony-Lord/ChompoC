# Runtime и производительность

Chompo остаётся tree-walk интерпретатором, но runtime оптимизирован для частых операций.

## Реализованные оптимизации

- идентификаторы интернируются lexer-ом в числовые `SymbolId`;
- Environment ищет переменные по `uint32_t`, а не повторно хеширует строки;
- глубина найденной переменной кешируется внутри активного scope;
- пользовательские функции переиспользуют завершённые call frames;
- frame не переиспользуется, когда удерживается closure, поэтому семантика замыканий сохраняется;
- lexer заранее резервирует память под tokens;
- keyword lookup выполняется через `string_view`;
- Release включает IPO/LTO, когда компилятор его поддерживает;
- `push` использует `reserve`, `pop` перемещает последний элемент.

## Execution-only Performance/TLE suite

TLE относится только к исполнению программ Chompo. Configure, C++-компиляция и линковка измеряться не должны.

В GitHub Actions проверка разделена на два job:

1. `performance-build` собирает Release-бинарник `Chompo` и загружает его как artifact;
2. `performance-execution` скачивает готовый бинарник и запускает checker без CMake и компилятора.

Таймер Python-checker запускается непосредственно перед:

```text
Chompo case.chmp
```

и останавливается сразу после завершения этого процесса. В индивидуальный лимит не входят:

- установка инструментов;
- CMake configure;
- C++ compilation;
- linking;
- artifact upload/download;
- запуск самого Python-интерпретатора до benchmark case.

## Локальный запуск

Сначала один раз собрать Release:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel --target Chompo
```

После этого запускать только исполнение:

```bash
python tests/performance/run_performance_suite.py \
  --executable build-release/Chompo \
  --cases tests/performance/cases
```

Windows:

```powershell
python tests/performance/run_performance_suite.py `
  --executable build-release/Chompo.exe `
  --cases tests/performance/cases
```

Набор проверяет арифметику, вызовы функций, массивы и lookup через глубокие scope. Каждый сценарий одновременно проверяет checksum и индивидуальное ограничение времени.

## Ограничения runtime

- максимальная глубина вызовов задаётся `ChompoConfig::MaxCallDepth`;
- циклические ссылки массивов запрещены;
- строки и `char` работают с байтами, не с Unicode code points;
- массивы имеют ссылочную семантику;
- `for-in` обходит snapshot последовательности.
