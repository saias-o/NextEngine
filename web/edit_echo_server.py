#!/usr/bin/env python3
"""Tiny WebSocket echo/broadcast server for PLAN_LIVE_EDIT_WEB S3.

This is a local spike helper, not the production Collaboration Gateway. It only
proves the browser path: WebSocket text frame -> SaidaOp JSON -> wasm applyOp.

Usage:
  python web/edit_echo_server.py [port]
  python web/serve.py build-web 8080
  open http://localhost:8080/index.html?edit&ws=1
"""

import base64
import hashlib
import socket
import struct
import sys
import threading


GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8787

clients = set()
clients_lock = threading.Lock()


def recv_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("socket closed")
        data.extend(chunk)
    return bytes(data)


def read_http_headers(sock):
    data = bytearray()
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("socket closed during handshake")
        data.extend(chunk)
        if len(data) > 32768:
            raise ValueError("handshake too large")
    return data.decode("iso-8859-1")


def websocket_accept(key):
    digest = hashlib.sha1((key + GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def handshake(sock):
    headers = read_http_headers(sock)
    key = None
    for line in headers.split("\r\n"):
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
            break
    if not key:
        raise ValueError("missing Sec-WebSocket-Key")

    response = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {websocket_accept(key)}\r\n"
        "\r\n"
    )
    sock.sendall(response.encode("ascii"))


def read_frame(sock):
    b1, b2 = recv_exact(sock, 2)
    opcode = b1 & 0x0F
    masked = (b2 & 0x80) != 0
    length = b2 & 0x7F

    if length == 126:
        length = struct.unpack("!H", recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(sock, 8))[0]

    mask = recv_exact(sock, 4) if masked else b"\x00\x00\x00\x00"
    payload = bytearray(recv_exact(sock, length))
    if masked:
        for i in range(length):
            payload[i] ^= mask[i % 4]
    return opcode, bytes(payload)


def send_text(sock, text):
    payload = text.encode("utf-8")
    header = bytearray([0x81])
    if len(payload) < 126:
        header.append(len(payload))
    elif len(payload) <= 0xFFFF:
        header.append(126)
        header.extend(struct.pack("!H", len(payload)))
    else:
        header.append(127)
        header.extend(struct.pack("!Q", len(payload)))
    sock.sendall(bytes(header) + payload)


def broadcast(text):
    dead = []
    with clients_lock:
        targets = list(clients)
    for client in targets:
        try:
            send_text(client, text)
        except OSError:
            dead.append(client)
    if dead:
        with clients_lock:
            for client in dead:
                clients.discard(client)


def handle_client(sock, addr):
    try:
        handshake(sock)
        with clients_lock:
            clients.add(sock)
        print(f"connected {addr[0]}:{addr[1]} ({len(clients)} client(s))", flush=True)

        while True:
            opcode, payload = read_frame(sock)
            if opcode == 0x8:  # close
                break
            if opcode == 0x9:  # ping
                continue
            if opcode != 0x1:  # text only
                continue
            text = payload.decode("utf-8")
            print(f"echo {len(text)} bytes: {text}", flush=True)
            broadcast(text)
    except Exception as exc:
        print(f"client {addr[0]}:{addr[1]} closed: {exc}", flush=True)
    finally:
        with clients_lock:
            clients.discard(sock)
        try:
            sock.close()
        except OSError:
            pass


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen()
        print(f"Saida edit echo WS listening on ws://{HOST}:{PORT}", flush=True)
        while True:
            sock, addr = server.accept()
            threading.Thread(target=handle_client, args=(sock, addr), daemon=True).start()


if __name__ == "__main__":
    main()
