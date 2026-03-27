import time
import serial

from serial_utils import auto_detect_port, open_serial
from protocol import (
    motor_mask,
    validate_amp,
    validate_motor_index,
    validate_freqs,
)


class VibratorController:
    """
    High-level controller for the Arduino vibration board.

    Supported commands on MCU side:
      X
      S mask amp
      F f0 f1 f2 ... f9
      E

    Example:
        vc = VibratorController()
        vc.connect()
        vc.echo()
        vc.set_all_freqs([300] * 10)
        vc.pulse_motor(0, amp=80, on_ms=120)
        vc.close()
    """

    def __init__(
        self,
        port: str | None = None,
        baud: int = 115200,
        read_timeout: float = 0.3,
        startup_wait_s: float = 1.5,
        init_retry_gap_s: float = 0.05,
        num_motors: int = 10,
    ):
        self.port = port
        self.baud = baud
        self.read_timeout = read_timeout
        self.startup_wait_s = startup_wait_s
        self.init_retry_gap_s = init_retry_gap_s
        self.num_motors = num_motors
        self.ser: serial.Serial | None = None

    def connect(self) -> None:
        """
        Open serial port. If port is None, auto-detect.
        Then do a basic init sequence.
        """
        if self.port is None:
            self.port = auto_detect_port()

        self.ser = open_serial(
            port=self.port,
            baud=self.baud,
            timeout=self.read_timeout,
            startup_wait_s=self.startup_wait_s,
        )

        self.stop_all()
        time.sleep(self.init_retry_gap_s)
        self.stop_all()

    def close(self) -> None:
        if self.ser is not None:
            self.ser.close()
            self.ser = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def _require_serial(self) -> serial.Serial:
        if self.ser is None:
            raise RuntimeError("Serial port is not connected. Call connect() first.")
        return self.ser

    def send_line(self, line: str) -> None:
        ser = self._require_serial()
        ser.write((line.strip() + "\n").encode("utf-8"))
        ser.flush()

    def read_line(self, timeout_s: float = 1.0) -> str | None:
        """
        Read one line from serial. Return None on timeout.
        """
        ser = self._require_serial()
        old_timeout = ser.timeout
        ser.timeout = timeout_s
        try:
            raw = ser.readline()
            if not raw:
                return None
            return raw.decode("utf-8", errors="replace").strip()
        finally:
            ser.timeout = old_timeout

    def echo(self, timeout_s: float = 1.0) -> str | None:
        """
        Send E and read back one response line, for example:
            E v1.0.0
        """
        self.send_line("E")
        return self.read_line(timeout_s=timeout_s)

    def stop_all(self) -> None:
        self.send_line("X")

    def start_mask(self, mask: int, amp: int) -> None:
        validate_amp(amp)
        max_mask = (1 << self.num_motors) - 1
        if not (0 <= mask <= max_mask):
            raise ValueError(f"mask must be in range 0..{max_mask}")
        self.send_line(f"S {mask} {amp}")

    def set_all_freqs(self, freqs) -> None:
        validate_freqs(freqs, self.num_motors)
        cmd = "F " + " ".join(str(int(f)) for f in freqs)
        self.send_line(cmd)
        time.sleep(self.init_retry_gap_s)
        self.send_line(cmd)

    def pulse_mask(self, mask: int, amp: int = 80, on_ms: int = 120) -> None:
        self.start_mask(mask, amp)
        time.sleep(on_ms / 1000.0)
        self.stop_all()

    def pulse_motor(self, motor_index: int, amp: int = 80, on_ms: int = 120) -> None:
        validate_motor_index(motor_index, self.num_motors)
        self.pulse_mask(motor_mask(motor_index), amp=amp, on_ms=on_ms)

    def pulse_motor_n(
        self,
        motor_index: int,
        n: int,
        amp: int = 80,
        on_ms: int = 120,
        off_ms: int = 120,
    ) -> None:
        validate_motor_index(motor_index, self.num_motors)
        if n <= 0:
            raise ValueError("n must be > 0")

        for i in range(n):
            self.pulse_motor(motor_index, amp=amp, on_ms=on_ms)
            if i != n - 1:
                time.sleep(off_ms / 1000.0)

    def test_all_one_by_one(
        self,
        amp: int = 80,
        on_ms: int = 120,
        wait_between_motors_s: float = 1.5,
    ) -> None:
        for motor_idx in range(self.num_motors):
            print(f"Testing motor {motor_idx}")
            self.pulse_motor(motor_idx, amp=amp, on_ms=on_ms)
            if motor_idx != self.num_motors - 1:
                time.sleep(wait_between_motors_s)

    def run_pattern(
        self,
        pattern,
        amp: int = 80,
        on_ms: int = 120,
        off_ms: int = 120,
        wait_between_groups_s: float = 2.0,
    ) -> None:
        """
        pattern example:
            [(0, 1), (1, 2), (2, 3)]
        means:
            motor 0 pulse once
            motor 1 pulse twice
            motor 2 pulse three times
        """
        for idx, (motor_index, count) in enumerate(pattern):
            print(f"Motor {motor_index}: x{count}")
            self.pulse_motor_n(
                motor_index=motor_index,
                n=count,
                amp=amp,
                on_ms=on_ms,
                off_ms=off_ms,
            )
            if idx != len(pattern) - 1:
                time.sleep(wait_between_groups_s)