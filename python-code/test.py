from vibrator import VibratorController

with VibratorController() as vc:
    print(vc.echo())              # 看固件版本
    vc.set_all_freqs([300] * 10)  # 10 个口都设成 300Hz
    vc.pulse_motor(0, amp=80, on_ms=120)   # 震 0 号口一次
    vc.pulse_motor_n(3, n=3, amp=100, on_ms=150, off_ms=100)  # 3 号口震 3 次
    vc.test_all_one_by_one()      # 0 到 9 全测一遍
