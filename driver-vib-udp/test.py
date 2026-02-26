import sys
import time
import serial

BAUD = 115200
READ_TIMEOUT = 0.3

# Tunable parameters
AMP = 80                 # PWM duty (0..255)
ON_MS = 120              # Duration of each pulse (ms)
OFF_MS = 120             # Interval between pulses inside the same group (ms)
WAIT_BETWEEN_GROUPS_S = 2.0  # Delay between motor groups (seconds)

# PWM frequency settings (Hz)
FREQ1, FREQ2, FREQ3 = 300, 240, 240


def open_serial(port: str) -> serial.Serial:
    """
    Open the serial port and wait for the board to reboot (many boards reset on serial open).
    We use a small delay and clear the input buffer to avoid stale data.
    """
    ser = serial.Serial(port, BAUD, timeout=READ_TIMEOUT)
    time.sleep(1.5)            # give the MCU time to reset and start
    ser.reset_input_buffer()   # drop any boot garbage
    return ser


def send_line(ser: serial.Serial, line: str) -> None:
    """Send one command line (newline-terminated). No ACK/ERR is expected (UDP-style)."""
    ser.write((line.strip() + "\n").encode("utf-8"))
    ser.flush()


def motor_mask(motor_index_1based: int) -> int:
    """
    Convert motor index (1..3) into a bitmask:
      motor 1 -> 0b001 (1)
      motor 2 -> 0b010 (2)
      motor 3 -> 0b100 (4)
    """
    return 1 << (motor_index_1based - 1)


def vibrate_once(ser: serial.Serial, mask: int, amp: int, on_ms: int) -> None:
    """
    Single pulse:
      - Start vibration (S mask amp)
      - Wait on_ms
      - Stop vibration (X)
    No serial replies are read (fire-and-forget).
    """
    send_line(ser, f"S {mask} {amp}")
    time.sleep(on_ms / 1000.0)
    send_line(ser, "X")


def vibrate_n(ser: serial.Serial, mask: int, amp: int, n: int, on_ms: int, off_ms: int) -> None:
    """
    Repeat n pulses.
    Between pulses, wait off_ms (except after the last pulse).
    """
    for i in range(n):
        vibrate_once(ser, mask, amp, on_ms)
        if i != n - 1:
            time.sleep(off_ms / 1000.0)


def main():
    if len(sys.argv) < 2:
        print("Usage: python play_pattern.py <serial_port>")
        print("Example (Windows): python play_pattern.py COM5")
        print("Example (macOS/Linux): python play_pattern.py /dev/ttyACM0")
        sys.exit(1)

    port = sys.argv[1]
    ser = open_serial(port)

    try:
        # Robust UDP-style init:
        # Send STOP twice in case the first command is lost during MCU reset.
        send_line(ser, "X")
        time.sleep(0.05)
        send_line(ser, "X")

        # Set PWM frequencies (the MCU may ignore this if not supported by the core).
        # Send twice for the same reason (possible first-command loss on reset).
        send_line(ser, f"F {FREQ1} {FREQ2} {FREQ3}")
        time.sleep(0.05)
        send_line(ser, f"F {FREQ1} {FREQ2} {FREQ3}")

        # Sequence you requested:
        # Motor 1: single pulse -> wait 2s
        # Motor 2: double pulse -> wait 2s
        # Motor 3: triple pulse
        print("Motor 1: single")
        vibrate_n(ser, motor_mask(1), AMP, n=1, on_ms=ON_MS, off_ms=OFF_MS)

        time.sleep(WAIT_BETWEEN_GROUPS_S)

        print("Motor 2: double")
        vibrate_n(ser, motor_mask(2), AMP, n=2, on_ms=ON_MS, off_ms=OFF_MS)

        time.sleep(WAIT_BETWEEN_GROUPS_S)

        print("Motor 3: triple")
        vibrate_n(ser, motor_mask(3), AMP, n=3, on_ms=ON_MS, off_ms=OFF_MS)

        # Final safety stop
        send_line(ser, "X")
        print("Done.")

    finally:
        ser.close()


if __name__ == "__main__":
    main()