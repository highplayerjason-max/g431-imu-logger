#!/usr/bin/env python3
"""
Register-level SPI1 test through openocd TCL.

Restores PB3/PB4/PB5 to SPI1 AF5, then performs a raw DR-based WHO_AM_I read
so we can tell whether the SPI peripheral path works (vs GPIO bit-bang).
"""

import socket
import time

CMDS = [
    "halt",
    "set m [mrw 0x48000400]",
    "set m [expr {($m & ~(3<<6) & ~(3<<8) & ~(3<<10) & ~(3<<12)) | (2<<6) | (2<<8) | (2<<10) | (1<<12)}]",
    "mww 0x48000400 $m",
    "mww 0x48000420 0x00555000",
    "set p [mrw 0x4800040C]",
    "set p [expr {$p & ~(3<<6) & ~(3<<8) & ~(3<<10) & ~(3<<12)}]",
    "mww 0x4800040C $p",
    "mdw 0x48000400 1",
    "mdw 0x48000420 1",
    "mdw 0x40013000 1",
    "mww 0x48000418 0x00400000",
    "mww 0x4001300C 0xF5",
    "mdw 0x40013008 1",
    "mdw 0x4001300C 1",
    "mww 0x4001300C 0x00",
    "mdw 0x40013008 1",
    "mdw 0x4001300C 1",
    "mww 0x48000418 0x40",
    "resume",
]


def main():
    s = socket.create_connection(("127.0.0.1", 4444), timeout=10)
    time.sleep(0.3)
    try:
        s.recv(65536)
    except socket.timeout:
        pass
    text_all = ""
    for c in CMDS:
        s.sendall((c + "\n").encode())
        time.sleep(0.4)
        try:
            text_all += s.recv(65536).decode("utf-8", errors="ignore")
        except socket.timeout:
            pass
    s.close()
    for line in text_all.splitlines():
        line = line.strip()
        if line.startswith("0x"):
            print(line)


if __name__ == "__main__":
    main()
