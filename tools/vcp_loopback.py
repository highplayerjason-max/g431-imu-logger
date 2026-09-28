#!/usr/bin/env python3
"""
ST-Link Virtual COM Port loopback test.

Short the ST-Link TXD and RXD pins together (with the target board wire
removed), then run:

    python tools/vcp_loopback.py COM17

If the bytes come back, the ST-Link VCP and the PC driver are fine and the
problem is on the target/board side.
"""

import sys
import time

import serial


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM17"
    pattern = bytes(range(0x20, 0x30)) * 4

    s = serial.Serial(port, 115200, timeout=0.5)
    s.reset_input_buffer()
    s.write(pattern)
    s.flush()
    time.sleep(0.2)
    data = s.read(len(pattern))
    s.close()

    print(f"sent {len(pattern)} bytes, received {len(data)} bytes")
    if data == pattern:
        print("VCP LOOPBACK PASS")
        return 0
    print(f"expected: {pattern!r}")
    print(f"actual  : {data!r}")
    print("VCP LOOPBACK FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
