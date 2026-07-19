# LangJam Chat

Сервер и клиент находятся в:

```text
langjam/Chompo/chat_server.chmp
langjam/Chompo/chat_client.chmp
```

Оба приложения полностью написаны на Chompo. C++-часть предоставляет интерпретатор, I/O и TCP handles.

## Запуск сервера

```bash
./build/Chompo langjam/Chompo/chat_server.chmp 0.0.0.0 4040 50
```

Аргументы:

1. bind host, по умолчанию `0.0.0.0`;
2. port, по умолчанию `4040`;
3. количество сохраняемых сообщений, по умолчанию `50`.

Порт `0` выбирает свободный порт ОС. Сервер печатает:

```text
LISTENING 54321
```

## Запуск клиента

```bash
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040
```

Аргументы клиента: host и port.

Windows multi-config:

```powershell
.\build\Debug\Chompo.exe langjam\Chompo\chat_server.chmp 0.0.0.0 4040 50
.\build\Debug\Chompo.exe langjam\Chompo\chat_client.chmp 127.0.0.1 4040
```

## Возможности

- несколько TCP-клиентов в одном event loop;
- уникальные непустые имена до 24 байт;
- broadcast каждого пользовательского сообщения;
- последние N сообщений;
- `/help`;
- `/history`;
- `/quit`;
- удаление клиента при EOF, normal close и connection reset;
- timeout полной отправки;
- интерактивный клиент, одновременно читающий socket и console.

Имена и сообщения измеряются в байтах. Пробелы в имени разрешены; сравнение имён точное и регистрозависимое. Имя не может начинаться с `/`.

История содержит только пользовательские сообщения. Join/leave notices не занимают место в истории.

## Ограничения

- длина имени: 24 байта;
- длина сообщения: 1024 байта;
- protocol line buffer: 1 MiB;
- history limit принудительно не меньше 1;
- сервер завершается внешним сигналом, отдельной административной команды shutdown нет;
- `netSendAll` синхронно ждёт отдельного клиента не более заданного timeout.

## Протокол

После подключения сервер отправляет:

```text
NAME choose a unique name
```

Клиент отвечает одной строкой имени. Успех:

```text
OK Alice
HISTORY 2
Bob: old message
Alice: previous message
END
* Alice joined
```

Ошибка имени:

```text
ERROR name is already in use
```

После ошибки соединение остаётся открытым, и клиент может отправить другое имя.

Обычное сообщение распространяется как:

```text
Alice: hello
```

Команды:

```text
COMMANDS /help /history /quit
HISTORY N
...
END
BYE
```

## Архитектура сервера

Сервер хранит параллельные массивы socket handles и имён. `removeAt` удаляет обе записи при отключении. Каждый проход создаёт watched snapshot, вызывает `netPoll`, принимает все ожидающие соединения и читает каждую готовую socket до статуса `wait`.

История ограничивается удалением нулевого элемента после превышения лимита.

## Архитектура клиента

Клиент использует `netPoll` с коротким timeout и затем `inputPoll(0)`. Поэтому входящие сообщения отображаются даже тогда, когда пользователь ещё ничего не ввёл.

После `/quit` клиент продолжает читать socket до `BYE` или закрытия, чтобы не потерять broadcast сообщений, отправленных непосредственно перед командой выхода.

## End-to-end тест

CTest `langjam_chat`:

- запускает настоящий сервер на порту `0`;
- регистрирует Alice и Bob;
- проверяет конфликт имён;
- проверяет broadcast, `/history`, `/help`, `/quit`;
- подключает Eve и закрывает socket через RST;
- проверяет, что сервер удалил Eve и продолжил работу;
- запускает настоящий `chat_client.chmp` с redirected stdin.

Тест выполняется в CI на Windows и Ubuntu.
