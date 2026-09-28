#!/usr/bin/env python3
"""
IMU attitude GUI for STM32G431 + ICM-42688-P.

Features
  - live 3D attitude from USART1 telemetry (Q/R lines)
  - zero/tare button (host side)
  - logger control buttons (R record / D dump CSV / E erase) - these require
    the Phase-11 firmware command interface
  - CSV replay: load a dumped CSV and re-integrate attitude offline

Usage:
    python tools/imu_gui.py
"""

import csv
import math
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import numpy as np
import serial
from serial.tools import list_ports

import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg


# --------------------------------------------------------------------------
# quaternion helpers
# --------------------------------------------------------------------------
def quat_mul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return np.array([
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
    ])


def quat_conj(q):
    return np.array([q[0], -q[1], -q[2], -q[3]])


def quat_to_matrix(q):
    w, x, y, z = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def quat_from_euler(roll_deg, pitch_deg, yaw_deg):
    r = math.radians(roll_deg) * 0.5
    p = math.radians(pitch_deg) * 0.5
    y = math.radians(yaw_deg) * 0.5
    cr, sr = math.cos(r), math.sin(r)
    cp, sp = math.cos(p), math.sin(p)
    cy, sy = math.cos(y), math.sin(y)
    return np.array([
        cr * cp * cy + sr * sp * sy,
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
    ])


def quat_to_euler(q):
    w, x, y, z = q
    roll = math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
    pitch = math.asin(max(-1.0, min(1.0, 2 * (w * y - z * x))))
    yaw = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
    return [math.degrees(v) for v in (roll, pitch, yaw)]


def make_cube(d=1.0):
    v = np.array([
        [-d, -d, -d], [d, -d, -d], [d, d, -d], [-d, d, -d],
        [-d, -d, d], [d, -d, d], [d, d, d], [-d, d, d],
    ])
    edges = [(0, 1), (1, 2), (2, 3), (3, 0),
             (4, 5), (5, 6), (6, 7), (7, 4),
             (0, 4), (1, 5), (2, 6), (3, 7)]
    return v, edges


# --------------------------------------------------------------------------
# offline Mahony (same formulation as the C implementation)
# --------------------------------------------------------------------------
def mahony_offline(acc_g, gyro_dps, t_us, kp=0.5, ki=0.0):
    n = len(t_us)
    q = np.array([1.0, 0.0, 0.0, 0.0])
    integral = np.zeros(3)
    out = np.zeros((n, 4))
    two_kp = 2.0 * kp
    two_ki = 2.0 * ki
    gscale = math.pi / 180.0

    for i in range(n):
        dt = 0.002 if i == 0 else (t_us[i] - t_us[i - 1]) * 1e-6
        dt = min(max(dt, 1e-4), 0.05)
        gx, gy, gz = gyro_dps[i] * gscale
        ax, ay, az = acc_g[i]
        norm = math.sqrt(ax * ax + ay * ay + az * az)
        if norm > 1e-6:
            ax, ay, az = ax / norm, ay / norm, az / norm
            q0, q1, q2, q3 = q
            halfvx = q1 * q3 - q0 * q2
            halfvy = q0 * q1 + q2 * q3
            halfvz = q0 * q0 - 0.5 + q3 * q3
            halfex = ay * halfvz - az * halfvy
            halfey = az * halfvx - ax * halfvz
            halfez = ax * halfvy - ay * halfvx
            if two_ki > 0:
                integral += two_ki * np.array([halfex, halfey, halfez]) * dt
                gx += integral[0]
                gy += integral[1]
                gz += integral[2]
            gx += two_kp * halfex
            gy += two_kp * halfey
            gz += two_kp * halfez

        gx *= 0.5 * dt
        gy *= 0.5 * dt
        gz *= 0.5 * dt
        q = q + np.array([
            -q[1] * gx - q[2] * gy - q[3] * gz,
            q[0] * gx + q[2] * gz - q[3] * gy,
            q[0] * gy - q[1] * gz + q[3] * gx,
            q[0] * gz + q[1] * gy - q[2] * gx,
        ])
        q = q / np.linalg.norm(q)
        out[i] = q
    return out


# --------------------------------------------------------------------------
class ImuGui:
    def __init__(self, root):
        self.root = root
        self.root.title("IMU Attitude GUI - STM32G431 / ICM-42688-P")
        self.root.geometry("1080x720")

        self.ser = None
        self.reader = None
        self.running = False
        self.rx_queue = queue.Queue()
        self.csv_busy = False
        self.q = np.array([1.0, 0.0, 0.0, 0.0])
        self.q_zero = np.array([1.0, 0.0, 0.0, 0.0])
        self.rpy = np.zeros(3)
        self.dump_lines = None
        self.dump_path = None
        self.dump_start = 0.0
        self.replay_q = None
        self.replay_index = 0

        self._build_ui()
        self._refresh_ports()
        self.root.after(33, self._tick)
        self.root.after(30, self._drain_queue)

    # ---------------- UI ----------------
    def _build_ui(self):
        left = ttk.Frame(self.root, padding=10)
        left.pack(side=tk.LEFT, fill=tk.Y)

        ttk.Label(left, text="Serial port").pack(anchor=tk.W)
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(left, textvariable=self.port_var, width=18)
        self.port_box.pack(fill=tk.X)
        ttk.Button(left, text="Refresh", command=self._refresh_ports).pack(fill=tk.X, pady=2)

        self.connect_btn = ttk.Button(left, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(fill=tk.X, pady=(6, 10))

        ttk.Separator(left).pack(fill=tk.X, pady=4)
        ttk.Button(left, text="ZERO  (tare attitude)",
                   command=self._zero).pack(fill=tk.X, pady=2)

        ttk.Separator(left).pack(fill=tk.X, pady=8)
        ttk.Label(left, text="Offline logger").pack(anchor=tk.W)
        ttk.Button(left, text="R  Start recording",
                   command=lambda: self._send(b"R")).pack(fill=tk.X, pady=2)
        ttk.Button(left, text="E  Erase log",
                   command=lambda: self._send(b"E")).pack(fill=tk.X, pady=2)
        ttk.Button(left, text="D  Dump log to CSV",
                   command=self._dump_csv).pack(fill=tk.X, pady=2)

        ttk.Separator(left).pack(fill=tk.X, pady=8)
        ttk.Label(left, text="CSV replay").pack(anchor=tk.W)
        ttk.Button(left, text="Load CSV and replay",
                   command=self._load_csv).pack(fill=tk.X, pady=2)

        self.status = tk.Text(left, width=38, height=14, state=tk.DISABLED)
        self.status.pack(fill=tk.BOTH, expand=True, pady=(10, 0))

        right = ttk.Frame(self.root)
        right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)

        self.fig = plt.Figure(figsize=(7, 6), dpi=100)
        self.ax = self.fig.add_subplot(111, projection="3d")
        self.ax.set_xlim(-2, 2)
        self.ax.set_ylim(-2, 2)
        self.ax.set_zlim(-2, 2)
        self.ax.set_xlabel("X")
        self.ax.set_ylabel("Y")
        self.ax.set_zlabel("Z")

        self.vertices, self.edges = make_cube(1.0)
        self.cube_lines = [self.ax.plot([], [], [], "b-", lw=2)[0] for _ in self.edges]
        self.axis_x = self.ax.plot([], [], [], "r-", lw=3)[0]
        self.axis_y = self.ax.plot([], [], [], "g-", lw=3)[0]
        self.axis_z = self.ax.plot([], [], [], "k-", lw=3)[0]

        self.canvas = FigureCanvasTkAgg(self.fig, master=right)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    def _log(self, text):
        self.status.configure(state=tk.NORMAL)
        self.status.insert(tk.END, text + "\n")
        self.status.see(tk.END)
        self.status.configure(state=tk.DISABLED)

    def _refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    # ---------------- serial ----------------
    def _toggle_connect(self):
        if self.ser:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("Serial", "Select a serial port first")
            return

        # Make sure a previous reader thread is really gone before reopening.
        if self.reader is not None and self.reader.is_alive():
            self.running = False
            self.reader.join(timeout=1.0)
            self.reader = None

        ser = None
        last_exc = None
        for _ in range(4):
            try:
                ser = serial.Serial(port, 115200, timeout=0.05)
                break
            except (serial.SerialException, OSError) as exc:
                last_exc = exc
                time.sleep(0.25)

        if ser is None:
            self._log(f"open {port} failed: {last_exc}")
            messagebox.showerror(
                "Serial",
                f"{port} 打不开：{last_exc}\n\n"
                "刚断开过的话等 1 秒再试，或点 Refresh 重新枚举端口；"
                "也确认没有别的串口工具占用它。",
            )
            return

        # Drop anything left over from a previous session.
        try:
            while True:
                self.rx_queue.get_nowait()
        except queue.Empty:
            pass

        self.ser = ser
        self.running = True
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()
        self.connect_btn.configure(text="Disconnect")
        self._log(f"connected {port} @115200")

    def _disconnect(self):
        self.running = False
        ser = self.ser
        self.ser = None
        if ser is not None:
            try:
                ser.close()
            except (serial.SerialException, OSError):
                pass
        if self.reader is not None and self.reader.is_alive():
            self.reader.join(timeout=1.0)
            if self.reader.is_alive():
                self._log("warning: reader thread still shutting down")
        self.reader = None
        self.connect_btn.configure(text="Connect")
        self._log("disconnected")

    def _send(self, data):
        if not self.ser:
            self._log("serial not connected")
            return
        try:
            self.ser.write(data)
            self._log(f"TX {data!r}")
        except serial.SerialException as exc:
            self._log(f"TX error: {exc}")

    def _read_loop(self):
        """Reader thread: no Tk access here, everything goes through the queue."""
        while self.running:
            ser = self.ser
            if ser is None:
                break
            try:
                raw = ser.readline()
            except (serial.SerialException, OSError):
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="ignore").strip()
            if line:
                self.rx_queue.put(("line", line))

    # ---------------- main-thread queue handling ----------------
    def _drain_queue(self):
        try:
            while True:
                kind, payload = self.rx_queue.get_nowait()
                if kind == "line":
                    self._handle_serial_line(payload)
                elif kind == "csv_done":
                    t_us, quats, path = payload
                    self.replay_q = quats
                    self.replay_index = 0
                    self.csv_busy = False
                    self._log(f"loaded {len(t_us)} samples from {path}")
                elif kind == "csv_error":
                    self.csv_busy = False
                    messagebox.showerror("CSV", payload)
        except queue.Empty:
            pass
        finally:
            # Abort a dump that never finishes (e.g. MCU stopped answering).
            if (self.dump_lines is not None and self.dump_path is not None
                    and (time.time() - self.dump_start) > 120.0):
                self._log("dump timed out (no LOG_END)")
                self.dump_lines = None
                self.dump_path = None
            self.root.after(30, self._drain_queue)

    def _handle_serial_line(self, line):
        if self.dump_lines is not None:
            self.dump_lines.append(line)
            if line.startswith("LOG_END"):
                self._finish_dump()
            return

        parts = line.split()
        if parts and parts[0] == "Q" and len(parts) == 5:
            try:
                q = np.array([float(v) for v in parts[1:]])
                norm = np.linalg.norm(q)
                if norm > 1e-9:
                    self.q = q / norm
            except ValueError:
                pass
        elif parts and parts[0] == "R" and len(parts) == 4:
            try:
                self.rpy = np.array([float(v) for v in parts[1:]])
            except ValueError:
                pass
        else:
            self._log(line)

    # ---------------- actions ----------------
    def _zero(self):
        self.q_zero = self.q.copy()
        if self.ser:
            self._send(b"Z")
        self._log("attitude zero set (host offset + MCU Z command)")

    def _dump_csv(self):
        if not self.ser:
            self._log("serial not connected")
            return
        path = filedialog.asksaveasfilename(defaultextension=".csv",
                                            filetypes=[("CSV", "*.csv")])
        if not path:
            return
        self.dump_path = path
        self.dump_lines = []
        self.dump_start = time.time()
        self._send(b"D")
        self._log(f"dumping log -> {path}")

    def _finish_dump(self):
        lines = self.dump_lines or []
        path = self.dump_path
        self.dump_lines = None
        self.dump_path = None
        if path:
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write("\n".join(lines) + "\n")
            self._log(f"saved {len(lines)} lines -> {path}")

    def _load_csv(self):
        if self.csv_busy:
            self._log("CSV load already in progress")
            return
        path = filedialog.askopenfilename(filetypes=[("CSV", "*.csv")])
        if not path:
            return
        self.csv_busy = True
        self._log(f"loading {path} (attitude integration runs in background) ...")
        threading.Thread(target=self._csv_worker, args=(path,), daemon=True).start()

    def _csv_worker(self, path):
        """Background thread: parse CSV and integrate attitude off the UI thread."""
        try:
            t_us, acc, gyro = self._read_csv(path)
            # Remove the stationary gyro bias (same idea as the firmware's
            # boot calibration). Without it yaw drifts ~60 deg/min per 1 dps.
            n_bias = min(len(gyro), 250)
            if n_bias > 0:
                gyro = gyro - gyro[:n_bias].mean(axis=0)
            # Same IMU mounting remap as the firmware: the sensor sits
            # vertically, so display X=X, display Y=Z, display Z=-Y.
            acc = np.column_stack((acc[:, 0], acc[:, 2], -acc[:, 1]))
            gyro = np.column_stack((gyro[:, 0], gyro[:, 2], -gyro[:, 1]))
            quats = mahony_offline(acc, gyro, t_us)
        except Exception as exc:  # noqa: BLE001
            self.rx_queue.put(("csv_error", str(exc)))
            return
        self.rx_queue.put(("csv_done", (t_us, quats, path)))

    @staticmethod
    def _read_csv(path):
        with open(path, "r", encoding="utf-8") as fh:
            reader = csv.DictReader(fh)
            t_us, acc, gyro = [], [], []
            for row in reader:
                t_us.append(float(row["time_us"]))
                if "ax_g" in row and row.get("ax_g"):
                    acc.append([float(row["ax_g"]), float(row["ay_g"]), float(row["az_g"])])
                    gyro.append([float(row["gx_dps"]), float(row["gy_dps"]), float(row["gz_dps"])])
                else:
                    s = 16.0 / 32768.0
                    acc.append([float(row["ax_raw"]) * s,
                                float(row["ay_raw"]) * s,
                                float(row["az_raw"]) * s])
                    gyro.append([float(row["gx_raw"]) * (2000.0 / 32768.0),
                                 float(row["gy_raw"]) * (2000.0 / 32768.0),
                                 float(row["gz_raw"]) * (2000.0 / 32768.0)])
        return np.array(t_us), np.array(acc), np.array(gyro)

    # ---------------- drawing ----------------
    def _tick(self):
        if self.replay_q is not None and len(self.replay_q):
            if self.replay_index == 0:
                self.replay_start = time.time()
            elapsed = time.time() - self.replay_start
            idx = int(elapsed / 0.04)
            if idx >= len(self.replay_q):
                self.replay_index = 0
                idx = 0
                self.replay_start = time.time()
            q_display = self.replay_q[idx]
            roll, pitch, yaw = quat_to_euler(q_display)
            self.ax.set_title(f"CSV replay {idx + 1}/{len(self.replay_q)}  "
                              f"R {roll:6.1f}  P {pitch:6.1f}  Y {yaw:6.1f}   "
                              f"(X=red  Y=black  Z=green)")
        else:
            q_display = quat_mul(quat_conj(self.q_zero), self.q)
            self.ax.set_title(f"Live  Roll {self.rpy[0]:6.1f}  "
                              f"Pitch {self.rpy[1]:6.1f}  Yaw {self.rpy[2]:6.1f}   "
                              f"(X=red  Y=black  Z=green)")

        rot = quat_to_matrix(q_display)
        p = self.vertices @ rot.T
        for line, (a, b) in zip(self.cube_lines, self.edges):
            line.set_data([p[a, 0], p[b, 0]], [p[a, 1], p[b, 1]])
            line.set_3d_properties([p[a, 2], p[b, 2]])
        # Axis colour mapping requested by the user:
        #   red   = X,  black = Y,  green = Z
        for axis_line, vec in ((self.axis_x, np.array([[1.0, 0, 0]])),    # red   -> X
                               (self.axis_y, np.array([[0, 0, 1.0]])),    # green -> Z
                               (self.axis_z, np.array([[0, 1.0, 0]]))):   # black -> Y
            e = vec @ rot.T
            axis_line.set_data([0, e[0, 0]], [0, e[0, 1]])
            axis_line.set_3d_properties([0, e[0, 2]])
        self.canvas.draw_idle()
        self.root.after(33, self._tick)


def main():
    root = tk.Tk()
    ImuGui(root)
    root.mainloop()


if __name__ == "__main__":
    main()
