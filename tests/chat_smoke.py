#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import queue
import re
import select
import socket
import struct
import subprocess
import threading
import time
from pathlib import Path

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
# Optional leading [HH:MM:SS] then "name: body".
CHAT_RE = re.compile(r"^(?:\[\d{2}:\d{2}:\d{2}\] )?([^:]+): (.*)$")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def send_line(sock: socket.socket, text: str) -> None:
    sock.sendall((text + "\n").encode("utf-8"))


def open_client(port: int) -> tuple[socket.socket, object]:
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(3)
    reader = sock.makefile("r", encoding="utf-8", newline="\n")
    return sock, reader


def read_line(reader: object, description: str) -> str:
    line = reader.readline()
    if line == "":
        raise RuntimeError(f"connection closed while waiting for {description}")
    return line.rstrip("\r\n")


def expect(reader: object, expected: str) -> None:
    actual = read_line(reader, repr(expected))
    require(actual == expected, f"expected {expected!r}, got {actual!r}")


def expect_prefix(reader: object, prefix: str) -> str:
    actual = read_line(reader, f"prefix {prefix!r}")
    require(actual.startswith(prefix), f"expected prefix {prefix!r}, got {actual!r}")
    return actual


def expect_chat(reader: object, name: str, body: str) -> str:
    actual = read_line(reader, f"chat from {name}")
    match = CHAT_RE.match(actual)
    require(match is not None, f"not a chat line: {actual!r}")
    require(match.group(1) == name, f"expected name {name!r}, got {match.group(1)!r} in {actual!r}")
    require(match.group(2) == body, f"expected body {body!r}, got {match.group(2)!r} in {actual!r}")
    require(actual.startswith("["), f"chat line must include timestamp: {actual!r}")
    return actual


def expect_eventually(reader: object, expected: str, limit: int = 20) -> list[str]:
    """Read lines until exact match; return skipped lines."""
    skipped: list[str] = []
    for _ in range(limit):
        actual = read_line(reader, repr(expected))
        if actual == expected:
            return skipped
        skipped.append(actual)
    raise RuntimeError(f"never saw {expected!r}; skipped {skipped!r}")


def drain_history(reader: object) -> list[str]:
    header = expect_prefix(reader, "HISTORY ")
    count = int(header.split(" ", 1)[1])
    messages: list[str] = []
    for _ in range(count):
        messages.append(read_line(reader, "history line"))
    expect(reader, "END")
    return messages


def register(reader: object, sock: socket.socket, name: str, expect_role: str = "member") -> None:
    expect(reader, "NAME choose a unique name")
    send_line(sock, name)
    expect(reader, f"OK {name}")
    expect(reader, f"ROLE {expect_role}")
    expect(reader, "ROOM lobby")
    drain_history(reader)


def start_output_reader(stream: object, lines: queue.Queue[str]) -> threading.Thread:
    def read() -> None:
        for line in stream:
            lines.put(line.rstrip("\r\n"))

    thread = threading.Thread(target=read, daemon=True)
    thread.start()
    return thread


def wait_server_line(lines: queue.Queue[str], timeout: float = 5.0) -> str:
    try:
        return lines.get(timeout=timeout)
    except queue.Empty as error:
        raise RuntimeError("server did not print its listening port") from error


def abrupt_close(sock: socket.socket) -> None:
    linger = struct.pack("hh", 1, 0) if os.name == "nt" else struct.pack("ii", 1, 0)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
    sock.close()


def encrypt_body(plain: str, key: str) -> str:
    """Mirror of chat_client.chmp Vigenère-byte + hex encoding."""
    if not key:
        return plain
    cipher = bytes((plain_b + key.encode("utf-8")[i % len(key)]) % 256 for i, plain_b in enumerate(plain.encode("utf-8")))
    return "#E#" + cipher.hex()


def start_server(
    executable: Path,
    server_source: Path,
    *extra_args: str,
) -> tuple[subprocess.Popen[str], queue.Queue[str], int]:
    server = subprocess.Popen(
        [str(executable), str(server_source), "127.0.0.1", "0", "3", *extra_args],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )
    require(server.stdout is not None and server.stderr is not None, "failed to capture server output")
    output_lines: queue.Queue[str] = queue.Queue()
    start_output_reader(server.stdout, output_lines)

    listening = None
    for _ in range(40):
        line = wait_server_line(output_lines)
        plain = strip_ansi(line)
        if plain.startswith("LISTENING "):
            listening = plain
            break
    require(listening is not None, "server did not print LISTENING <port>")
    port = int(listening.split(" ", 1)[1])
    require(0 < port <= 65535, f"invalid listening port {port}")
    return server, output_lines, port


def stop_server(server: subprocess.Popen[str]) -> None:
    if server.poll() is None:
        server.terminate()
        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=3)

    stderr = server.stderr.read() if server.stderr is not None else ""
    if server.returncode not in (0, -15, 1):
        raise RuntimeError(f"unexpected server exit {server.returncode}: {stderr}")


def run_plaintext_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    server, _output_lines, port = start_server(executable, server_source)
    clients: list[socket.socket] = []
    readers: list[object] = []

    try:
        alice, alice_reader = open_client(port)
        clients.append(alice)
        readers.append(alice_reader)
        register(alice_reader, alice, "Alice", expect_role="admin")
        expect(alice_reader, "* Alice joined #lobby as admin")

        bob, bob_reader = open_client(port)
        clients.append(bob)
        readers.append(bob_reader)
        expect(bob_reader, "NAME choose a unique name")
        send_line(bob, "")
        expect(bob_reader, "ERROR name cannot be empty")
        send_line(bob, "Alice")
        expect(bob_reader, "ERROR name is already in use")
        send_line(bob, "Bob")
        expect(bob_reader, "OK Bob")
        expect(bob_reader, "ROLE member")
        expect(bob_reader, "ROOM lobby")
        expect(bob_reader, "HISTORY 0")
        expect(bob_reader, "END")
        expect(bob_reader, "* Bob joined #lobby as member")
        expect(alice_reader, "* Bob joined #lobby as member")

        send_line(alice, "hello")
        expect_chat(alice_reader, "Alice", "hello")
        expect_chat(bob_reader, "Alice", "hello")

        send_line(bob, "/history")
        expect(bob_reader, "HISTORY 1")
        hist = read_line(bob_reader, "history chat")
        require(CHAT_RE.match(hist) is not None and "Alice: hello" in hist, f"bad history line: {hist!r}")
        expect(bob_reader, "END")

        send_line(bob, "/help")
        help_line = read_line(bob_reader, "help")
        require(help_line.startswith("COMMANDS "), f"bad help: {help_line!r}")
        require("/kick" in help_line and "/join" in help_line, f"help missing new commands: {help_line!r}")

        send_line(bob, "/quit")
        expect(bob_reader, "BYE")
        expect(alice_reader, "* Bob left #lobby")
        bob_reader.close()
        bob.close()
        readers.remove(bob_reader)
        clients.remove(bob)

        eve, eve_reader = open_client(port)
        expect(eve_reader, "NAME choose a unique name")
        send_line(eve, "Eve")
        expect(eve_reader, "OK Eve")
        expect(eve_reader, "ROLE member")
        expect(eve_reader, "ROOM lobby")
        expect(eve_reader, "HISTORY 1")
        hist = read_line(eve_reader, "history")
        require("Alice: hello" in hist, f"expected history chat: {hist!r}")
        expect(eve_reader, "END")
        expect(eve_reader, "* Eve joined #lobby as member")
        expect(alice_reader, "* Eve joined #lobby as member")
        eve_reader.close()
        abrupt_close(eve)
        expect(alice_reader, "* Eve left #lobby")

        client = subprocess.run(
            [str(executable), str(client_source), "127.0.0.1", str(port)],
            input="Cli\nfrom-client\n/quit\n",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=10,
        )
        require(client.returncode == 0, f"chat client exited with {client.returncode}: {client.stderr}")
        normalized = client.stdout.replace("\r\n", "\n")
        require("OK Cli" in normalized, f"client did not register:\n{normalized}")
        expect(alice_reader, "* Cli joined #lobby as member")
        expect_chat(alice_reader, "Cli", "from-client")
        expect(alice_reader, "* Cli left #lobby")
        require(normalized.count("Cli: from-client") == 0, f"own live chat should be suppressed:\n{normalized}")

        send_line(alice, "/quit")
        expect(alice_reader, "BYE")
        alice_reader.close()
        alice.close()
        readers.remove(alice_reader)
        clients.remove(alice)

        time.sleep(0.1)
        require(server.poll() is None, "server exited during the plaintext smoke test")
    finally:
        for reader in readers:
            try:
                reader.close()
            except Exception:
                pass
        for sock in clients:
            try:
                sock.close()
            except Exception:
                pass
        stop_server(server)


def run_encrypted_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    password = "secret"
    server, output_lines, port = start_server(executable, server_source, password)
    clients: list[socket.socket] = []
    readers: list[object] = []

    try:
        saw_auth = False
        for _ in range(8):
            try:
                extra = output_lines.get(timeout=0.3)
            except queue.Empty:
                break
            if strip_ansi(extra).strip() == "AUTH required":
                saw_auth = True
                break
        require(saw_auth, "encrypted server must announce AUTH required")

        bad, bad_reader = open_client(port)
        clients.append(bad)
        readers.append(bad_reader)
        expect(bad_reader, "AUTH enter room password")
        send_line(bad, "wrong")
        expect(bad_reader, "ERROR wrong password")
        send_line(bad, "still-wrong")
        expect(bad_reader, "ERROR wrong password")
        send_line(bad, password)
        expect(bad_reader, "OK auth")
        expect(bad_reader, "NAME choose a unique name")
        send_line(bad, "Temp")
        expect(bad_reader, "OK Temp")
        expect(bad_reader, "ROLE admin")
        expect(bad_reader, "ROOM lobby")
        while True:
            line = read_line(bad_reader, "history/end")
            if line == "END":
                break
        expect(bad_reader, "* Temp joined #lobby as admin")
        send_line(bad, "/quit")
        expect(bad_reader, "BYE")
        bad_reader.close()
        bad.close()
        readers.remove(bad_reader)
        clients.remove(bad)

        alice, alice_reader = open_client(port)
        clients.append(alice)
        readers.append(alice_reader)
        expect(alice_reader, "AUTH enter room password")
        send_line(alice, password)
        expect(alice_reader, "OK auth")
        expect(alice_reader, "NAME choose a unique name")
        send_line(alice, "Alice")
        expect(alice_reader, "OK Alice")
        expect(alice_reader, "ROLE admin")
        expect(alice_reader, "ROOM lobby")
        expect(alice_reader, "HISTORY 0")
        expect(alice_reader, "END")
        expect(alice_reader, "* Alice joined #lobby as admin")

        bob, bob_reader = open_client(port)
        clients.append(bob)
        readers.append(bob_reader)
        expect(bob_reader, "AUTH enter room password")
        send_line(bob, password)
        expect(bob_reader, "OK auth")
        expect(bob_reader, "NAME choose a unique name")
        send_line(bob, "Bob")
        expect(bob_reader, "OK Bob")
        expect(bob_reader, "ROLE member")
        expect(bob_reader, "ROOM lobby")
        expect(bob_reader, "HISTORY 0")
        expect(bob_reader, "END")
        expect(bob_reader, "* Bob joined #lobby as member")
        expect(alice_reader, "* Bob joined #lobby as member")

        wire = encrypt_body("hello", password)
        require(wire.startswith("#E#"), "encrypted body must use #E# prefix")
        require("hello" not in wire, "plaintext must not appear in ciphertext")
        send_line(alice, wire)
        expect_chat(alice_reader, "Alice", wire)
        expect_chat(bob_reader, "Alice", wire)

        send_line(bob, "/history")
        expect(bob_reader, "HISTORY 1")
        hist = read_line(bob_reader, "hist")
        require(wire in hist and "Alice:" in hist, f"bad encrypted history: {hist!r}")
        expect(bob_reader, "END")

        send_line(bob, "/quit")
        expect(bob_reader, "BYE")
        expect(alice_reader, "* Bob left #lobby")
        bob_reader.close()
        bob.close()
        readers.remove(bob_reader)
        clients.remove(bob)

        client = subprocess.run(
            [str(executable), str(client_source), "127.0.0.1", str(port)],
            input=password + "\nCli\nsecret-msg\n/quit\n",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=10,
        )
        require(client.returncode == 0, f"encrypted chat client exited with {client.returncode}: {client.stderr}")
        normalized = client.stdout.replace("\r\n", "\n")
        plain_out = strip_ansi(normalized)
        require("Password:" in plain_out, f"missing Password: field on AUTH:\n{normalized}")
        require("Name:" in plain_out, f"missing Name: field after AUTH:\n{normalized}")
        require("OK Cli" in normalized, f"client did not register:\n{normalized}")
        expect(alice_reader, "* Cli joined #lobby as member")
        expect_chat(alice_reader, "Cli", encrypt_body("secret-msg", password))
        expect(alice_reader, "* Cli left #lobby")
        require("Cli: " not in strip_ansi(normalized), f"own live chat line leaked into client stdout:\n{normalized}")

        time.sleep(0.1)
        require(server.poll() is None, "server exited during the encrypted smoke test")

        send_line(alice, "/quit")
        expect(alice_reader, "BYE")
    finally:
        for reader in readers:
            try:
                reader.close()
            except Exception:
                pass
        for sock in clients:
            try:
                sock.close()
            except Exception:
                pass
        stop_server(server)


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def wait_client_output(lines: queue.Queue[str], predicate, timeout: float = 5.0, collected: list[str] | None = None) -> str:
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = max(0.05, deadline - time.time())
        try:
            line = lines.get(timeout=remaining)
        except queue.Empty as error:
            raise RuntimeError("timed out waiting for client output") from error
        if collected is not None:
            collected.append(line)
        if predicate(line):
            return line
    raise RuntimeError("timed out waiting for matching client output")


def run_client_ui_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    """Drive the real chat_client entry point and check UI/prompt invariants."""
    password = "secret"
    server, _output_lines, port = start_server(executable, server_source, password)
    client: subprocess.Popen[str] | None = None
    peer: socket.socket | None = None
    peer_reader: object | None = None
    collected: list[str] = []

    try:
        client = subprocess.Popen(
            [str(executable), str(client_source), "127.0.0.1", str(port)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        require(client.stdin is not None and client.stdout is not None, "failed to pipe client stdio")
        client_lines: queue.Queue[str] = queue.Queue()
        start_output_reader(client.stdout, client_lines)

        wait_client_output(
            client_lines,
            lambda line: "AUTH enter room password" in strip_ansi(line),
            collected=collected,
        )
        client.stdin.write(password + "\n")
        client.stdin.flush()
        wait_client_output(client_lines, lambda line: "NAME choose a unique name" in strip_ansi(line), collected=collected)
        client.stdin.write("mag\n")
        client.stdin.flush()
        wait_client_output(client_lines, lambda line: "* mag joined" in strip_ansi(line), collected=collected)

        peer, peer_reader = open_client(port)
        expect(peer_reader, "AUTH enter room password")
        send_line(peer, password)
        expect(peer_reader, "OK auth")
        expect(peer_reader, "NAME choose a unique name")
        send_line(peer, "krul")
        expect(peer_reader, "OK krul")
        expect(peer_reader, "ROLE member")
        expect(peer_reader, "ROOM lobby")
        while True:
            line = read_line(peer_reader, "history/end")
            if line == "END":
                break
        expect(peer_reader, "* krul joined #lobby as member")

        wait_client_output(client_lines, lambda line: "* krul joined" in strip_ansi(line), collected=collected)

        wire = encrypt_body("bebro", password)
        send_line(peer, wire)
        expect_chat(peer_reader, "krul", wire)
        wait_client_output(
            client_lines,
            lambda line: "krul:" in strip_ansi(line) and "bebro" in strip_ansi(line),
            collected=collected,
        )

        client.stdin.write("hi\n")
        client.stdin.flush()
        expect_chat(peer_reader, "mag", encrypt_body("hi", password))

        client.stdin.write("/quit\n")
        client.stdin.flush()
        try:
            client.wait(timeout=5)
        except subprocess.TimeoutExpired:
            client.kill()
            client.wait(timeout=3)
            raise RuntimeError("client did not exit after /quit")

        while True:
            try:
                collected.append(client_lines.get(timeout=0.2))
            except queue.Empty:
                break

        plain = "\n".join(strip_ansi(line) for line in collected)
        full_raw = "\n".join(collected)

        banner_lines = [
            line
            for line in plain.splitlines()
            if line.startswith("+---") or (line.startswith("|") and "Chompo" in line) or (line.startswith("|") and "rooms" in line)
        ]
        require(len(banner_lines) >= 4, f"banner lines missing:\n{plain}")
        top = next(line for line in plain.splitlines() if line.startswith("+---"))
        title = next(line for line in plain.splitlines() if line.startswith("|") and "Chompo Chat" in line)
        sub = next(line for line in plain.splitlines() if line.startswith("|") and ("rooms" in line or "multi-user" in line))
        bot = [line for line in plain.splitlines() if line.startswith("+---")]
        require(len(bot) >= 2, "banner bottom missing")
        require(
            len(top) == len(title) == len(sub) == len(bot[1]),
            f"banner widths mismatch: {len(top)} {len(title)} {len(sub)} {len(bot[1])}\n{top}\n{title}\n{sub}\n{bot[1]}",
        )
        require(set(top[1:-1]) == {"-"}, f"banner top not ASCII dashes: {top!r}")
        require("┌" not in full_raw and "╔" not in full_raw, "legacy multi-byte box art still present")
        require("┌─ message" not in full_raw and "└────" not in full_raw, "old multi-line input frame still used")

        for line in plain.splitlines():
            require(not re.search(r">\s+\*", line), f"prompt smash with system notice: {line!r}")
            require(not re.search(r">\s+\S+:", line), f"prompt smash with chat line: {line!r}")

        mag_hi = [line for line in plain.splitlines() if re.search(r"\bmag:\s*hi\b", line)]
        require(len(mag_hi) == 0, f"own live chat should be suppressed, got {mag_hi}:\n{plain}")

        krul_bebro = [line for line in plain.splitlines() if "krul:" in line and "bebro" in line]
        require(len(krul_bebro) == 1, f"expected one krul:bebro line, got {krul_bebro}:\n{plain}")
        # Timestamp rendered on the left of the peer message.
        require(re.search(r"\[\d{2}:\d{2}:\d{2}\].*krul:", plain), f"expected timestamp near krul message:\n{plain}")

        require(client.returncode == 0, f"client exit {client.returncode}: {client.stderr.read() if client.stderr else ''}")
    finally:
        if peer_reader is not None:
            try:
                peer_reader.close()
            except Exception:
                pass
        if peer is not None:
            try:
                peer.close()
            except Exception:
                pass
        if client is not None and client.poll() is None:
            client.kill()
            client.wait(timeout=3)
        stop_server(server)


def run_no_prereg_broadcast_smoke(executable: Path, server_source: Path) -> None:
    """Live chat must not reach clients still on NAME; history delivers once after OK."""
    server, _output_lines, port = start_server(executable, server_source)
    clients: list[socket.socket] = []
    readers: list[object] = []

    try:
        alice, alice_reader = open_client(port)
        clients.append(alice)
        readers.append(alice_reader)
        register(alice_reader, alice, "Alice", expect_role="admin")
        expect(alice_reader, "* Alice joined #lobby as admin")

        send_line(alice, "early")
        expect_chat(alice_reader, "Alice", "early")

        bob, bob_reader = open_client(port)
        clients.append(bob)
        readers.append(bob_reader)
        expect(bob_reader, "NAME choose a unique name")

        send_line(alice, "during-name")
        expect_chat(alice_reader, "Alice", "during-name")
        time.sleep(0.2)
        ready, _, _ = select.select([bob], [], [], 0.2)
        require(not ready, "unnamed client must not receive live chat before registration")

        send_line(bob, "Bob")
        expect(bob_reader, "OK Bob")
        expect(bob_reader, "ROLE member")
        expect(bob_reader, "ROOM lobby")
        expect(bob_reader, "HISTORY 2")
        h1 = read_line(bob_reader, "h1")
        h2 = read_line(bob_reader, "h2")
        require("Alice: early" in h1, h1)
        require("Alice: during-name" in h2, h2)
        expect(bob_reader, "END")
        expect(bob_reader, "* Bob joined #lobby as member")
        expect(alice_reader, "* Bob joined #lobby as member")

        send_line(bob, "/history")
        expect(bob_reader, "HISTORY 2")
        read_line(bob_reader, "h1")
        read_line(bob_reader, "h2")
        expect(bob_reader, "END")

        send_line(alice, "/quit")
        expect(alice_reader, "BYE")
        expect(bob_reader, "* Alice left #lobby")
        # Last remaining user is auto-promoted to admin.
        expect(bob_reader, "* Bob is now admin")
        send_line(bob, "/quit")
        expect(bob_reader, "BYE")
    finally:
        for reader in readers:
            try:
                reader.close()
            except Exception:
                pass
        for sock in clients:
            try:
                sock.close()
            except Exception:
                pass
        stop_server(server)


def run_empty_name_retry_client(executable: Path, server_source: Path, client_source: Path) -> None:
    """Real client must re-prompt Name after empty name ERROR, then register."""
    server, _output_lines, port = start_server(executable, server_source)
    try:
        client = subprocess.run(
            [str(executable), str(client_source), "127.0.0.1", str(port)],
            input="\nRetryName\n/quit\n",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=10,
        )
        require(client.returncode == 0, f"client exit {client.returncode}: {client.stderr}")
        plain = strip_ansi(client.stdout.replace("\r\n", "\n"))
        require("ERROR name cannot be empty" in plain, f"missing empty-name error:\n{plain}")
        require("OK RetryName" in plain, f"did not register after retry:\n{plain}")
        require(plain.count("Name:") >= 2, f"expected Name: re-prompt after error:\n{plain}")
    finally:
        stop_server(server)


def run_wrong_password_retry_client(executable: Path, server_source: Path, client_source: Path) -> None:
    """Wrong password re-prompts Password:; Name: only after OK auth."""
    password = "secret"
    server, _output_lines, port = start_server(executable, server_source, password)
    try:
        client = subprocess.run(
            [str(executable), str(client_source), "127.0.0.1", str(port)],
            input="wrong\n" + password + "\nGoodUser\n/quit\n",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=10,
        )
        require(client.returncode == 0, f"client exit {client.returncode}: {client.stderr}")
        plain = strip_ansi(client.stdout.replace("\r\n", "\n"))
        require("ERROR wrong password" in plain, f"missing wrong-password error:\n{plain}")
        require("OK auth" in plain, f"missing OK auth after retry:\n{plain}")
        require("OK GoodUser" in plain, f"did not register after password retry:\n{plain}")
        require(plain.count("Password:") >= 2, f"expected Password: re-prompt:\n{plain}")
        auth_at = plain.find("OK auth")
        name_at = plain.find("Name:")
        require(auth_at >= 0 and name_at >= 0, f"missing auth/name markers:\n{plain}")
        require(name_at > auth_at, f"Name: appeared before OK auth:\n{plain}")
        require("Disconnected from server" not in plain, f"should not disconnect on wrong password:\n{plain}")
    finally:
        stop_server(server)


def run_rooms_moderation_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    """Rooms, roles, kick/ban/whitelist, mute (client), custom statuses — real server path."""
    server, _output_lines, port = start_server(executable, server_source)
    clients: list[socket.socket] = []
    readers: list[object] = []

    try:
        admin_sock, admin_r = open_client(port)
        clients.append(admin_sock)
        readers.append(admin_r)
        register(admin_r, admin_sock, "Admin", expect_role="admin")
        expect(admin_r, "* Admin joined #lobby as admin")

        bob_sock, bob_r = open_client(port)
        clients.append(bob_sock)
        readers.append(bob_r)
        register(bob_r, bob_sock, "Bob", expect_role="member")
        expect(bob_r, "* Bob joined #lobby as member")
        expect(admin_r, "* Bob joined #lobby as member")

        carol_sock, carol_r = open_client(port)
        clients.append(carol_sock)
        readers.append(carol_r)
        register(carol_r, carol_sock, "Carol", expect_role="member")
        expect(carol_r, "* Carol joined #lobby as member")
        expect(admin_r, "* Carol joined #lobby as member")
        expect(bob_r, "* Carol joined #lobby as member")

        # --- rooms ---
        send_line(bob_sock, "/join den")
        expect(bob_r, "ROOM den")
        drain_history(bob_r)
        expect(bob_r, "* Bob joined #den as member")
        expect(admin_r, "* Bob left #lobby")
        expect(carol_r, "* Bob left #lobby")
        # Carol still in lobby — must not see Bob's den join broadcast (only leave on lobby).
        # Bob's join is only in #den.
        send_line(admin_sock, "/rooms")
        rooms_line = read_line(admin_r, "rooms")
        require(rooms_line.startswith("ROOMS "), rooms_line)
        require("#lobby" in rooms_line and "#den" in rooms_line, rooms_line)

        send_line(bob_sock, "secret-den")
        expect_chat(bob_r, "Bob", "secret-den")
        time.sleep(0.15)
        ready, _, _ = select.select([admin_sock], [], [], 0.15)
        require(not ready, "lobby admin must not receive den messages")

        send_line(admin_sock, "/join den")
        expect(admin_r, "ROOM den")
        hist = drain_history(admin_r)
        require(any("Bob: secret-den" in m for m in hist), f"den history missing: {hist}")
        expect(admin_r, "* Admin joined #den as admin")
        expect(bob_r, "* Admin joined #den as admin")

        # --- status create / set ---
        send_line(admin_sock, "/status create moderator")
        expect(admin_r, "OK status created moderator")
        expect(admin_r, "* status created: moderator")
        expect(bob_r, "* status created: moderator")

        send_line(admin_sock, "/status set Bob moderator")
        expect(bob_r, "ROLE moderator")
        # broadcast to all registered
        notice = read_line(admin_r, "status notice")
        require("Bob status:" in notice and "moderator" in notice, notice)
        notice_b = read_line(bob_r, "status notice bob")
        require("Bob status:" in notice_b, notice_b)

        send_line(admin_sock, "/status list")
        statuses = read_line(admin_r, "statuses")
        require(statuses.startswith("STATUSES "), statuses)
        require("admin" in statuses and "member" in statuses and "moderator" in statuses, statuses)

        # Member cannot kick.
        send_line(bob_sock, "/kick Carol")
        expect(bob_r, "ERROR admin only")

        # --- kick ---
        # Carol may still have lobby leave/status broadcasts buffered.
        send_line(admin_sock, "/kick Carol")
        expect_eventually(carol_r, "ERROR kicked by Admin")
        expect(carol_r, "BYE")
        # Carol was in lobby; kick notice is lobby-only (Admin/Bob are in #den).
        carol_r.close()
        carol_sock.close()
        readers.remove(carol_r)
        clients.remove(carol_sock)

        # --- ban ---
        send_line(admin_sock, "/ban Bob")
        expect_eventually(bob_r, "ERROR banned by Admin")
        expect(bob_r, "BYE")
        expect_eventually(admin_r, "OK banned Bob")
        # ban also broadcasts "* Bob is banned" after OK
        expect(admin_r, "* Bob is banned")
        bob_r.close()
        bob_sock.close()
        readers.remove(bob_r)
        clients.remove(bob_sock)

        banned_sock, banned_r = open_client(port)
        clients.append(banned_sock)
        readers.append(banned_r)
        expect(banned_r, "NAME choose a unique name")
        send_line(banned_sock, "Bob")
        expect(banned_r, "ERROR name is banned")
        send_line(banned_sock, "Dave")
        expect(banned_r, "OK Dave")
        expect(banned_r, "ROLE member")
        expect(banned_r, "ROOM lobby")
        drain_history(banned_r)
        expect(banned_r, "* Dave joined #lobby as member")

        send_line(admin_sock, "/unban Bob")
        expect(admin_r, "OK unbanned Bob")
        expect(admin_r, "* Bob is unbanned")
        # Dave is in lobby and also receives unban broadcast.
        expect(banned_r, "* Bob is unbanned")

        # --- whitelist ---
        send_line(admin_sock, "/whitelist add Eve")
        expect(admin_r, "OK whitelist add Eve")
        send_line(admin_sock, "/whitelist on")
        expect(admin_r, "OK whitelist on")
        expect(admin_r, "* whitelist enabled")
        expect(banned_r, "* whitelist enabled")
        # Dave is already online; new non-whitelisted names blocked.
        fox_sock, fox_r = open_client(port)
        clients.append(fox_sock)
        readers.append(fox_r)
        expect(fox_r, "NAME choose a unique name")
        send_line(fox_sock, "Fox")
        expect(fox_r, "ERROR name is not on the whitelist")
        send_line(fox_sock, "Eve")
        expect(fox_r, "OK Eve")
        expect(fox_r, "ROLE member")
        expect(fox_r, "ROOM lobby")
        drain_history(fox_r)
        expect(fox_r, "* Eve joined #lobby as member")

        send_line(admin_sock, "/whitelist off")
        expect(admin_r, "OK whitelist off")
        expect(admin_r, "* whitelist disabled")

        # --- client mute via real client process ---
        # Admin returns to lobby for the mute demo.
        send_line(admin_sock, "/join lobby")
        expect(admin_r, "ROOM lobby")
        drain_history(admin_r)
        # join notice (Admin sees own join; lobby peers also do)
        expect_prefix(admin_r, "* Admin joined #lobby")

        mute_client = subprocess.run(
            [str(executable), str(client_source), "127.0.0.1", str(port)],
            input="MuteUser\n/mute Dave\nignored-local\n/mutes\n/unmute Dave\n/quit\n",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=12,
        )
        require(mute_client.returncode == 0, f"mute client exit {mute_client.returncode}: {mute_client.stderr}")
        mute_plain = strip_ansi(mute_client.stdout.replace("\r\n", "\n"))
        require("muted Dave" in mute_plain, f"mute confirm missing:\n{mute_plain}")
        require("muted: Dave" in mute_plain or "muted:Dave" in mute_plain.replace(" ", ""), f"mutes list:\n{mute_plain}")
        require("unmuted Dave" in mute_plain, f"unmute confirm missing:\n{mute_plain}")
        require("OK MuteUser" in mute_plain, mute_plain)

        # Structural: client source contains mute helpers; server contains admin gates.
        client_src = client_source.read_text(encoding="utf-8")
        server_src = server_source.read_text(encoding="utf-8")
        require("/mute" in client_src and "isMutedChat" in client_src, "client mute path missing")
        require("requireAdmin" in server_src and "/kick" in server_src and "/ban" in server_src, "server moderation missing")
        require("formatTime" in server_src and "clockTime" in server_src, "timestamp path missing")
        require("cmdJoin" in server_src or "/join" in server_src, "rooms path missing")
        require("statusCatalog" in server_src and "cmdStatusCreate" in server_src, "custom status path missing")
        require("var banned = Map" in server_src and "mapHas" in server_src, "server Map-backed bans missing")
        require("clockTime" in client_src or "formatTime" in server_src, "timestamp builtin path missing")

        # MuteUser may have joined/left while admin was idle — drain to BYE.
        send_line(admin_sock, "/quit")
        expect_eventually(admin_r, "BYE")
    finally:
        for reader in readers:
            try:
                reader.close()
            except Exception:
                pass
        for sock in clients:
            try:
                sock.close()
            except Exception:
                pass
        stop_server(server)


def run_clock_time_unit(executable: Path) -> None:
    """Unit-test the shipped clockTime builtin (used for message timestamps)."""
    prog = 'var t = clockTime(); print(len(t), " ", t[0], " ", t[1], " ", t[2], "\\n");'
    # Write a tiny program next to the binary invocation via stdin is not supported —
    # use a temp file under the goal scratch via the caller, or embed with -c if any.
    # Chompo takes a source file path.
    import tempfile

    with tempfile.NamedTemporaryFile("w", suffix=".chmp", delete=False, encoding="utf-8") as handle:
        handle.write(prog)
        path = handle.name
    try:
        result = subprocess.run(
            [str(executable), path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            timeout=5,
        )
        require(result.returncode == 0, f"clockTime program failed: {result.stderr}")
        parts = result.stdout.strip().split()
        require(len(parts) == 4, f"unexpected clockTime output: {result.stdout!r}")
        length, hour, minute, second = (int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3]))
        require(length == 3, f"clockTime must return 3 fields, got {length}")
        require(0 <= hour <= 23, f"hour out of range: {hour}")
        require(0 <= minute <= 59, f"minute out of range: {minute}")
        require(0 <= second <= 59, f"second out of range: {second}")
    finally:
        os.unlink(path)


def run_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    run_clock_time_unit(executable)
    run_plaintext_smoke(executable, server_source, client_source)
    run_encrypted_smoke(executable, server_source, client_source)
    run_client_ui_smoke(executable, server_source, client_source)
    run_no_prereg_broadcast_smoke(executable, server_source)
    run_empty_name_retry_client(executable, server_source, client_source)
    run_wrong_password_retry_client(executable, server_source, client_source)
    run_rooms_moderation_smoke(executable, server_source, client_source)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    arguments = parser.parse_args()

    run_smoke(arguments.executable.resolve(), arguments.server.resolve(), arguments.client.resolve())
    print("LangJam chat smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
