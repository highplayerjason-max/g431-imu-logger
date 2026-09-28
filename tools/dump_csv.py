#!/usr/bin/env python3
"""
Dump the offline IMU log from the G431 logger over USART1.

Usage:
    python tools/dump_csv.py COM17 out.csv            # just dump existing log
    python tools/dump_csv.py COM17 out.csv --record   # start a new record first
"""

import argparse
import re
import sys
import time

import serial

DEFAULT_HEADER = ("time_us,ax_raw,ay_raw,az_raw,gx_raw,gy_raw,gz_raw,"
                  "ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="serial port, e.g. COM17")
    ap.add_argument("output", help="output CSV path")
    ap.add_argument("--record", action="store_true",
                    help="send R and wait for RECORD COMPLETE before dumping")
    ap.add_argument("--wait", action="store_true",
                    help="do not send anything, just wait for a dump triggered "
                         "externally (e.g. by the debugger)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    print(f"opened {args.port} @ {args.baud}")

    if args.record:
        print("sending R (start recording) ...")
        ser.write(b"R")
        deadline = time.time() + 40
        while time.time() < deadline:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                print("  " + line)
            if line.startswith("RECORD COMPLETE"):
                break
        else:
            print("timeout waiting for RECORD COMPLETE")
            ser.close()
            return 1

    if not args.wait:
        print("sending D (dump CSV) ...")
        ser.write(b"D")
    else:
        print("waiting for externally triggered dump ...")

    header = None
    rows = []
    deadline = time.time() + 180

    # Wait for a full dump to start (header line) so the CSV is complete.
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if line.startswith("time_us,"):
            header = line
            break
        if line.startswith("DUMP ERROR") or line.startswith("LOG CRC FAIL"):
            print("device reported: " + line)

    if header is None:
        print("no CSV header received")
        ser.close()
        return 1

    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="ignore").strip()
        if not line:
            continue
        if line.startswith("LOG_END"):
            break
        if line.startswith("DUMP ERROR") or line.startswith("LOG CRC FAIL"):
            print("device reported: " + line)
            continue
        if re.match(r"^\d+,", line):
            rows.append(line)
    else:
        print("timeout waiting for LOG_END")
        ser.close()
        return 1

    ser.close()

    if not rows:
        print("no CSV rows received")
        return 1

    with open(args.output, "w", encoding="utf-8", newline="") as fh:
        fh.write(header + "\n")
        fh.write("\n".join(rows) + "\n")

    print(f"saved {len(rows)} samples -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
