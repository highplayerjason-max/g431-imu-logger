#!/usr/bin/env python3
"""
Real-time 3D attitude visualizer for the STM32G431 + ICM-42688-P telemetry.

MCU sends over USART1 (115200 8N1):
    Q q0 q1 q2 q3
    R roll pitch yaw

Usage:
    python tools/imu_viewer.py COM17
"""

import math
import serial
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


def quat_to_matrix(q):
    w, x, y, z = q
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )


def make_cube():
    """12 cube edges, centered at origin, half-size 1."""
    d = 1.0
    v = np.array(
        [
            [-d, -d, -d], [d, -d, -d], [d, d, -d], [-d, d, -d],
            [-d, -d, d],  [d, -d, d],  [d, d, d],  [-d, d, d],
        ]
    )
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),
        (4, 5), (5, 6), (6, 7), (7, 4),
        (0, 4), (1, 5), (2, 6), (3, 7),
    ]
    return v, edges


def main():
    import sys

    port = sys.argv[1] if len(sys.argv) > 1 else "COM17"
    try:
        ser = serial.Serial(port, 115200, timeout=0)
    except serial.SerialException as exc:
        print(f"Cannot open {port}: {exc}")
        print("Check wiring / close other serial monitor, then retry.")
        return

    vertices, edges = make_cube()

    fig = plt.figure(figsize=(7, 6))
    ax = fig.add_subplot(111, projection="3d")
    ax.set_xlim(-2.2, 2.2)
    ax.set_ylim(-2.2, 2.2)
    ax.set_zlim(-2.2, 2.2)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Z")
    ax.set_title("IMU Attitude (Mahony) - waiting for data...")

    cube_lines = [ax.plot([], [], [], "b-", lw=2)[0] for _ in edges]
    axis_x = ax.plot([], [], [], "r-", lw=3)[0]
    axis_y = ax.plot([], [], [], "g-", lw=3)[0]
    axis_z = ax.plot([], [], [], "k-", lw=3)[0]

    state = {
        "q": np.array([1.0, 0.0, 0.0, 0.0]),
        "rpy": np.array([0.0, 0.0, 0.0]),
    }

    def read_latest():
        try:
            while ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                parts = line.split()
                if not parts:
                    continue
                if parts[0] == "Q" and len(parts) == 5:
                    q = np.array([float(v) for v in parts[1:]])
                    norm = np.linalg.norm(q)
                    if norm > 1e-9:
                        state["q"] = q / norm
                elif parts[0] == "R" and len(parts) == 4:
                    state["rpy"] = np.array([float(v) for v in parts[1:]])
        except (ValueError, serial.SerialException):
            pass

    def update(_frame):
        read_latest()
        q = state["q"]
        rot = quat_to_matrix(q)
        p = vertices @ rot.T

        for line, (a, b) in zip(cube_lines, edges):
            line.set_data([p[a, 0], p[b, 0]], [p[a, 1], p[b, 1]])
            line.set_3d_properties([p[a, 2], p[b, 2]])

        origin = np.zeros((1, 3))
        ex = origin @ rot.T
        axis_x.set_data([0, ex[0, 0]], [0, ex[0, 1]])
        axis_x.set_3d_properties([0, ex[0, 2]])
        # Colour mapping: red = X, black = Y, green = Z
        ey = np.array([[0.0, 0.0, 1.0]]) @ rot.T
        axis_y.set_data([0, ey[0, 0]], [0, ey[0, 1]])
        axis_y.set_3d_properties([0, ey[0, 2]])
        ez = np.array([[0.0, 1.0, 0.0]]) @ rot.T
        axis_z.set_data([0, ez[0, 0]], [0, ez[0, 1]])
        axis_z.set_3d_properties([0, ez[0, 2]])

        roll, pitch, yaw = state["rpy"]
        ax.set_title(f"IMU Attitude\nRoll {roll:7.1f}  Pitch {pitch:7.1f}  Yaw {yaw:7.1f}")
        return cube_lines + [axis_x, axis_y, axis_z]

    anim = FuncAnimation(fig, update, interval=33, blit=False)
    try:
        plt.show()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
