from vibrator import VibratorController

NUM_MOTORS = 10

def main():
    with VibratorController() as vc:
        vc.set_all_freqs([300] * NUM_MOTORS)

        # Turn All vibrators on
        all_mask = (1 << NUM_MOTORS) - 1   # 10port = 1023

        vc.pulse_mask(all_mask, amp=80, on_ms=500)

        vc.stop_all()
        print("All motors pulsed once.")


if __name__ == "__main__":
    main()
