# Chompo language description

Chompo is a dynamically typed language with lexical scopes. Programs use the `.chmp` extension and are executed by a C++23 interpreter.

## Values

```text
NULL
bool
integer
double
char
string
array
callable
```

Strings and `char` values are byte-based. Arrays have reference semantics: assigning an array creates an alias to the same mutable object.

## Variables and expressions

```javascript
var value = 10;
var empty;          // NULL

value = 20;
value += 2;
value--;
```

Operators include arithmetic `+ - * / %`, comparisons `< <= > >=`, equality `== !=`, short-circuit logic `&& || !`, membership `in`, assignments `= += -= *= /=`, prefix/postfix `++ --`, function calls, and indexing.

## Control flow

```javascript
if (condition) {
    print("yes\n");
} else {
    print("no\n");
}

while (condition) {
    if (skip)
        continue;
    if (stop)
        break;
}

for (var value in Array{1, 2, 3})
    print(value, "\n");
```

`for-in` accepts arrays and strings and iterates over a snapshot.

## Functions

```javascript
fun makeCounter(start) {
    var value = start;

    fun next() {
        value++;
        return value;
    }

    return next;
}
```

Functions are first-class values. Recursion and closures are supported. A missing or empty `return` produces `NULL`.

## Collections

```javascript
var values = Array{1, 2};
push(values, 3);
var last = pop(values);
var first = removeAt(values, 0);
```

Arrays support indexing, nested mutation, concatenation, repetition, `len`, and `in`. Direct and indirect cyclic array references are rejected. Strings support byte indexing and indexed replacement with a `char`.

## I/O and script arguments

```javascript
var line = input();
var packet = inputPoll(0);
flush();
var commandLine = args();
```

`inputPoll` returns `Array{"data", line}`, `Array{"wait"}`, or `Array{"closed"}`. `args()` returns arguments passed after the source filename.

## TCP event loop

```javascript
var listener = netListen("0.0.0.0", 4040);
var ready = netPoll(Array{listener}, 100);
var client = netAccept(listener);
var packet = netReceiveLine(client);
var result = netSendAll(client, "hello\n", 2000);
netClose(client);
```

Sockets are non-blocking. `netPoll` drives a single-threaded event loop. `netSend` is a low-level partial-send operation; `netSendAll` completes a protocol line or returns a timeout/error status.

## Runtime architecture

```text
source -> Lexer -> Pratt Parser -> AST -> Resolver -> Interpreter
```

The Resolver converts local names to lexical `(depth, slot)` addresses. Local values use dense slots, while global and native values remain in an extensible `SymbolId` registry. I/O and networking are independent native modules rather than grammar-specific special cases.
