#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Exercise concurrent echo, EOF, peer errors and pending cancellation over TCP."""
import concurrent.futures
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

original = "--original" in sys.argv
controlled = "--controlled" in sys.argv
program = str(Path(sys.argv[-1]).resolve())
base = [program]
if program.endswith(".wasm"):
    base = [
        os.environ.get("WASMTIME", "wasmtime"),
        "run",
        "-S",
        "inherit-network=y",
        "-S",
        "tcp=y",
        "-W",
        "timeout=30s",
        "-S",
        "p3",
        "-W",
        "component-model-async=y",
        "-W",
        "component-model-threading=y",
    ]
    base += [program]


def run(case):
    with socket.socket() as reservation:
        # The immutable upstream example reuses port 55555 across profiles.
        # Match the acceptor's reuse policy so a previous test's TIME_WAIT
        # connection does not make this availability probe reject a free port.
        reservation.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        reservation.bind(("127.0.0.1", 55555 if original and not controlled else 0))
        port = reservation.getsockname()[1]
    command = base + ([] if original else [str(port), "1" if case == "reset" else "2"])
    environment = dict(os.environ)
    if controlled:
        environment["WEBCPP_COBALT_LISTENER_PORT"] = str(port)
        if program.endswith(".wasm"):
            command = base[:-1] + ["--env", f"WEBCPP_COBALT_LISTENER_PORT={port}", program]
    if case.startswith("cancel"):
        command += ["30" if case == "cancel-accept" else "1000"]
    with tempfile.TemporaryDirectory(prefix="cobalt-echo-") as directory:
        output = Path(directory) / "stdout.log"
        errors = Path(directory) / "stderr.log"
        with output.open("w") as stdout, errors.open("w") as stderr:
            process = subprocess.Popen(command, env=environment, stdout=stdout, stderr=stderr)
        peers = []
        try:
            if case != "cancel-accept":
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    assert process.poll() is None, (command, output.read_text(), errors.read_text())
                    try:
                        peers.append(socket.create_connection(("127.0.0.1", port), timeout=0.2))
                        break
                    except (ConnectionRefusedError, TimeoutError):
                        time.sleep(0.02)
                assert peers, f"echo server did not listen: {errors.read_text()}"
                peers[0].settimeout(5)
            if case == "traffic":
                peers.append(socket.create_connection(("127.0.0.1", port), timeout=2))

                def transfer(index):
                    peer = peers[index]
                    peer.settimeout(5)
                    payload = bytes(range(256)) * (32 + index)
                    for start in range(0, len(payload), 17):
                        peer.sendall(payload[start : start + 17])
                    peer.shutdown(socket.SHUT_WR)
                    received = bytearray()
                    while True:
                        part = peer.recv(31)
                        if not part:
                            break
                        received.extend(part)
                    assert received == payload, (index, len(received), len(payload))
                    return len(received)

                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                    counts = list(pool.map(transfer, range(2)))
                expected = "accepted=2 completed=2 bytes=16640 cancelled=0 failures=0"
                assert counts == [8192, 8448], counts
            elif case == "cancel-accept":
                expected = "accepted=0 completed=0 bytes=0 cancelled=1 failures=0"
            elif case == "cancel-session":
                peers[0].sendall(b"pending")
                reply = bytearray()
                while len(reply) < 7:
                    part = peers[0].recv(7 - len(reply))
                    assert part, "session ended before echo"
                    reply.extend(part)
                assert reply == b"pending", reply
                assert peers[0].recv(1) == b"", "cancelled session did not close"
                expected = "accepted=1 completed=1 bytes=7 cancelled=1 failures=0"
            else:
                # Observe an accepted session before resetting it. A successful
                # connect alone can still leave the socket in the accept queue.
                peers[0].sendall(b"!")
                assert peers[0].recv(1) == b"!", "session ended before reset handshake"
                peers[0].setsockopt(
                    socket.SOL_SOCKET,
                    socket.SO_LINGER,
                    struct.pack("hh" if os.name == "nt" else "ii", 1, 0),
                )
                peers[0].close()
                expected = "accepted=1 completed=1 bytes=1 cancelled=0 failures=1"
            if original:
                assert process.poll() is None, "the original infinite listener exited unexpectedly"
                # Traffic is the witness. The native runner catches SIGTERM,
                # so use a hard stop without claiming graceful cancellation.
                process.kill()
                process.wait(timeout=5)
            else:
                status = process.wait(timeout=15)
                assert status == (1 if case == "reset" else 0), (
                    status,
                    output.read_text(),
                    errors.read_text(),
                )
                assert expected in output.read_text(), (
                    expected,
                    output.read_text(),
                    errors.read_text(),
                )
            print(f'PASS {"original" if original else "finite"} echo: {case}')
        finally:
            for peer in peers:
                peer.close()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


run("traffic")
if original:
    print("Original listener terminated after traffic; no graceful shutdown claimed.")
else:
    for scenario in ("cancel-accept", "cancel-session", "reset"):
        run(scenario)
    result = subprocess.run(base + ["0", "2"], capture_output=True, text=True, timeout=15)
    assert result.returncode == (1 if program.endswith(".wasm") else 2), result
    assert "usage: echo_server" in result.stderr, result
    print("PASS finite echo: invalid arguments")
