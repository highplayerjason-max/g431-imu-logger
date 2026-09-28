#!/usr/bin/env python3
"""
Bit-bang ICM-42688 WHO_AM_I through openocd's TCL interface.

Requires an openocd server already running with a halted-or-haltable target.
This bypasses the STM32 SPI peripheral completely, so it isolates
"HAL/SPI configuration bug" from "IMU hardware problem".

Usage: python tools/bitbang_who.py
"""

import socket
import sys
import time
import re

OPENOCD_HOST = "127.0.0.1"
OPENOCD_PORT = 4444

TCL = r"""
halt
set moder [mrw 0x48000400]
set m [expr {($moder & ~(3<<6) & ~(3<<8) & ~(3<<10) & ~(3<<12)) | (1<<6) | (1<<10) | (1<<12)}]
mww 0x48000400 $m
set pupdr [mrw 0x4800040C]
set p [expr {$pupdr & ~(3<<6) & ~(3<<8) & ~(3<<10) & ~(3<<12)}]
mww 0x4800040C $p

proc sck {v} { if {$v} { mww 0x48000418 0x8 } else { mww 0x48000418 0x00080000 } }
proc mosi {v} { if {$v} { mww 0x48000418 0x20 } else { mww 0x48000418 0x00200000 } }
proc csl {v} { if {$v} { mww 0x48000418 0x40 } else { mww 0x48000418 0x00400000 } }
proc miso {} { set idr [mrw 0x48000410]; return [expr {($idr >> 4) & 1}] }

proc readwho {} { csl 1; sck 0; mosi 0; csl 0; set b 0xF5; for {set i 0} {$i < 8} {incr i} { mosi [expr {($b >> (7 - $i)) & 1}]; sck 1; sck 0 }; set val 0; for {set i 0} {$i < 8} {incr i} { sck 1; set val [expr {($val << 1) | [miso]}]; sck 0 }; csl 1; return $val }

for {set k 0} {$k < 3} {incr k} { set v [readwho]; echo [format {BITBANG_WHO_%d=0x%02X} $k $v] }
resume
"""


def main():
    try:
        sock = socket.create_connection((OPENOCD_HOST, OPENOCD_PORT), timeout=10)
    except OSError as exc:
        print(f"cannot connect to openocd telnet: {exc}")
        return 1

    time.sleep(0.3)
    try:
        sock.recv(65536)
    except socket.timeout:
        pass

    sock.sendall((TCL + "\n").encode())

    deadline = time.time() + 30
    output = b""
    while time.time() < deadline:
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        output += chunk
        if b"BITBANG_WHO[2]" in output:
            break

    sock.close()
    text = output.decode("utf-8", errors="ignore")
    found = False
    for line in text.splitlines():
        if re.search(r"BITBANG_WHO_\d=0x[0-9A-Fa-f]{2}", line):
            print(line.strip())
            found = True
    if not found:
        print("no result; raw output:")
        print(text)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
