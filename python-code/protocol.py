def motor_mask(index_0based: int) -> int:
    """
    Convert motor index 0..9 to a bitmask.
    0 -> 1
    1 -> 2
    2 -> 4
    ...
    """
    if index_0based < 0:
        raise ValueError("motor index must be >= 0")
    return 1 << index_0based


def validate_amp(amp: int) -> None:
    if not (0 <= amp <= 255):
        raise ValueError("amp must be in range 0..255")


def validate_motor_index(index: int, num_motors: int) -> None:
    if not (0 <= index < num_motors):
        raise ValueError(f"motor index must be in range 0..{num_motors - 1}")


def validate_freqs(freqs, num_motors: int) -> None:
    if len(freqs) != num_motors:
        raise ValueError(f"freqs must contain exactly {num_motors} items")
    for f in freqs:
        if int(f) <= 0:
            raise ValueError("all frequencies must be > 0")