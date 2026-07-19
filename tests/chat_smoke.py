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

    # Banner may precede the machine-readable LISTENING line.
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
        expect(alice_reader, "NAME choose a unique name")
        send_line(alice, "Alice")
        expect(alice_reader, "OK Alice")
        expect(alice_reader, "HISTORY 0")
        expect(alice_reader, "END")
        expect(alice_reader, "* Alice joined")

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
        expect(bob_reader, "HISTORY 0")
        expect(bob_reader, "END")
        expect(bob_reader, "* Bob joined")
        expect(alice_reader, "* Bob joined")

        send_line(alice, "hello")
        expect(alice_reader, "Alice: hello")
        expect(bob_reader, "Alice: hello")

        send_line(bob, "/history")
        expect(bob_reader, "HISTORY 1")
        expect(bob_reader, "Alice: hello")
        expect(bob_reader, "END")

        send_line(bob, "/help")
        expect(bob_reader, "COMMANDS /help /history /quit")

        send_line(bob, "/quit")
        expect(bob_reader, "BYE")
        expect(alice_reader, "* Bob left")
        bob_reader.close()
        bob.close()
        readers.remove(bob_reader)
        clients.remove(bob)

        eve, eve_reader = open_client(port)
        expect(eve_reader, "NAME choose a unique name")
        send_line(eve, "Eve")
        expect(eve_reader, "OK Eve")
        expect(eve_reader, "HISTORY 1")
        expect(eve_reader, "Alice: hello")
        expect(eve_reader, "END")
        expect(eve_reader, "* Eve joined")
        expect(alice_reader, "* Eve joined")
        eve_reader.close()
        abrupt_close(eve)
        expect(alice_reader, "* Eve left")

        # Keep Alice online to observe the Chompo client's outbound message
        # (client suppresses local echo of its own live chat lines).
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
        # Own live chat is not re-printed; peer must see the wire line.
        expect(alice_reader, "* Cli joined")
        expect(alice_reader, "Cli: from-client")
        expect(alice_reader, "* Cli left")
        # Client must not duplicate its own chat line in stdout.
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
        # AUTH line may be colored; scan a few startup lines.
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
        # Connection stays open for password retry.
        send_line(bad, "still-wrong")
        expect(bad_reader, "ERROR wrong password")
        send_line(bad, password)
        expect(bad_reader, "OK auth")
        expect(bad_reader, "NAME choose a unique name")
        send_line(bad, "Temp")
        expect(bad_reader, "OK Temp")
        # drain history + join
        while True:
            line = read_line(bad_reader, "history/end")
            if line == "END":
                break
        expect(bad_reader, "* Temp joined")
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
        expect(alice_reader, "HISTORY 0")
        expect(alice_reader, "END")
        expect(alice_reader, "* Alice joined")

        bob, bob_reader = open_client(port)
        clients.append(bob)
        readers.append(bob_reader)
        expect(bob_reader, "AUTH enter room password")
        send_line(bob, password)
        expect(bob_reader, "OK auth")
        expect(bob_reader, "NAME choose a unique name")
        send_line(bob, "Bob")
        expect(bob_reader, "OK Bob")
        expect(bob_reader, "HISTORY 0")
        expect(bob_reader, "END")
        expect(bob_reader, "* Bob joined")
        expect(alice_reader, "* Bob joined")

        wire = encrypt_body("hello", password)
        require(wire.startswith("#E#"), "encrypted body must use #E# prefix")
        require("hello" not in wire, "plaintext must not appear in ciphertext")
        send_line(alice, wire)
        expect(alice_reader, f"Alice: {wire}")
        expect(bob_reader, f"Alice: {wire}")

        send_line(bob, "/history")
        expect(bob_reader, "HISTORY 1")
        expect(bob_reader, f"Alice: {wire}")
        expect(bob_reader, "END")

        send_line(bob, "/quit")
        expect(bob_reader, "BYE")
        expect(alice_reader, "* Bob left")
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
        require("OK auth" in normalized or "OK Cli" in normalized, f"client did not auth/register:\n{normalized}")
        require("OK Cli" in normalized, f"client did not register:\n{normalized}")
        # Own live echo suppressed; peer observes ciphertext on the wire.
        expect(alice_reader, "* Cli joined")
        expect(alice_reader, "Cli: " + encrypt_body("secret-msg", password))
        expect(alice_reader, "* Cli left")
        require(
            "secret-msg" not in strip_ansi(normalized) or normalized.count("Cli:") == 0,
            f"own live chat should not be re-printed as Cli: secret-msg:\n{normalized}",
        )
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

        # Password: is printed without a trailing newline (like Name:); wait for
        # the AUTH control line, then type the password into the field.
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

        # Client should now sit at a single-line prompt. Peer joins + chats
        # while mag has not typed yet — must not smash into the prompt line.
        peer, peer_reader = open_client(port)
        expect(peer_reader, "AUTH enter room password")
        send_line(peer, password)
        expect(peer_reader, "OK auth")
        expect(peer_reader, "NAME choose a unique name")
        send_line(peer, "krul")
        expect(peer_reader, "OK krul")
        # drain history block
        while True:
            line = read_line(peer_reader, "history/end")
            if line == "END":
                break
        expect(peer_reader, "* krul joined")

        wait_client_output(client_lines, lambda line: "* krul joined" in strip_ansi(line), collected=collected)

        wire = encrypt_body("bebro", password)
        send_line(peer, wire)
        # Raw peer also receives its own server echo; drain it.
        expect(peer_reader, "krul: " + wire)
        wait_client_output(
            client_lines,
            lambda line: "krul:" in strip_ansi(line) and "bebro" in strip_ansi(line),
            collected=collected,
        )

        client.stdin.write("hi\n")
        client.stdin.flush()
        # Peer must see mag's live message; mag's client suppresses own live echo.
        expect(peer_reader, "mag: " + encrypt_body("hi", password))

        client.stdin.write("/quit\n")
        client.stdin.flush()
        try:
            client.wait(timeout=5)
        except subprocess.TimeoutExpired:
            client.kill()
            client.wait(timeout=3)
            raise RuntimeError("client did not exit after /quit")

        # Drain remaining stdout.
        while True:
            try:
                collected.append(client_lines.get(timeout=0.2))
            except queue.Empty:
                break

        plain = "\n".join(strip_ansi(line) for line in collected)
        full_raw = "\n".join(collected)

        # --- Banner: equal-width ASCII box ---
        banner_lines = [line for line in plain.splitlines() if line.startswith("+---") or (line.startswith("|") and "Chompo" in line) or (line.startswith("|") and "multi-user" in line)]
        require(len(banner_lines) >= 4, f"banner lines missing:\n{plain}")
        # Take the first contiguous banner block
        top = next(line for line in plain.splitlines() if line.startswith("+---"))
        title = next(line for line in plain.splitlines() if line.startswith("|") and "Chompo Chat" in line)
        sub = next(line for line in plain.splitlines() if line.startswith("|") and "multi-user" in line)
        bot = [line for line in plain.splitlines() if line.startswith("+---")]
        require(len(bot) >= 2, "banner bottom missing")
        require(len(top) == len(title) == len(sub) == len(bot[1]), f"banner widths mismatch: {len(top)} {len(title)} {len(sub)} {len(bot[1])}\n{top}\n{title}\n{sub}\n{bot[1]}")
        require(set(top[1:-1]) == {"-"}, f"banner top not ASCII dashes: {top!r}")
        require("┌" not in full_raw and "╔" not in full_raw, "legacy multi-byte box art still present")

        # --- No multi-line open input frame ---
        require("┌─ message" not in full_raw and "└────" not in full_raw, "old multi-line input frame still used")

        # --- Prompt smash: peer events must not share a line with "> " ---
        for line in plain.splitlines():
            require(not re.search(r">\s+\*", line), f"prompt smash with system notice: {line!r}")
            require(not re.search(r">\s+\S+:", line), f"prompt smash with chat line: {line!r}")

        # --- Own live chat suppressed (no mag: hi on own screen) ---
        mag_hi = [line for line in plain.splitlines() if re.search(r"\bmag:\s*hi\b", line)]
        require(len(mag_hi) == 0, f"own live chat should be suppressed, got {mag_hi}:\n{plain}")

        # Peer message decrypted once
        krul_bebro = [line for line in plain.splitlines() if "krul:" in line and "bebro" in line]
        require(len(krul_bebro) == 1, f"expected one krul:bebro line, got {krul_bebro}:\n{plain}")

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
        expect(alice_reader, "NAME choose a unique name")
        send_line(alice, "Alice")
        expect(alice_reader, "OK Alice")
        expect(alice_reader, "HISTORY 0")
        expect(alice_reader, "END")
        expect(alice_reader, "* Alice joined")

        send_line(alice, "early")
        expect(alice_reader, "Alice: early")

        bob, bob_reader = open_client(port)
        clients.append(bob)
        readers.append(bob_reader)
        expect(bob_reader, "NAME choose a unique name")

        # Bob has not registered yet — must not receive live room traffic.
        send_line(alice, "during-name")
        expect(alice_reader, "Alice: during-name")
        time.sleep(0.2)
        ready, _, _ = select.select([bob], [], [], 0.2)
        require(not ready, "unnamed client must not receive live chat before registration")

        send_line(bob, "Bob")
        expect(bob_reader, "OK Bob")
        expect(bob_reader, "HISTORY 2")
        expect(bob_reader, "Alice: early")
        expect(bob_reader, "Alice: during-name")
        expect(bob_reader, "END")
        expect(bob_reader, "* Bob joined")
        expect(alice_reader, "* Bob joined")

        # Each history line once; no pre-OK duplicate of during-name.
        send_line(bob, "/history")
        expect(bob_reader, "HISTORY 2")
        expect(bob_reader, "Alice: early")
        expect(bob_reader, "Alice: during-name")
        expect(bob_reader, "END")

        send_line(alice, "/quit")
        expect(alice_reader, "BYE")
        expect(bob_reader, "* Alice left")
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
        # Name prompt should appear more than once (initial + after error).
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
        # Name: must not appear before the successful OK auth line.
        auth_at = plain.find("OK auth")
        name_at = plain.find("Name:")
        require(auth_at >= 0 and name_at >= 0, f"missing auth/name markers:\n{plain}")
        require(name_at > auth_at, f"Name: appeared before OK auth:\n{plain}")
        require("Disconnected from server" not in plain, f"should not disconnect on wrong password:\n{plain}")
    finally:
        stop_server(server)


def run_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    run_plaintext_smoke(executable, server_source, client_source)
    run_encrypted_smoke(executable, server_source, client_source)
    run_client_ui_smoke(executable, server_source, client_source)
    run_no_prereg_broadcast_smoke(executable, server_source)
    run_empty_name_retry_client(executable, server_source, client_source)
    run_wrong_password_retry_client(executable, server_source, client_source)


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
