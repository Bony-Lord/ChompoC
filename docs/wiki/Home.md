# Chompo Wiki

Chompo — динамический язык с оптимизированным tree-walk интерпретатором на C++23. Файлы программ имеют расширение `.chmp`.

## Разделы

- [Быстрый старт](Getting-Started)
- [Синтаксис языка](Language-Syntax)
- [Типы и операторы](Types-and-Operators)
- [Функции и области видимости](Functions-and-Scopes)
- [Массивы и строки](Arrays-and-Strings)
- [Встроенные функции](Built-in-Functions)
- [Ввод, вывод и файлы](Input-and-Output)
- [Network API](Network-API)
- [LangJam Chat](LangJam-Chat)
- [Архитектура runtime](Runtime-Architecture)
- [Runtime и производительность](Runtime-and-Performance)

## Минимальная программа

```javascript
var values = Array{1, 2, 3};
push(values, 4);

for (var value in values)
    print(value, "\n");
```

## Что поддерживается

Chompo включает динамические типы, лексические scope, функции первого класса, closures, рекурсию, условия, `while`, `for-in`, изменяемые массивы, изменяемые по индексу byte strings, файловый I/O, command-line arguments и TCP sockets.

Многопользовательский сервер и клиент полностью написаны на Chompo и находятся в `langjam/Chompo`.
