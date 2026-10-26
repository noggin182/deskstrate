#!/usr/bin/env python3
"""Rings the boards' update doorbells: one UDP datagram per port, starting with the
magic bytes "DSUP". A board that hears it checks GitHub Releases for a new image (at
most once a minute). The datagram carries nothing else; see
components/ota_pull/include/ota_pull.h.

    doorbell.py <host> <port>...     e.g. doorbell.py deskstrate.nogsnet.com 18210 18211

Ports: 18210 = core, 18211 = deck. On the LAN, use each board's own address instead.
"""
import socket
import sys

MAGIC = b"DSUP"


def main() -> int:
    host, ports = sys.argv[1], [int(p) for p in sys.argv[2:]]
    try:
        addr = socket.getaddrinfo(host, None, socket.AF_INET, socket.SOCK_DGRAM)[0][4][0]
    except socket.gaierror as e:
        print(f"{host}: {e}", file=sys.stderr)
        return 1
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for port in ports:
            sock.sendto(MAGIC, (addr, port))
            print(f"rang {host} ({addr}) port {port}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
