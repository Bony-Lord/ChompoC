# Network API

Chompo предоставляет неблокирующие TCP sockets через целочисленные handles. API рассчитан на однопоточный event loop.

## `netListen(host, port, backlog = 16)`

```javascript
var listener = netListen("0.0.0.0", 4040, 64);
```

Порт `0` просит ОС выбрать свободный порт. Фактический порт возвращает `netPort(listener)`.

## `netConnect(host, port)`

```javascript
var socket = netConnect("127.0.0.1", 4040);
```

Подключение выполняется до возврата из функции, после чего socket переводится в неблокирующий режим.

## `netAccept(listener)`

Возвращает новый socket handle или `NULL`, если ожидающих соединений сейчас нет. После события listener обычно вызывается в цикле до первого `NULL`, чтобы очистить accept queue.

## `netPoll(handles, timeoutMs = 0)`

```javascript
var ready = netPoll(Array{listener, client}, 100);
```

Возвращает массив handles, для которых доступно чтение/accept или зафиксировано закрытие/error.

Timeout:

- `0` — не ждать;
- положительное значение — ждать миллисекунды;
- `-1` — ждать без ограничения.

Переданный массив handles читается как snapshot. Закрытый/неизвестный handle в списке является runtime-ошибкой.

## `netSend(socket, data)`

```javascript
var sent = netSend(socket, data);
```

Это низкоуровневая неблокирующая отправка. Она возвращает фактически записанное число байтов. При заполненном системном буфере результат может быть меньше `len(data)`, включая `0`.

`netSend` не гарантирует полную доставку строки и не должен использоваться в чат-протоколе без самостоятельного учёта offset.

## `netSendAll(socket, data, timeoutMs = 5000)`

```javascript
var result = netSendAll(socket, "hello\n", 2000);
```

Повторяет partial sends до полного результата или остановки:

```javascript
Array{"sent", bytes}
Array{"timeout", bytes}
Array{"error", bytes, message}
```

`timeoutMs = -1` означает отсутствие лимита. Функция синхронна: пока она завершает одну отправку, интерпретатор не обрабатывает другие события. Чат ограничивает сообщения и использует timeout, поэтому один медленный клиент не блокирует сервер бесконечно.

## `netReceive(socket, maxBytes = 4096)`

Читает до `maxBytes` байт, максимум 1 MiB:

```javascript
Array{"data", chunk}
Array{"wait"}
Array{"closed"}
```

Необработанная socket-ошибка становится runtime-ошибкой. Для построчных серверных протоколов предпочтителен `netReceiveLine`.

## `netReceiveLine(socket)`

Читает одну строку до `\n`. Завершающие `\n` и `\r` не входят в результат. Максимальный накопленный размер строки — 1 MiB.

```javascript
Array{"data", line}
Array{"wait"}
Array{"closed"}
Array{"error", message}
```

Connection reset и другие ошибки возвращаются как `error`, а не завершают весь interpreter process. Сервер может закрыть только проблемного клиента.

Один вызов может вернуть только одну строку. После `data` следует повторять `netReceiveLine` до `wait`, чтобы обработать все уже накопленные строки.

## `netPort(handle)`

Возвращает локальный порт listener или socket.

## `netClose(handle)`

Закрывает handle и возвращает `NULL`. Повторное закрытие или использование закрытого handle является runtime-ошибкой.

## Безопасная отправка строки

```javascript
fun sendLine(socket, text) {
    var result = netSendAll(socket, text + "\n", 2000);
    return result[0] == "sent";
}
```

## Event loop

```javascript
var listener = netListen("0.0.0.0", 4040);
var clients = Array{};

while (true) {
    var watched = Array{listener};
    for (var client in clients)
        push(watched, client);

    var ready = netPoll(watched, 100);

    for (var handle in ready) {
        if (handle == listener) {
            while (true) {
                var client = netAccept(listener);
                if (client == NULL)
                    break;
                push(clients, client);
            }
        } else {
            while (true) {
                var packet = netReceiveLine(handle);
                if (packet[0] == "wait")
                    break;
                if (packet[0] == "closed" || packet[0] == "error") {
                    // найти handle, netClose(handle), removeAt(clients, index)
                    break;
                }
                sendLine(handle, packet[1]);
            }
        }
    }
}
```

Полная реализация: [LangJam Chat](LangJam-Chat).
