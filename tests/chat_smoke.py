#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import queue
import socket
import struct
import subprocess
import threading
import time
from pathlib import Path


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


def run_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    server = subprocess.Popen(
        [str(executable), str(server_source), "127.0.0.1", "0", "3"],
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

    clients: list[socket.socket] = []
    readers: list[object] = []

    try:
        first_line = wait_server_line(output_lines)
        require(first_line.startswith("LISTENING "), f"unexpected server startup line: {first_line!r}")
        port = int(first_line.split(" ", 1)[1])
        require(0 < port <= 65535, f"invalid listening port {port}")

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

        send_line(alice, "/quit")
        expect(alice_reader, "BYE")
        alice_reader.close()
        alice.close()
        readers.remove(alice_reader)
        clients.remove(alice)

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
        require("Cli: from-client" in normalized, f"client did not receive its broadcast:\n{normalized}")

        time.sleep(0.1)
        require(server.poll() is None, "server exited during the smoke test")
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
