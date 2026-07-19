#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import threading
import time
from pathlib import Path
from typing import TextIO

PASSWORD = "chompo-test-password"


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

    def wait_contains(self, text: str, timeout: float = 8.0) -> None:
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

    def wait_regex(self, pattern: str, timeout: float = 8.0) -> re.Match[str]:
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

    def wait_exit(self, timeout: float = 8.0) -> int:
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


def start_client(executable: Path, client_source: Path, port: int, name: str) -> CapturedProcess:
    client = CapturedProcess(
        [str(executable), str(client_source), "127.0.0.1", str(port), PASSWORD],
        name,
    )
    client.wait_contains("Защищённый канал установлен.")
    client.send(name)
    client.wait_contains("OK " + name)
    client.wait_contains("* " + name + " joined")
    return client


def run_smoke(executable: Path, server_source: Path, client_source: Path) -> None:
    server = CapturedProcess(
        [str(executable), str(server_source), "127.0.0.1", "0", "10", PASSWORD],
        "server",
    )
    clients: list[CapturedProcess] = []

    try:
        match = server.wait_regex(r"LISTENING (\d+) SECURE AES-256-GCM")
        port = int(match.group(1))
        require(0 < port <= 65535, f"invalid listening port {port}")

        alice = start_client(executable, client_source, port, "Alice")
        clients.append(alice)

        bob = start_client(executable, client_source, port, "Bob")
        clients.append(bob)
        alice.wait_contains("* Bob joined")

        message = "Привет, Bob 👋"
        alice.send(message)
        alice.wait_contains("Alice: " + message)
        bob.wait_contains("Alice: " + message)

        bob.send("/users")
        bob.wait_contains("USERS 2")
        bob.wait_contains("USER Alice online")
        bob.wait_contains("USER Bob online")

        bob.send("/status away")
        bob.wait_contains("* Bob is now away")
        alice.wait_contains("* Bob is now away")

        bob.send("/msg Alice секретное сообщение 🔐")
        bob.wait_contains("[DM to Alice] секретное сообщение 🔐")
        alice.wait_contains("[DM from Bob] секретное сообщение 🔐")

        bob.send("/nick Борис")
        bob.wait_contains("OK renamed to Борис")
        alice.wait_contains("* Bob is now known as Борис")

        server.send("/say Проверка серверного сообщения")
        alice.wait_contains("[SERVER] Проверка серверного сообщения")
        bob.wait_contains("[SERVER] Проверка серверного сообщения")

        alice.send("/ping")
        alice.wait_contains("PONG")

        server.send("/kick Борис")
        bob.wait_contains("KICKED by server")
        alice.wait_contains("* Борис left")
        require(bob.wait_exit() == 0, f"Bob client exited with {bob.process.returncode}: {bob.stderr}")

        alice.send("/quit")
        alice.wait_contains("BYE")
        require(alice.wait_exit() == 0, f"Alice client exited with {alice.process.returncode}: {alice.stderr}")

        server.send("/stop")
        require(server.wait_exit() == 0, f"server exited with {server.process.returncode}: {server.stderr}")

        require("Привет, Bob 👋" in server.stdout, "server did not log the UTF-8 message")
        require("SECURITY rejected client packet" not in server.stdout, "valid encrypted traffic was rejected")
    finally:
        for client in clients:
            client.terminate()
        server.terminate()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--client", required=True, type=Path)
    arguments = parser.parse_args()

    run_smoke(arguments.executable.resolve(), arguments.server.resolve(), arguments.client.resolve())
    print("Encrypted Chompo chat smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
