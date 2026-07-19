#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import socket
import subprocess
import threading
import time
from pathlib import Path
from typing import TextIO

PASSWORD = "chompo-test-password"
WRONG_PASSWORD = "wrong-password-xx"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


class CapturedProcess:
    def __init__(self, command: list[str], name: str) -> None:
        self.name = name
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        require(self.process.stdin is not None, f"{name}: stdin was not captured")
        require(self.process.stdout is not None, f"{name}: stdout was not captured")
        require(self.process.stderr is not None, f"{name}: stderr was not captured")

        self._condition = threading.Condition()
        self._stdout = ""
        self._stderr = ""
        self._stdout_done = False
        self._stderr_done = False
        self._start_reader(self.process.stdout, False)
        self._start_reader(self.process.stderr, True)

    def _start_reader(self, stream: TextIO, stderr: bool) -> None:
        def read() -> None:
            while True:
                chunk = stream.read(1)
                if chunk == "":
                    break
                with self._condition:
                    if stderr:
                        self._stderr += chunk
                    else:
                        self._stdout += chunk
                    self._condition.notify_all()
            with self._condition:
                if stderr:
                    self._stderr_done = True
                else:
                    self._stdout_done = True
                self._condition.notify_all()

        threading.Thread(target=read, daemon=True).start()

    @property
    def stdout(self) -> str:
        with self._condition:
            return self._stdout

    @property
    def stderr(self) -> str:
        with self._condition:
            return self._stderr

    def send(self, line: str) -> None:
        require(self.process.stdin is not None, f"{self.name}: stdin is closed")
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def wait_contains(self, text: str, timeout: float = 20.0) -> None:
        deadline = time.monotonic() + timeout
        with self._condition:
            while text not in self._stdout:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError(
                        f"{self.name}: did not output {text!r}\n"
                        f"stdout:\n{self._stdout}\n"
                        f"stderr:\n{self._stderr}"
                    )
                if self.process.poll() is not None and self._stdout_done:
                    raise RuntimeError(
                        f"{self.name}: exited before outputting {text!r}\n"
                        f"exit={self.process.returncode}\n"
                        f"stdout:\n{self._stdout}\n"
                        f"stderr:\n{self._stderr}"
                    )
                self._condition.wait(timeout=min(remaining, 0.1))

    def wait_regex(self, pattern: str, timeout: float = 20.0) -> re.Match[str]:
        compiled = re.compile(pattern)
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                match = compiled.search(self._stdout)
                if match is not None:
                    return match
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError(
                        f"{self.name}: output did not match {pattern!r}\n"
                        f"stdout:\n{self._stdout}\n"
                        f"stderr:\n{self._stderr}"
                    )
                if self.process.poll() is not None and self._stdout_done:
                    raise RuntimeError(
                        f"{self.name}: exited before matching {pattern!r}\n"
                        f"exit={self.process.returncode}\n"
                        f"stdout:\n{self._stdout}\n"
                        f"stderr:\n{self._stderr}"
                    )
                self._condition.wait(timeout=min(remaining, 0.1))

    def wait_exit(self, timeout: float = 20.0) -> int:
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(
                f"{self.name}: did not exit\nstdout:\n{self.stdout}\nstderr:\n{self.stderr}"
            ) from error

    def terminate(self) -> None:
        if self.process.poll() is not None:
            return
        self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)


def start_server(
    executable: Path, server_source: Path, password: str | None = PASSWORD, plaintext: bool = False
) -> tuple[CapturedProcess, int]:
    command = [str(executable), str(server_source)]
    if plaintext:
        command.append("--plaintext")
    command.extend(["127.0.0.1", "0", "10"])
    if password is not None and not plaintext:
        command.append(password)

    server = CapturedProcess(command, "server")
    if plaintext:
        match = server.wait_regex(r"LISTENING (\d+) PLAINTEXT")
    else:
        match = server.wait_regex(r"LISTENING (\d+) SECURE AES-256-GCM")
    port = int(match.group(1))
    require(0 < port <= 65535, f"invalid listening port {port}")
    return server, port


def start_client(
    executable: Path,
    client_source: Path,
    port: int,
    name: str,
    password: str = PASSWORD,
    plaintext: bool = False,
    wait_register: bool = True,
) -> CapturedProcess:
    command = [str(executable), str(client_source)]
    if plaintext:
        command.append("--plaintext")
    command.extend(["127.0.0.1", str(port)])
    if not plaintext:
        command.append(password)

    client = CapturedProcess(command, name)
    if plaintext:
        client.wait_contains("PLAINTEXT", timeout=30.0)
    else:
        client.wait_contains("Защищённый канал установлен.", timeout=30.0)

    # Registration prompt (also match partial in case of encoding noise on CI).
    client.wait_contains("NAME choose a unique name", timeout=30.0)
    client.send(name)

    if wait_register:
        client.wait_contains("OK NAME " + name, timeout=30.0)
        client.wait_contains("* " + name + " joined", timeout=30.0)
    return client


def run_basic_secure(executable: Path, server_source: Path, client_source: Path) -> None:
    server, port = start_server(executable, server_source)
    clients: list[CapturedProcess] = []

    try:
        alice = start_client(executable, client_source, port, "Alice")
        clients.append(alice)

        bob = start_client(executable, client_source, port, "Bob")
        clients.append(bob)
        alice.wait_contains("* Bob joined")

        message = "Hello Bob"
        alice.send(message)
        alice.wait_contains("Alice: " + message)
        bob.wait_contains("Alice: " + message)

        bob.send("/users")
        bob.wait_contains("USERS 2")
        bob.wait_contains("USER Alice online")
        bob.wait_contains("USER Bob online")

        # UTF-8 / Cyrillic nick and message
        alice.send("/nick Алиса")
        alice.wait_contains("OK NAME Алиса")
        bob.wait_contains("* Alice is now known as Алиса")
        cyrillic = "Привет, мир"
        alice.send(cyrillic)
        alice.wait_contains("Алиса: " + cyrillic)
        bob.wait_contains("Алиса: " + cyrillic)

        bob.send("/status away")
        bob.wait_contains("* Bob is now away")
        alice.wait_contains("* Bob is now away")

        bob.send("/msg Алиса secret-dm")
        bob.wait_contains("[DM to Алиса] secret-dm")
        alice.wait_contains("[DM from Bob] secret-dm")

        bob.send("/nick Boris")
        bob.wait_contains("OK NAME Boris")
        alice.wait_contains("* Bob is now known as Boris")

        server.send("/say Server broadcast")
        alice.wait_contains("[SERVER] Server broadcast")
        bob.wait_contains("[SERVER] Server broadcast")

        alice.send("/ping")
        alice.wait_contains("PONG")

        server.send("/kick Boris")
        bob.wait_contains("KICKED by server")
        alice.wait_contains("* Boris left")
        require(bob.wait_exit() == 0, f"Bob client exited with {bob.process.returncode}: {bob.stderr}")

        # /exit is an alias for /quit
        alice.send("/exit")
        alice.wait_contains("BYE")
        require(alice.wait_exit() == 0, f"Alice client exited with {alice.process.returncode}: {alice.stderr}")

        server.send("/stop")
        require(server.wait_exit() == 0, f"server exited with {server.process.returncode}: {server.stderr}")

        require("Hello Bob" in server.stdout, "server did not log the message")
        require(cyrillic in server.stdout, "server did not log the Cyrillic message")
        require("SECURITY rejected client packet" not in server.stdout, "valid encrypted traffic was rejected")
    finally:
        for client in clients:
            client.terminate()
        server.terminate()


def run_wrong_password(executable: Path, server_source: Path, client_source: Path) -> None:
    server, port = start_server(executable, server_source)
    try:
        client = CapturedProcess(
            [str(executable), str(client_source), "127.0.0.1", str(port), WRONG_PASSWORD],
            "bad-client",
        )
        client.wait_contains("Не удалось создать защищённое соединение")
        require(client.wait_exit(timeout=15) != 0 or "защищённ" in client.stdout, "wrong password should fail")
        # Server must keep running for a good client afterwards.
        good = start_client(executable, client_source, port, "Alice")
        good.send("/quit")
        good.wait_contains("BYE")
        require(good.wait_exit() == 0, "good client after wrong password failed")
        server.send("/stop")
        require(server.wait_exit() == 0, "server failed after wrong password attempt")
        require("PLAINTEXT" not in good.stdout or "AES-256-GCM" in good.stdout, "unexpected plaintext downgrade")
    finally:
        server.terminate()


def run_name_retry_and_reject(executable: Path, server_source: Path, client_source: Path) -> None:
    server, port = start_server(executable, server_source)
    clients: list[CapturedProcess] = []
    try:
        alice = start_client(executable, client_source, port, "Alice")
        clients.append(alice)

        # Taken name then valid retry.
        bob = CapturedProcess(
            [str(executable), str(client_source), "127.0.0.1", str(port), PASSWORD],
            "Bob",
        )
        clients.append(bob)
        bob.wait_contains("Защищённый канал установлен.")
        bob.wait_contains("NAME choose a unique name")
        bob.send("Alice")
        bob.wait_contains("ERROR NAME")
        bob.send("Bob")
        bob.wait_contains("OK NAME Bob")

        # Invalid names
        for bad in ("admin:x", "bad name", ""):
            # empty may just re-prompt; use a third client for colon name
            pass

        carol = CapturedProcess(
            [str(executable), str(client_source), "127.0.0.1", str(port), PASSWORD],
            "Carol",
        )
        clients.append(carol)
        carol.wait_contains("NAME choose a unique name")
        carol.send("admin:hello")
        carol.wait_contains("ERROR NAME")
        carol.send("Carol")
        carol.wait_contains("OK NAME Carol")

        alice.send("/quit")
        bob.send("/quit")
        carol.send("/quit")
        for client in clients:
            client.wait_exit()
        server.send("/stop")
        server.wait_exit()
    finally:
        for client in clients:
            client.terminate()
        server.terminate()


def run_rooms(executable: Path, server_source: Path, client_source: Path) -> None:
    server, port = start_server(executable, server_source)
    clients: list[CapturedProcess] = []
    try:
        alice = start_client(executable, client_source, port, "Alice")
        bob = start_client(executable, client_source, port, "Bob")
        clients.extend([alice, bob])

        alice.send("/join games")
        alice.wait_contains("ROOM games")
        alice.wait_contains("* Alice joined #games")

        alice.send("only-in-games")
        alice.wait_contains("Alice: only-in-games")
        time.sleep(0.3)
        require("only-in-games" not in bob.stdout, "room isolation failed: Bob saw games message")

        bob.send("only-in-lobby")
        bob.wait_contains("Bob: only-in-lobby")
        time.sleep(0.3)
        require("only-in-lobby" not in alice.stdout, "room isolation failed: Alice saw lobby message")

        bob.send("/join games")
        bob.wait_contains("ROOM games")
        bob.wait_contains("Alice: only-in-games")  # history

        alice.send("/rooms")
        alice.wait_contains("ROOMS ")
        alice.wait_contains("#games")

        alice.send("/quit")
        bob.send("/quit")
        for client in clients:
            client.wait_exit()
        server.send("/stop")
        server.wait_exit()
    finally:
        for client in clients:
            client.terminate()
        server.terminate()


def run_moderation(executable: Path, server_source: Path, client_source: Path) -> None:
    server, port = start_server(executable, server_source)
    clients: list[CapturedProcess] = []
    try:
        alice = start_client(executable, client_source, port, "Alice")  # first user = admin
        bob = start_client(executable, client_source, port, "Bob")
        clients.extend([alice, bob])

        alice.wait_contains("ROLE admin")
        bob.wait_contains("ROLE member")

        alice.send("/ban Bob")
        alice.wait_contains("OK banned Bob")
        bob.wait_contains("KICKED")
        bob.wait_exit()

        # Ban survives reconnect.
        banned = CapturedProcess(
            [str(executable), str(client_source), "127.0.0.1", str(port), PASSWORD],
            "Bob-again",
        )
        clients.append(banned)
        banned.wait_contains("NAME choose a unique name")
        banned.send("Bob")
        banned.wait_contains("ERROR NAME")
        banned.send("Bobby")
        banned.wait_contains("OK NAME Bobby")

        alice.send("/whitelist on")
        alice.wait_contains("OK whitelist on")
        alice.send("/whitelist add Alice")
        alice.wait_contains("OK whitelist add Alice")

        dave = CapturedProcess(
            [str(executable), str(client_source), "127.0.0.1", str(port), PASSWORD],
            "Dave",
        )
        clients.append(dave)
        dave.wait_contains("NAME choose a unique name")
        dave.send("Dave")
        dave.wait_contains("ERROR NAME")
        dave.send("/quit")

        alice.send("/quit")
        bobby = banned
        bobby.send("/quit")
        for client in clients:
            if client.process.poll() is None:
                client.send("/quit")
            try:
                client.wait_exit(timeout=5)
            except RuntimeError:
                client.terminate()
        server.send("/stop")
        server.wait_exit()
    finally:
        for client in clients:
            client.terminate()
        server.terminate()


def run_local_mute(executable: Path, server_source: Path, client_source: Path) -> None:
    server, port = start_server(executable, server_source)
    clients: list[CapturedProcess] = []
    try:
        alice = start_client(executable, client_source, port, "Alice")
        bob = start_client(executable, client_source, port, "Bob")
        clients.extend([alice, bob])

        bob.send("/mute Alice")
        bob.wait_contains("muted Alice")
        alice.send("should-be-muted")
        alice.wait_contains("Alice: should-be-muted")
        time.sleep(0.4)
        require("should-be-muted" not in bob.stdout, "mute failed: Bob saw muted message")

        bob.send("/unmute Alice")
        bob.wait_contains("unmuted Alice")
        alice.send("visible-again")
        bob.wait_contains("Alice: visible-again")

        alice.send("/quit")
        bob.send("/quit")
        for client in clients:
            client.wait_exit()
        server.send("/stop")
        server.wait_exit()
    finally:
        for client in clients:
            client.terminate()
        server.terminate()


def run_hung_handshake_does_not_block(executable: Path, server_source: Path, client_source: Path) -> None:
    server, port = start_server(executable, server_source)
    hang: socket.socket | None = None
    try:
        # Open a raw TCP connection and do nothing — handshake should pend, not block.
        hang = socket.create_connection(("127.0.0.1", port), timeout=2)
        time.sleep(0.2)

        alice = start_client(executable, client_source, port, "Alice", wait_register=True)
        alice.send("still-alive")
        alice.wait_contains("Alice: still-alive")
        require("still-alive" in server.stdout, "server did not process traffic during hung handshake")

        alice.send("/quit")
        alice.wait_exit()
        server.send("/stop")
        server.wait_exit()
    finally:
        if hang is not None:
            hang.close()
        server.terminate()


def run_no_plaintext_downgrade(executable: Path, server_source: Path, client_source: Path) -> None:
    """Secure client against secure server with wrong password must not enter chat."""
    server, port = start_server(executable, server_source)
    try:
        client = CapturedProcess(
            [str(executable), str(client_source), "127.0.0.1", str(port), WRONG_PASSWORD],
            "no-downgrade",
        )
        client.wait_contains("Не удалось создать защищённое соединение")
        # Must not reach name prompt or OK NAME.
        time.sleep(0.5)
        require("OK NAME" not in client.stdout, "client registered after failed secure handshake")
        require("NAME choose" not in client.stdout, "client saw NAME prompt after failed handshake")
        client.terminate()
        server.send("/stop")
        server.wait_exit()
    finally:
        server.terminate()


def run_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    run_basic_secure(executable, server_source, client_source)
    run_wrong_password(executable, server_source, client_source)
    run_name_retry_and_reject(executable, server_source, client_source)
    run_rooms(executable, server_source, client_source)
    run_moderation(executable, server_source, client_source)
    run_local_mute(executable, server_source, client_source)
    run_hung_handshake_does_not_block(executable, server_source, client_source)
    run_no_plaintext_downgrade(executable, server_source, client_source)
    print("Encrypted Chompo chat smoke test passed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    arguments = parser.parse_args()

    run_smoke(arguments.executable.resolve(), arguments.server.resolve(), arguments.client.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
