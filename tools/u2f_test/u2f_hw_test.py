#!/usr/bin/env python3
"""
u2f_hw_test.py — 実機 (NUCLEO-H533RE) の U2F セキュリティキーを PC から検証する

USB で接続したキーに CTAPHID で U2F (CTAP1) の要求を送り、応答と署名を確かめる。
途中で NUCLEO の B1 (USER) ボタンを押すよう求める。

    pip install fido2
    python tools/u2f_test/u2f_hw_test.py

Windows では FIDO デバイスを直接開くのに管理者権限が必要
(管理者として起動したターミナルから実行する)。
Linux では hidraw へのアクセス権 (FIDO 用 udev ルール) が必要。
"""

import argparse
import hashlib
import os
import sys
import time

from fido2.ctap1 import ApduError, Ctap1
from fido2.hid import CtapHidDevice

SW_CONDITIONS_NOT_SATISFIED = 0x6985
SW_WRONG_DATA = 0x6A80

passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  OK   {name}")
    else:
        failed += 1
        print(f"  FAIL {name} {detail}")


def find_device(vid, pid):
    devs = list(CtapHidDevice.list_devices())
    for d in devs:
        if d.descriptor.vid == vid and d.descriptor.pid == pid:
            return d
    print(f"VID {vid:04X} / PID {pid:04X} の FIDO デバイスが見つかりません。")
    if devs:
        print("見つかった FIDO デバイス:")
        for d in devs:
            print(f"  {d.descriptor.vid:04X}:{d.descriptor.pid:04X} {d.descriptor.product_name}")
    if os.name == "nt":
        print("Windows では管理者として起動したターミナルから実行してください。")
    return None


def expect_sw(name, fn, sw):
    try:
        fn()
        check(name, False, f"(成功してしまった。期待値 {sw:04X})")
    except ApduError as e:
        check(name, e.code == sw, f"(SW={e.code:04X}, 期待値 {sw:04X})")


def with_button(prompt, fn, timeout):
    """ボタンが押されるまで 6985 を受けながら要求を繰り返す。"""
    print(f"  >>> {prompt} ({timeout} 秒以内)")
    end = time.time() + timeout
    while time.time() < end:
        try:
            return fn()
        except ApduError as e:
            if e.code != SW_CONDITIONS_NOT_SATISFIED:
                raise
            time.sleep(0.2)
    raise TimeoutError("ボタンが押されませんでした")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--vid", type=lambda s: int(s, 16), default=0xCAFE)
    ap.add_argument("--pid", type=lambda s: int(s, 16), default=0x4004)
    ap.add_argument("--timeout", type=int, default=30, help="ボタン待ちの秒数")
    args = ap.parse_args()

    dev = find_device(args.vid, args.pid)
    if dev is None:
        return 2
    print(f"デバイス: {dev.descriptor.product_name} "
          f"(CTAPHID v{dev.version}, firmware {'.'.join(map(str, dev.device_version))})")
    ctap = Ctap1(dev)

    print("\n[CTAPHID]")
    data = os.urandom(300)
    check("PING 300 バイト (複数パケット)", dev.ping(data) == data)

    print("\n[VERSION]")
    check("U2F_V2", ctap.get_version() == "U2F_V2")

    app = hashlib.sha256(b"https://t-hid.example").digest()
    other_app = hashlib.sha256(b"https://other.example").digest()
    client = hashlib.sha256(b"client-data-register").digest()

    print("\n[REGISTER]")
    expect_sw("ボタンを押していないと 6985",
              lambda: ctap.register(client, app), SW_CONDITIONS_NOT_SATISFIED)
    t0 = time.time()
    reg = with_button("B1 を押してください (登録)",
                      lambda: ctap.register(client, app), args.timeout)
    print(f"  応答まで {time.time() - t0:.1f} 秒")
    check("公開鍵 65 バイト (非圧縮点)",
          len(reg.public_key) == 65 and reg.public_key[0] == 0x04)
    check("キーハンドル 64 バイト", len(reg.key_handle) == 64)
    try:
        reg.verify(app, client)
        check("アテステーション署名の検証", True)
    except Exception as e:  # noqa: BLE001
        check("アテステーション署名の検証", False, repr(e))
    try:
        from cryptography import x509
        cert = x509.load_der_x509_certificate(reg.certificate)
        print(f"  証明書: {cert.subject.rfc4514_string()}")
        check("証明書を X.509 として読める", True)
    except Exception as e:  # noqa: BLE001
        check("証明書を X.509 として読める", False, repr(e))

    print("\n[AUTHENTICATE]")
    auth_client = hashlib.sha256(b"client-data-auth").digest()
    expect_sw("check-only は 6985 (自分のキーハンドル)",
              lambda: ctap.authenticate(auth_client, app, reg.key_handle, check_only=True),
              SW_CONDITIONS_NOT_SATISFIED)
    expect_sw("他サイトのキーハンドルは 6A80",
              lambda: ctap.authenticate(auth_client, other_app, reg.key_handle),
              SW_WRONG_DATA)
    bad_handle = bytes([reg.key_handle[0] ^ 1]) + reg.key_handle[1:]
    expect_sw("改ざんしたキーハンドルは 6A80",
              lambda: ctap.authenticate(auth_client, app, bad_handle),
              SW_WRONG_DATA)
    expect_sw("ボタンを押していないと 6985",
              lambda: ctap.authenticate(auth_client, app, reg.key_handle),
              SW_CONDITIONS_NOT_SATISFIED)

    counters = []
    for i in range(2):
        sig = with_button(f"B1 を押してください (認証 {i + 1}/2)",
                          lambda: ctap.authenticate(auth_client, app, reg.key_handle),
                          args.timeout)
        check(f"認証 {i + 1}: user presence フラグ", sig.user_presence & 1 == 1)
        try:
            sig.verify(app, auth_client, reg.public_key)
            check(f"認証 {i + 1}: 署名の検証", True)
        except Exception as e:  # noqa: BLE001
            check(f"認証 {i + 1}: 署名の検証", False, repr(e))
        counters.append(sig.counter)
        print(f"  カウンタ: {sig.counter}")
    check("カウンタが増える", counters[1] > counters[0], f"({counters})")

    print(f"\n結果: {passed} 件成功, {failed} 件失敗")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
