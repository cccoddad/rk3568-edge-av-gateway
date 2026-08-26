#!/usr/bin/env python3
"""Receive one TFTP upload or send one file without third-party dependencies.

This helper is intended for transferring RK3568 validation artifacts to a
Windows development host when the minimal Buildroot image has only BusyBox
``tftp`` available.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
from pathlib import Path


TFTP_RRQ = 1
TFTP_WRQ = 2
TFTP_DATA = 3
TFTP_ACK = 4
TFTP_ERROR = 5
TFTP_BLOCK_SIZE = 512


def parse_wrq(packet: bytes) -> tuple[str, str]:
    if len(packet) < 4 or struct.unpack("!H", packet[:2])[0] != TFTP_WRQ:
        raise ValueError("expected a TFTP write request")
    fields = packet[2:].split(b"\0")
    if len(fields) < 3 or not fields[0] or not fields[1]:
        raise ValueError("malformed TFTP write request")
    filename = fields[0].decode("utf-8", errors="replace")
    mode = fields[1].decode("ascii", errors="replace").lower()
    if mode != "octet":
        raise ValueError(f"unsupported TFTP mode: {mode}")
    return filename, mode


def parse_rrq(packet: bytes) -> tuple[str, str]:
    if len(packet) < 4 or struct.unpack("!H", packet[:2])[0] != TFTP_RRQ:
        raise ValueError("expected a TFTP read request")
    fields = packet[2:].split(b"\0")
    if len(fields) < 3 or not fields[0] or not fields[1]:
        raise ValueError("malformed TFTP read request")
    filename = fields[0].decode("utf-8", errors="replace")
    mode = fields[1].decode("ascii", errors="replace").lower()
    if mode != "octet":
        raise ValueError(f"unsupported TFTP mode: {mode}")
    return filename, mode


def error_packet(code: int, message: str) -> bytes:
    return struct.pack("!HH", TFTP_ERROR, code) + message.encode("utf-8") + b"\0"


def receive_one(bind_address: str, port: int, output_dir: Path, timeout: float) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind((bind_address, port))
    print(f"Waiting for one TFTP upload on {bind_address}:{port} ...", flush=True)

    request, client = listener.recvfrom(65535)
    try:
        requested_name, _ = parse_wrq(request)
    except ValueError as exc:
        listener.sendto(error_packet(4, str(exc)), client)
        raise
    finally:
        listener.close()

    safe_name = Path(requested_name.replace("\\", "/")).name
    if not safe_name or safe_name in {".", ".."}:
        raise ValueError("invalid destination filename")
    destination = output_dir / safe_name
    temporary = output_dir / f".{safe_name}.part"
    if destination.exists() or temporary.exists():
        raise FileExistsError(f"refusing to overwrite: {destination}")

    transfer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    transfer.bind((bind_address, 0))
    transfer.settimeout(timeout)
    ack = struct.pack("!HH", TFTP_ACK, 0)
    transfer.sendto(ack, client)
    expected_block = 1
    total_bytes = 0
    retries = 0

    try:
        with temporary.open("xb") as output:
            while True:
                try:
                    packet, sender = transfer.recvfrom(4 + TFTP_BLOCK_SIZE)
                except socket.timeout:
                    retries += 1
                    if retries > 5:
                        raise TimeoutError("TFTP upload timed out")
                    transfer.sendto(ack, client)
                    continue

                if sender != client or len(packet) < 4:
                    continue
                opcode, block = struct.unpack("!HH", packet[:4])
                if opcode == TFTP_ERROR:
                    message = packet[4:].rstrip(b"\0").decode("utf-8", errors="replace")
                    raise RuntimeError(f"client reported TFTP error: {message}")
                if opcode != TFTP_DATA:
                    continue
                if block == ((expected_block - 1) & 0xFFFF):
                    transfer.sendto(ack, client)
                    continue
                if block != (expected_block & 0xFFFF):
                    continue

                payload = packet[4:]
                output.write(payload)
                total_bytes += len(payload)
                ack = struct.pack("!HH", TFTP_ACK, block)
                transfer.sendto(ack, client)
                retries = 0
                expected_block += 1
                if len(payload) < TFTP_BLOCK_SIZE:
                    break

        temporary.replace(destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    finally:
        transfer.close()

    print(f"Received {total_bytes} bytes: {destination}", flush=True)
    return destination


def send_one(bind_address: str, port: int, source: Path, timeout: float) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"source file does not exist: {source}")
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind((bind_address, port))
    print(f"Waiting for one TFTP download request on {bind_address}:{port} ...", flush=True)

    request, client = listener.recvfrom(65535)
    try:
        requested_name, _ = parse_rrq(request)
    except ValueError as exc:
        listener.sendto(error_packet(4, str(exc)), client)
        raise
    finally:
        listener.close()

    if requested_name != source.name:
        transfer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            transfer.sendto(error_packet(1, "requested filename does not match server file"), client)
        finally:
            transfer.close()
        raise ValueError(f"client requested {requested_name}, expected {source.name}")

    transfer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    transfer.bind((bind_address, 0))
    transfer.settimeout(timeout)
    total_bytes = 0
    block = 1
    try:
        with source.open("rb") as input_file:
            while True:
                payload = input_file.read(TFTP_BLOCK_SIZE)
                packet = struct.pack("!HH", TFTP_DATA, block) + payload
                for attempt in range(6):
                    transfer.sendto(packet, client)
                    try:
                        response, sender = transfer.recvfrom(1024)
                    except socket.timeout:
                        if attempt == 5:
                            raise TimeoutError("TFTP download timed out")
                        continue
                    if sender != client or len(response) < 4:
                        continue
                    opcode, acknowledged_block = struct.unpack("!HH", response[:4])
                    if opcode == TFTP_ERROR:
                        message = response[4:].rstrip(b"\0").decode("utf-8", errors="replace")
                        raise RuntimeError(f"client reported TFTP error: {message}")
                    if opcode == TFTP_ACK and acknowledged_block == block:
                        break
                else:
                    raise TimeoutError("TFTP download timed out")
                total_bytes += len(payload)
                if len(payload) < TFTP_BLOCK_SIZE:
                    break
                block = (block + 1) & 0xFFFF
                if block == 0:
                    raise ValueError("source file exceeds classic TFTP block limit")
    finally:
        transfer.close()
    print(f"Sent {total_bytes} bytes: {source}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Transfer one TFTP file and then exit")
    parser.add_argument("--bind", default="0.0.0.0", help="local IPv4 address to bind")
    parser.add_argument("--port", type=int, default=69, help="UDP listen port")
    parser.add_argument(
        "--output-dir", type=Path, default=Path.cwd(), help="directory for the received file"
    )
    parser.add_argument("--timeout", type=float, default=5.0, help="per-packet timeout in seconds")
    parser.add_argument("--send", type=Path, help="serve this file for one TFTP download request")
    args = parser.parse_args()
    try:
        if args.send is not None:
            send_one(args.bind, args.port, args.send.resolve(), args.timeout)
        else:
            receive_one(args.bind, args.port, args.output_dir.resolve(), args.timeout)
    except KeyboardInterrupt:
        print("Transfer cancelled.", file=sys.stderr)
        return 130
    except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
        print(f"TFTP receive failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
