#!/usr/bin/env python3
"""End-to-end check of the example programs over real sockets.

Plays a MUD client the way TinTin++ does: accepts MCCP2 and inflates the
server's stream, accepts MCCP3 and sends its start marker and compressed
input in the same packet, answers TTYPE and NAWS. Then checks what the
servers say back. Also drives client_simple against server_simple.

usage: e2e_examples.py <build-dir>
"""

import os
import socket
import subprocess
import sys
import time
import zlib

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
TTYPE, NAWS, MCCP2, MCCP3 = 24, 31, 86, 87

failures = 0


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def start(binary, *args):
    proc = subprocess.Popen([binary, *map(str, args)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    port = int(args[0])
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            return proc
        except OSError:
            time.sleep(0.05)
    proc.kill()
    raise RuntimeError(f"{binary} did not start")


class MudClient:
    """Minimal TELNET client with MCCP2 inflate and MCCP3 deflate."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.inflate = None          # MCCP2: server -> us
        self.deflate = None          # MCCP3: us -> server
        self.mccp3_offered = False
        self.text = bytearray()
        self.raw = bytearray()       # decoded bytes awaiting telnet parsing
        self.stream_ended = False

    def send(self, data):
        if self.deflate:
            data = self.deflate.compress(data) + self.deflate.flush(zlib.Z_SYNC_FLUSH)
        self.sock.sendall(data)

    def line(self, text):
        self.send(text.encode() + b"\r\n")

    def start_mccp3(self, first_line):
        """Marker plus compressed input in one packet, like TinTin++."""
        self.deflate = zlib.compressobj()
        payload = self.deflate.compress(first_line.encode() + b"\r\n")
        payload += self.deflate.flush(zlib.Z_SYNC_FLUSH)
        self.sock.sendall(bytes([IAC, SB, MCCP3, IAC, SE]) + payload)

    def _feed(self, chunk):
        while chunk:
            if self.inflate:
                out = self.inflate.decompress(chunk)
                self.raw += out
                if self.inflate.eof:
                    self.stream_ended = True
                    chunk = self.inflate.unused_data
                    self.inflate = None
                    continue
                return
            # Plain bytes: watch for the MCCP2 start marker.
            marker = bytes([IAC, SB, MCCP2, IAC, SE])
            at = chunk.find(marker)
            if at < 0:
                self.raw += chunk
                return
            self.raw += chunk[:at]
            self.inflate = zlib.decompressobj()
            chunk = chunk[at + len(marker):]

    def _parse(self):
        b, i, out = self.raw, 0, []
        while i < len(b):
            if b[i] != IAC:
                self.text.append(b[i]); i += 1; continue
            if i + 1 >= len(b): break
            cmd = b[i + 1]
            if cmd == IAC:
                self.text.append(IAC); i += 2
            elif cmd in (WILL, WONT, DO, DONT):
                if i + 2 >= len(b): break
                self._negotiate(cmd, b[i + 2]); i += 3
            elif cmd == SB:
                end = b.find(bytes([IAC, SE]), i)
                if end < 0: break
                self._subneg(b[i + 2], bytes(b[i + 3:end])); i = end + 2
            else:
                i += 2
        del self.raw[:i]

    def _negotiate(self, cmd, opt):
        if cmd == WILL:
            if opt in (MCCP2, MCCP3):
                self.sock.sendall(bytes([IAC, DO, opt]))
                self.mccp3_offered |= opt == MCCP3
            else:
                self.send(bytes([IAC, DONT, opt]))
        elif cmd == DO:
            if opt in (TTYPE, NAWS):
                self.send(bytes([IAC, WILL, opt]))
                if opt == NAWS:
                    self.send(bytes([IAC, SB, NAWS, 0, 100, 0, 40, IAC, SE]))
            else:
                self.send(bytes([IAC, WONT, opt]))

    def _subneg(self, opt, payload):
        if opt == TTYPE and payload[:1] == b"\x01":
            self.send(bytes([IAC, SB, TTYPE, 0]) + b"xterm-e2e" + bytes([IAC, SE]))

    def read_until(self, needle, timeout=3.0):
        deadline = time.time() + timeout
        self.sock.settimeout(0.1)
        while needle.encode() not in self.text and time.time() < deadline:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                break
            self._feed(chunk)
            self._parse()
        return needle.encode() in self.text

    def settle(self, seconds=0.3):
        self.read_until("\x00never\x00", seconds)

    def close(self):
        self.sock.close()


def test_server_simple(build):
    port = free_port()
    server = start(os.path.join(build, "server_simple"), port)
    try:
        c = MudClient(port)
        check(c.read_until("Welcome to the telnetty chat server"), "server_simple: welcome text")
        c.settle()
        check(c.inflate is not None, "server_simple: MCCP2 stream started")
        check(c.mccp3_offered, "server_simple: MCCP3 offered")
        c.start_mccp3("/who")
        check(c.read_until("mccp3=on"), "server_simple: /who sent with the MCCP3 marker is answered")
        check(b"term=xterm-e2e" in c.text, "server_simple: TTYPE reached the server")
        check(b"size=100x40" in c.text, "server_simple: NAWS reached the server")
        check(b"mccp2=on" in c.text, "server_simple: server reports MCCP2 on")

        other = MudClient(port)
        other.read_until("Welcome")
        c.line("hello there")
        check(other.read_until("says: hello there"), "server_simple: chat line from compressed client")
        other.close()

        c.line("/quit")
        check(c.read_until("Goodbye."), "server_simple: /quit")
        c.read_until("\x00never\x00", 1.0)
        check(c.stream_ended, "server_simple: MCCP2 stream ended with Z_STREAM_END")
        c.close()

        # The bundled client against the bundled server.
        # Type /who after negotiation has had a moment, as a person would.
        cli = subprocess.Popen([os.path.join(build, "client_simple"), "127.0.0.1", str(port)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL,
                               env={**os.environ, "TERM": "client-e2e"})
        time.sleep(0.5)
        cli.stdin.write(b"/who\n")
        cli.stdin.flush()
        time.sleep(0.5)
        cli.stdin.close()
        out = cli.communicate(timeout=10)[0].decode(errors="replace")
        check("Welcome to the telnetty chat server" in out, "client_simple: shows server text")
        check("term=client-e2e" in out, "client_simple: answered TTYPE")
        check("mccp2=on mccp3=on" in out, "client_simple: negotiated MCCP2 and MCCP3")
    finally:
        server.terminate()
        server.wait(timeout=5)


def test_server_libuv(build):
    binary = os.path.join(build, "server_libuv")
    if not os.path.exists(binary):
        print("skip server_libuv (not built)")
        return
    port = free_port()
    server = start(binary, port)
    try:
        c = MudClient(port)
        check(c.read_until("Welcome to the telnetty libuv echo server"), "server_libuv: welcome text")
        c.settle()
        c.start_mccp3("ping")
        check(c.read_until("ping"), "server_libuv: echo through MCCP3 in, MCCP2 out")
        c.line("été")
        check(c.read_until("été"), "server_libuv: UTF-8 round trip")
        c.close()
    finally:
        server.terminate()
        server.wait(timeout=5)


if __name__ == "__main__":
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    test_server_simple(build)
    test_server_libuv(build)
    print(f"\n{failures} failure(s)")
    sys.exit(1 if failures else 0)
