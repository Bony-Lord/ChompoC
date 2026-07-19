# Синтаксис языка

## Литералы

```javascript
NULL
true
false
123
3.14
'A'
"text\n"
Array{1, 2, 3}
```

Строковые escape-последовательности: `\n`, `\t`, `\r`, `\"`, `\\`.

Для `char`: `\n`, `\t`, `\r`, `\0`, `\\`, `\'`.

## Переменные

```javascript
var value = 10;
var empty;          // NULL

value = 20;
value += 2;
value -= 1;
value *= 3;
value /= 2;
```

Повторное объявление имени в одном лексическом scope является ошибкой. Вложенный scope может скрыть внешнее имя.

## Условия

```javascript
if (value > 10) {
    print("large\n");
} else {
    print("small\n");
}
```

Условие принимает любое значение и использует его truthiness.

## `while`

```javascript
while (condition) {
    if (skip)
        continue;

    if (stop)
        break;
}
```

## `for-in`

```javascript
for (var value in Array{1, 2, 3})
    print(value, "\n");

for (var character in "abc")
    print(character, "\n");
```

Итерируемое выражение вычисляется один раз. Массив или строка обходятся по snapshot. Переменная каждой итерации имеет отдельный scope.

## Функции и `return`

```javascript
fun add(left, right) {
    return left + right;
}

var result = add(2, 3);
```

`return;` и завершение без явного `return` возвращают `NULL`.

## `break` и `continue`

Они разрешены только внутри цикла. Граница функции сбрасывает loop-контекст: функция, объявленная внутри цикла, не может использовать `break` или `continue` для внешнего цикла.

## `print`

```javascript
print("value=", value, "\n");
```

`print` — инструкция, а не callable-функция. Она принимает ноль или несколько выражений, вычисляет их слева направо и не добавляет пробелы или перевод строки автоматически.

## Пустая инструкция и блок

```javascript
;

{
    var local = 1;
    print(local);
}
```

## Вызов, индексирование и обновление

```javascript
functionValue(argument);
array[index];
array[index] = value;
string[index] = 'X';
++counter;
counter--;
```

Целью присваивания и `++/--` может быть переменная или цепочка индексирования, начинающаяся с переменной.
