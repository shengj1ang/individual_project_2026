from vibrator import VibratorController

NUM_MOTORS = 10
FREQS = [300] * NUM_MOTORS

AMP = 80
ON_MS = 120
WAIT_BETWEEN_MOTORS_S = 1.5


def main():
    with VibratorController() as vc:
        reply = vc.echo()
        print("Echo reply:", reply)

        vc.set_all_freqs(FREQS)
        vc.test_all_one_by_one(
            amp=AMP,
            on_ms=ON_MS,
            wait_between_motors_s=WAIT_BETWEEN_MOTORS_S,
        )

        vc.stop_all()
        print("Done.")


if __name__ == "__main__":
    main()