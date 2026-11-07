#!/usr/bin/env python3
# Copyright (c) 2026 WebCpp.org
#
# Distributed under the Boost Software License, Version 1.0. (See
# accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)

"""Drive actual Cobalt HTTP/ticker counterparts against bounded local TLS peers."""
import base64
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[3] / "beast/test/applications"))
import importlib

peer = importlib.import_module("driver")

kind = sys.argv[1]
program = Path(sys.argv[-1]).resolve()


def websocket_accept(connection):
    request, headers = peer.receive_headers(connection)
    assert request == "GET /mercury-gateway/v1/ws HTTP/1.1", request
    assert headers["origin"] == "https://exchange.blockchain.com", headers
    accept = base64.b64encode(
        hashlib.sha1(
            (headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()
        ).digest()
    )
    connection.sendall(
        b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        b"Connection: Upgrade\r\nSec-WebSocket-Accept: "
        + accept
        + b"\r\n\r\n"
    )


def downstream(port, process):
    end = time.monotonic() + 20
    while time.monotonic() < end:
        if process.poll() is not None:
            raise RuntimeError(f"ticker exited before listening: {process.returncode}")
        try:
            connection = socket.create_connection(("127.0.0.1", port), 0.2)
            connection.settimeout(10)
            break
        except (ConnectionRefusedError, TimeoutError):
            time.sleep(0.02)
    else:
        raise TimeoutError("ticker did not listen")
    key = base64.b64encode(b"0123456789abcdef").decode()
    connection.sendall(
        (
            "GET /btc/usd HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
            f"Sec-WebSocket-Key: {key}\r\n\r\n"
        ).encode()
    )
    status, headers = peer.receive_headers(connection)
    assert status == "HTTP/1.1 101 Switching Protocols", (status, headers)
    return connection


with tempfile.TemporaryDirectory(prefix="cobalt-protocols-") as temporary:
    directory = Path(temporary)
    peer.certificates(directory)
    context = peer.tls_context(directory, server=True)
    names = []
    context.set_servername_callback(lambda _, name, __: names.append(name))
    with socket.socket() as listener, socket.socket() as reservation:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(30)
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
        reservation.close()
        env = dict(
            os.environ,
            WEBCPP_COBALT_REMOTE_PORT=str(listener.getsockname()[1]),
            WEBCPP_COBALT_LISTENER_PORT=str(port),
            WEBCPP_COBALT_CA=str(directory / "ca.crt"),
        )
        command = [str(program)]
        if program.suffix == ".wasm":
            command = [
                os.environ.get("WASMTIME", "wasmtime"),
                "run",
                "-S",
                "p3",
                "-S",
                "inherit-network=y",
                "-S",
                "tcp=y",
                "-S",
                "allow-ip-name-lookup=y",
                "-W",
                "component-model-async=y",
                "-W",
                "component-model-threading=y",
                "-W",
                "timeout=45s",
                "--dir",
                str(directory),
            ]
            for key, value in env.items():
                if key.startswith("WEBCPP_COBALT_"):
                    command += ["--env", f"{key}={value}"]
            command += [str(program)]
        with (directory / "stdout").open("w") as output, (directory / "stderr").open("w") as errors:
            process = subprocess.Popen(command, env=env, stdout=output, stderr=errors)
        client = None
        try:
            raw, _ = listener.accept()
            with context.wrap_socket(raw, server_side=True) as connection:
                connection.settimeout(15)
                if kind == "http":
                    request, headers = peer.receive_headers(connection)
                    assert request == "GET /index.html HTTP/1.1", request
                    assert headers["host"] == "boost.org", headers
                    payload = b"controlled cobalt HTTPS response"
                    connection.sendall(
                        b"HTTP/1.1 200 OK\r\nContent-Length: "
                        + str(len(payload)).encode()
                        + b"\r\nConnection: close\r\n\r\n"
                        + payload
                    )
                    assert process.wait(timeout=15) == 0
                    assert payload.decode() in (directory / "stdout").read_text()
                else:
                    websocket_accept(connection)
                    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                        pending = pool.submit(downstream, port, process)
                        opcode, payload = peer.receive_frame(connection)
                        assert opcode == 1
                        assert json.loads(payload) == {
                            "action": "subscribe",
                            "channel": "ticker",
                            "symbol": "BTC-USD",
                        }, payload
                        client = pending.result(timeout=10)
                    for value in (100, 101, 102):
                        tick = {"event": "updated", "symbol": "BTC-USD", "last_trade_price": value}
                        peer.send_frame(connection, json.dumps(tick).encode())
                        opcode, payload = peer.receive_frame(client)
                        assert opcode == 1 and json.loads(payload) == tick, (opcode, payload)
                    assert process.poll() is None, "infinite ticker stopped unexpectedly"
                    process.kill()
                    process.wait(timeout=5)
                assert names == ["localhost"], names
                print(f"PASS controlled {kind}: verified TLS, SNI and exact application payloads")
                print(
                    json.dumps(
                        {
                            "application": "cobalt-" + kind,
                            "source_form": "controlled-endpoint-counterpart",
                            "command": command,
                            "server_terminated_after_traffic": kind == "ticker",
                        },
                        sort_keys=True,
                    )
                )
        except BaseException:
            print((directory / "stdout").read_text(), file=sys.stderr)
            print((directory / "stderr").read_text(), file=sys.stderr)
            raise
        finally:
            if client is not None:
                client.close()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
