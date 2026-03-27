import serial
from serial.tools import list_ports


def auto_detect_port() -> str:
    """
    Auto-detect a likely USB serial port.
    If exactly one likely port is found, return it directly.
    If multiple are found, ask the user to choose.
    """
    ports = list(list_ports.comports())

    if not ports:
        raise RuntimeError("No serial ports found.")

    preferred = []
    others = []

    for p in ports:
        desc = (p.description or "").lower()
        device = (p.device or "").lower()
        hwid = (p.hwid or "").lower()

        score = 0
        keywords = [
            "usb", "acm", "cdc", "serial", "uart",
            "cp210", "ch340", "ch341", "ftdi",
            "ttyusb", "ttyacm", "wch",
            "silicon labs", "arduino", "teensy"
        ]

        for kw in keywords:
            if kw in desc or kw in device or kw in hwid:
                score += 1

        if score > 0:
            preferred.append(p)
        else:
            others.append(p)

    candidates = preferred if preferred else ports

    if len(candidates) == 1:
        p = candidates[0]
        print(f"Auto detected serial port: {p.device} ({p.description})")
        return p.device

    print("Multiple serial ports found:")
    for i, p in enumerate(candidates):
        print(f"[{i}] {p.device}  |  {p.description}  |  {p.hwid}")

    while True:
        s = input("Select port index: ").strip()
        if s.isdigit():
            idx = int(s)
            if 0 <= idx < len(candidates):
                return candidates[idx].device
        print("Invalid selection, try again.")


def open_serial(port: str, baud: int, timeout: float, startup_wait_s: float) -> serial.Serial:
    """
    Open serial port and wait for MCU reset.
    """
    ser = serial.Serial(port, baud, timeout=timeout)
    import time
    time.sleep(startup_wait_s)
    ser.reset_input_buffer()
    return ser