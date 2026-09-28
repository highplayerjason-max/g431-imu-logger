#!/usr/bin/env python3
"""
Find which GPIO the board's user button is wired to.

Requires an openocd server on 127.0.0.1:4444 with the target halted-able.
It configures every *unused* pin as input + pull-up (leaving the pins used by
SPI1/SPI2/USART1/SWD untouched), then prints the pins currently reading low.

Run it twice:
    1) with the button released   -> baseline
    2) with the button pressed    -> the pin that changed is the button

Usage: python tools/button_scan.py <label>
"""

import socket
import sys
import time

HOST, PORT = "127.0.0.1", 4444

# gpio base, candidate pins, pins used by peripherals (left untouched)
PORTS = [
    ("GPIOA", 0x48000000, [0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 15], {9, 10, 13, 14}),
    ("GPIOB", 0x48000400, [0, 1, 2, 8, 9, 10, 11], {3, 4, 5, 6, 7, 12, 13, 14, 15}),
    ("GPIOC", 0x48000800, [13, 14, 15], set()),
    ("GPIOF", 0x48001400, [0, 1], set()),
]

MODER, PUPDR, IDR = 0x00, 0x0C, 0x10


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "snapshot"

    s = socket.create_connection((HOST, PORT), timeout=10)
    time.sleep(0.3)
    try:
        s.recv(65536)
    except socket.timeout:
        pass

    def cmd(c, wait=0.25):
        s.sendall((c + "\n").encode())
        time.sleep(wait)
        try:
            return s.recv(65536).decode("utf-8", errors="ignore")
        except socket.timeout:
            return ""

    def rd(addr):
        out = cmd("mdw 0x%08X 1" % addr)
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("0x") and ":" in line:
                return int(line.split(":")[1].strip(), 16)
        return 0

    cmd("halt")

    for name, base, pins, used in PORTS:
        moder = rd(base + MODER)
        pupdr = rd(base + PUPDR)
        for p in pins:
            if p in used:
                continue
            moder &= ~(3 << (2 * p))
            pupdr = (pupdr & ~(3 << (2 * p))) | (1 << (2 * p))
        cmd("mww 0x%08X 0x%08X" % (base + MODER, moder & 0xFFFFFFFF))
        cmd("mww 0x%08X 0x%08X" % (base + PUPDR, pupdr & 0xFFFFFFFF))

    print("[%s] pins reading LOW:" % label)
    for name, base, pins, used in PORTS:
        idr = rd(base + IDR)
        low = ["P%s%d" % (name[-1], p) for p in pins if not (idr >> p) & 1]
        print("  %-6s IDR=0x%08X  low: %s" % (name, idr, " ".join(low) if low else "-"))

    cmd("resume")
    s.close()


if __name__ == "__main__":
    main()
