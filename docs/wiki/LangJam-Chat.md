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
./build/Chompo langjam/Chompo/chat_server.chmp 0.0.0.0 4040 50 my-room-secret
```

Аргументы:

1. bind host, по умолчанию `0.0.0.0`;
2. port, по умолчанию `4040`;
3. количество сохраняемых сообщений, по умолчанию `50`;
4. optional room password — если задан, включаются `AUTH` и шифрование тел сообщений на стороне клиентов.

Порт `0` выбирает свободный порт ОС. Сервер печатает:

```text
LISTENING 54321
```

При заданном password дополнительно:

```text
AUTH required
```

## Запуск клиента

```bash
./build/Chompo langjam/Chompo/chat_client.chmp 127.0.0.1 4040
```

Аргументы клиента: host и port. Если сервер запущен с room password, клиент при подключении показывает поле **`Password: `**, затем **`Name: `**.

Windows multi-config:

```powershell
.\build\Debug\Chompo.exe langjam\Chompo\chat_server.chmp 0.0.0.0 4040 50 secret
.\build\Debug\Chompo.exe langjam\Chompo\chat_client.chmp 127.0.0.1 4040
```

## Возможности

- несколько TCP-клиентов в одном event loop;
- optional room password (`AUTH`);
- client-side шифрование user-сообщений (общий ключ = password);
- ANSI-оформление клиента: баннер, цвета по типам строк, рамка prompt;
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
- длина сообщения: 1024 байта (на wire body, включая `#E#`+hex при шифровании);
- protocol line buffer: 1 MiB;
- history limit принудительно не меньше 1;
- сервер завершается внешним сигналом, отдельной административной команды shutdown нет;
- `netSendAll` синхронно ждёт отдельного клиента не более заданного timeout;
- crypto — учебный byte stream cipher + hex, **не** production TLS/AES;
- ввод клиента построчный (`input` / `inputPoll`), без raw-mode TUI.

## Протокол

### Без password (как раньше)

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

После ошибки соединения имя можно отправить снова.

### С password

```text
AUTH enter room password
→ <password>
OK auth
NAME choose a unique name
→ Alice
OK Alice
HISTORY …
END
* Alice joined
```

Неверный password:

```text
ERROR wrong password
```

После ошибки сервер закрывает соединение.

### Сообщения

Обычное plaintext-сообщение:

```text
Alice: hello
```

При включённом room password клиент шифрует **тело** (не имя и не команды):

```text
Alice: #E#dbcacfded4
```

Формат: префикс `#E#` + hex-байты ciphertext. Алгоритм (общий secret = room password):

```text
cipher[i] = (plain[i] + key[i % len(key)]) % 256
plain[i]  = (cipher[i] - key[i % len(key)] + 256) % 256
```

Сервер **не** расшифровывает user body: кладёт wire-форму в history и broadcast. Клиенты с тем же password показывают plaintext; без ключа видно `#E#…`.

Команды и control-lines всегда plaintext:

```text
COMMANDS /help /history /quit
HISTORY N
...
END
BYE
* Alice joined
* Bob left
```

## Архитектура сервера

Сервер хранит параллельные массивы socket handles, имён и флагов auth. `removeAt` удаляет все записи при отключении. Каждый проход создаёт watched snapshot, вызывает `netPoll`, принимает все ожидающие соединения и читает каждую готовую socket до статуса `wait`.

История ограничивается удалением нулевого элемента после превышения лимита.

## Архитектура клиента

Клиент использует `netPoll` с коротким timeout и затем `inputPoll(0)`. Поэтому входящие сообщения отображаются даже тогда, когда пользователь ещё ничего не ввёл.

После `/quit` клиент продолжает читать socket до `BYE` или закрытия, чтобы не потерять broadcast сообщений, отправленных непосредственно перед командой выхода.

UI: ANSI-цвета (ESC = `Char(27)`), баннер и рамка поля ввода. Ввод по-прежнему завершается Enter.

## End-to-end тест

CTest `langjam_chat`:

- **plaintext path** — сервер без password: регистрация Alice/Bob, conflict имён, broadcast, `/history`, `/help`, `/quit`, RST-клиент Eve, запуск `chat_client.chmp`;
- **encrypted path** — сервер с password: wrong `AUTH`, good `AUTH`, wire body `#E#…`, history с ciphertext, `chat_client.chmp` с password расшифровывает broadcast.

Тест выполняется в CI на Windows и Ubuntu.
