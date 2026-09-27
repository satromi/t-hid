#!/usr/bin/env python3
"""
u2f_stress.py — 実機 (NUCLEO-H533RE) の U2F セキュリティキーに要求を流し続ける

B1 ボタンを押さずに済む要求 (PING、VERSION、ボタンなしの登録、他人のキーハンドルでの
認証) を指定した時間繰り返し、応答が返らなくなる (USB が止まる) ことがないかを確かめる。
登録と認証はセキュア側の処理を通るので、TrustZone 構成のタスク切り替えの確認にもなる。

    python tools/u2f_test/u2f_stress.py --minutes 10

Windows では管理者として起動したターミナルから実行する。
"""

import argparse
import hashlib
import os
import sys
import threading
import time

from fido2.ctap1 import ApduError, Ctap1
from fido2.hid import CtapHidDevice

SW_CONDITIONS_NOT_SATISFIED = 0x6985
SW_WRONG_DATA = 0x6A80
STALL_SECONDS = 5


def find_device(vid, pid):
    for d in CtapHidDevice.list_devices():
        if d.descriptor.vid == vid and d.descriptor.pid == pid:
            return d
    print(f"VID {vid:04X} / PID {pid:04X} の FIDO デバイスが見つかりません。")
    if os.name == "nt":
        print("Windows では管理者として起動したターミナルから実行してください。")
    return None


def expect_sw(fn, sw):
    try:
        fn()
    except ApduError as e:
        return e.code == sw
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--vid", type=lambda s: int(s, 16), default=0xCAFE)
    ap.add_argument("--pid", type=lambda s: int(s, 16), default=0x4004)
    ap.add_argument("--minutes", type=float, default=10)
    args = ap.parse_args()

    dev = find_device(args.vid, args.pid)
    if dev is None:
        return 2
    ctap = Ctap1(dev)
    app = hashlib.sha256(b"https://t-hid.example").digest()

    state = {"op": "", "since": time.time(), "stalled": False}

    def watchdog():
        while True:
            time.sleep(1)
            if state["op"] and time.time() - state["since"] > STALL_SECONDS and not state["stalled"]:
                state["stalled"] = True
                print(f"\n応答なし: {state['op']} が {STALL_SECONDS} 秒以上返りません (USB が止まった可能性)",
                      flush=True)

    threading.Thread(target=watchdog, daemon=True).start()

    def run(name, fn):
        state["op"], state["since"] = name, time.time()
        ok = fn()
        state["op"] = ""
        return ok

    end = time.time() + args.minutes * 60
    n = fails = 0
    worst = 0.0
    while time.time() < end:
        t0 = time.time()
        data = os.urandom(1 + n % 1000)
        checks = [
            run("PING", lambda: dev.ping(data) == data),
            run("VERSION", lambda: ctap.get_version() == "U2F_V2"),
            run("REGISTER (ボタンなし)",
                lambda: expect_sw(lambda: ctap.register(os.urandom(32), app),
                                  SW_CONDITIONS_NOT_SATISFIED)),
            run("AUTHENTICATE (他人のキーハンドル)",
                lambda: expect_sw(lambda: ctap.authenticate(os.urandom(32), app, os.urandom(64)),
                                  SW_WRONG_DATA)),
        ]
        worst = max(worst, time.time() - t0)
        n += 1
        if not all(checks):
            fails += 1
            print(f"  {n}: 期待と違う応答 {checks}", flush=True)
        if n % 50 == 0:
            left = max(0, end - time.time())
            print(f"  {n} 回 (失敗 {fails}, 1 回の最長 {worst:.2f} 秒, 残り {left / 60:.1f} 分)", flush=True)

    print(f"\n結果: {n} 回, 失敗 {fails}, 1 回の最長 {worst:.2f} 秒"
          + (", 応答なしあり" if state["stalled"] else ""))
    return 0 if fails == 0 and not state["stalled"] else 1


if __name__ == "__main__":
    sys.exit(main())
