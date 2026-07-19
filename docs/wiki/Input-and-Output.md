# Ввод и вывод

## `input()`

```javascript
var line = input();
```

Блокирующе читает одну строку без завершающего `\n`. На EOF возвращает `NULL`.

## `inputPoll(timeoutMs = 0)`

```javascript
var packet = inputPoll(0);

if (packet[0] == "data")
    print(packet[1], "\n");
```

Результаты:

```javascript
Array{"data", line}
Array{"wait"}
Array{"closed"}
```

Timeout:

- `0` — только проверить готовность;
- положительное значение — ждать до указанного числа миллисекунд;
- `-1` — ждать без ограничения.

Для интерактивного стандартного терминала готовностью считается введённая завершённая строка. Для файлов строка читается непосредственно. При перенаправленном pipe API остаётся построчным: producer должен завершать записи символом новой строки или закрывать pipe.

## `flush()`

```javascript
print("Name: ");
flush();
```

Сбрасывает текущий поток вывода и возвращает `NULL`. Нужен для интерактивных prompts и немедленной записи в файл.

## Стандартные потоки

Стандартный ввод/вывод обозначается строкой `"standart"`. Историческое написание является частью текущего API.

## `istream(path = "standart")`

Меняет источник `input` и `inputPoll`.

```javascript
istream("data.txt");
var first = input();
istream("standart");
```

Новое открытие файла начинает чтение с начала.

## `ostream(path = "standart", mode = "rewrite")`

Меняет поток инструкции `print` и функции `flush`.

```javascript
ostream("result.txt", "rewrite");
print("new\n");
flush();

ostream("result.txt", "append");
print("more\n");

ostream("standart");
```

Режимы:

| Режим | Поведение |
|---|---|
| `"rewrite"` | создать или полностью перезаписать файл |
| `"append"` | создать или дописывать в конец |
| `"create"` | создать новый файл; ошибка, если путь уже существует |

Неизвестный режим является runtime-ошибкой. Булевы флаги вместо строки не принимаются.

## `iostream(inputPath = "standart", outputPath = "standart", outputMode = "rewrite")`

Подготавливает входной и выходной файл перед переключением потоков. Если открытие одного из новых файлов не удалось, старые потоки не заменяются на частично подготовленную конфигурацию.

```javascript
iostream("request.txt", "response.txt", "rewrite");
print(input(), "\n");
flush();
iostream();
```

## `print(arguments...)`

`print` — инструкция языка. Она вычисляет аргументы слева направо и выводит их подряд без автоматических разделителей.

```javascript
print("value=", 42, "\n");
```

Для перевода строки нужно явно передать `"\n"`.
