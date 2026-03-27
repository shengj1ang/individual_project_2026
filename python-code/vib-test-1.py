"""
Single-motor (Motor #1) vibration test logger

What it does (10 trials):
  1) Send a single vibration pulse to motor #1 over serial (UDP-style, no ACK expected)
  2) Record ts_command_send = time.time() at the moment the command is sent
  3) Listen for SPACE key down, record ts_keyboard_down = time.time() on keydown
  4) duration = ts_keyboard_down - ts_command_send
  5) Save into SQLite DB: test_log.db

DB columns:
  group_id, ts_command_send, ts_keyboard_down, duration
(We also store trial_index for uniqueness.)
"""

import sys
import time
import sqlite3
import threading
from queue import Queue, Empty

import serial
from pynput import keyboard

# -------------------- User config --------------------
PORT = sys.argv[1] if len(sys.argv) >= 2 else None  # e.g. COM5 or /dev/ttyACM0
BAUD = 115200

GROUP_ID = 3      # <-- set this in Python as you want
TRIALS = 10

AMP = 80              # 0..255
PULSE_MS = 120        # single pulse duration
INTER_TRIAL_S = 0.8   # small pause between trials (optional)

# Optional: set frequencies at start (Arduino may ignore if unsupported)
FREQ1, FREQ2, FREQ3 = 300, 240, 240

DB_PATH = "test_log.db"
TABLE_NAME = "vib_test"
# -----------------------------------------------------


def init_db():
    con = sqlite3.connect(DB_PATH)
    cur = con.cursor()
    cur.execute(f"""
        CREATE TABLE IF NOT EXISTS {TABLE_NAME} (
            group_id INTEGER NOT NULL,
            trial_index INTEGER NOT NULL,
            ts_command_send REAL NOT NULL,
            ts_keyboard_down REAL NOT NULL,
            duration REAL NOT NULL,
            PRIMARY KEY (group_id, trial_index)
        )
    """)
    con.commit()
    return con


def db_insert(con, group_id: int, trial_index: int, ts_cmd: float, ts_key: float, duration: float):
    cur = con.cursor()
    cur.execute(
        f"INSERT OR REPLACE INTO {TABLE_NAME} (group_id, trial_index, ts_command_send, ts_keyboard_down, duration) "
        f"VALUES (?, ?, ?, ?, ?)",
        (group_id, trial_index, ts_cmd, ts_key, duration),
    )
    con.commit()


def open_serial(port: str) -> serial.Serial:
    ser = serial.Serial(port, BAUD, timeout=0.2)
    # Many boards reset when opening serial; give it time to boot.
    time.sleep(1.5)
    ser.reset_input_buffer()
    return ser


def send_line(ser: serial.Serial, line: str) -> None:
    ser.write((line.strip() + "\n").encode("utf-8"))
    ser.flush()


def motor1_single_pulse(ser: serial.Serial, amp: int, pulse_ms: int) -> float:
    """
    Fire-and-forget single pulse on motor #1:
      - Send start command S 1 amp
      - Schedule stop command X after pulse_ms
    Returns ts_command_send from time.time().
    """
    # motor #1 mask = 1 (0b001)
    ts_command_send = time.time()
    send_line(ser, f"S 1 {amp}")

    # Stop later without blocking the keyboard wait
    threading.Timer(pulse_ms / 1000.0, lambda: send_line(ser, "X")).start()
    return ts_command_send


def main():
    if not PORT:
        print("Usage: python vib_test.py <serial_port>")
        print("Example (Windows): python vib_test.py COM5")
        print("Example (macOS/Linux): python vib_test.py /dev/ttyACM0")
        sys.exit(1)

    con = init_db()
    ser = open_serial(PORT)

    # Queue to collect SPACE key-down timestamps
    key_q: Queue[float] = Queue()

    def on_press(key):
        try:
            if key == keyboard.Key.space:
                key_q.put(time.time())
        except Exception:
            pass

    listener = keyboard.Listener(on_press=on_press)
    listener.start()

    try:
        # Robust init: stop twice (in case first command is lost during reset)
        send_line(ser, "X")
        time.sleep(0.05)
        send_line(ser, "X")

        # Optional frequency set (may be ignored by Arduino core)
        send_line(ser, f"F {FREQ1} {FREQ2} {FREQ3}")
        time.sleep(0.05)
        send_line(ser, f"F {FREQ1} {FREQ2} {FREQ3}")

        print("Ready. Press SPACE when you feel the vibration.")
        print(f"Logging to: {DB_PATH} (table: {TABLE_NAME}), group_id={GROUP_ID}")
        print("Press Ctrl+C to abort.\n")

        for trial in range(1, TRIALS + 1):
            # Clear any stale SPACE events before starting this trial
            while True:
                try:
                    key_q.get_nowait()
                except Empty:
                    break

            # Start vibration pulse and record command timestamp
            ts_cmd = motor1_single_pulse(ser, AMP, PULSE_MS)

            # Wait for SPACE key-down timestamp
            try:
                ts_key = key_q.get(timeout=10.0)  # 10s timeout per trial
            except Empty:
                # If no response, store NaN-ish values (or skip). Here we skip logging.
                print(f"[Trial {trial}] No SPACE press detected within timeout. Skipping log.")
                continue

            duration = ts_key - ts_cmd
            db_insert(con, GROUP_ID, trial, ts_cmd, ts_key, duration)

            print(f"[Trial {trial:02d}] ts_cmd={ts_cmd:.6f}  ts_key={ts_key:.6f}  duration={duration*1000:.1f} ms")

            time.sleep(INTER_TRIAL_S)

        # Final safety stop
        send_line(ser, "X")
        print("\nDone.")

    except KeyboardInterrupt:
        send_line(ser, "X")
        print("\nInterrupted. Motor stopped.")
    finally:
        try:
            listener.stop()
        except Exception:
            pass
        ser.close()
        con.close()


if __name__ == "__main__":
    main()