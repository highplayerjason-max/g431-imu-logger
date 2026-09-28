#!/usr/bin/env python3
"""Validate a read-only SWD log capture and write CSV for imu_gui.py."""

import csv
from pathlib import Path
import shutil
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "build" / "SwdExport"
OUTPUT = ROOT / "exports"
HEADER = struct.Struct("<9I")
SAMPLE = struct.Struct("<I6h")


def main():
    header_bytes = (CAPTURE / "log_header.bin").read_bytes()
    if len(header_bytes) != HEADER.size:
        raise ValueError(f"header length {len(header_bytes)} != {HEADER.size}")

    (magic, version, count, rate, size, duration, address, data_crc,
     header_crc) = HEADER.unpack(header_bytes)
    expected_header_crc = zlib.crc32(header_bytes[:-4] + b"\0\0\0\0")
    if (magic, version, rate, address) != (0x494D554C, 1, 500, 0x1000):
        raise ValueError("unexpected log header fields")
    if count == 0 or size != count * SAMPLE.size:
        raise ValueError("sample count and record size disagree")
    if header_crc != expected_header_crc:
        raise ValueError("header CRC mismatch")

    raw = (CAPTURE / "log_data.bin").read_bytes()
    if len(raw) != size:
        raise ValueError(f"data length {len(raw)} != {size}")
    actual_data_crc = zlib.crc32(raw)
    if actual_data_crc != data_crc:
        raise ValueError(
            f"data CRC mismatch: {actual_data_crc:08X} != {data_crc:08X}"
        )

    OUTPUT.mkdir(exist_ok=True)
    csv_path = OUTPUT / "imu_log_swd.csv"
    temp_path = OUTPUT / "imu_log_swd.csv.tmp"
    first = None
    last = None
    min_delta = None
    max_delta = None
    prev_time = None
    with temp_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow([
            "time_us", "ax_raw", "ay_raw", "az_raw", "gx_raw", "gy_raw",
            "gz_raw", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps",
        ])
        for values in SAMPLE.iter_unpack(raw):
            time_us, ax, ay, az, gx, gy, gz = values
            acc = [v * 16.0 / 32768.0 for v in (ax, ay, az)]
            gyro = [v * 2000.0 / 32768.0 for v in (gx, gy, gz)]
            writer.writerow([*values, *(f"{v:.7f}" for v in acc),
                             *(f"{v:.7f}" for v in gyro)])
            if first is None:
                first = values
            if prev_time is not None:
                delta = time_us - prev_time
                min_delta = delta if min_delta is None else min(min_delta, delta)
                max_delta = delta if max_delta is None else max(max_delta, delta)
            prev_time = time_us
            last = values
    temp_path.replace(csv_path)
    shutil.copyfile(CAPTURE / "log_data.bin", OUTPUT / "imu_log_swd.bin")
    print(f"samples={count}, rate={rate} Hz, duration={duration} ms")
    print(f"data CRC32={data_crc:08X}, header CRC32={header_crc:08X}: verified")
    print(f"time_us delta range: {min_delta}..{max_delta}")
    print(f"first={first}")
    print(f"last={last}")
    print(csv_path)


if __name__ == "__main__":
    main()
