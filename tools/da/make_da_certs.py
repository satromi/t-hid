#!/usr/bin/env python3
"""
make_da_certs.py — STM32H5 のデバッグ認証 (DA) 用の鍵と証明書を作る

TrustZone を有効にした STM32H5 は、product state を進めた後のデバッグの再開と
Open への戻し (regression) を、証明書によるデバッグ認証で行う。証明書は PSA ADAC 形式で、
root → intermediate → leaf の 3 段。デバイスには root 公開鍵のハッシュだけを登録し
(STM32TrustedPackageCreator_CLI -obk で OBK ファイルにする)、認証時には leaf の秘密鍵と
証明書チェーンを使う。

    python tools/da/make_da_certs.py keys  <出力ディレクトリ>   # 鍵 3 つを新しく作る
    python tools/da/make_da_certs.py certs <鍵のディレクトリ>   # 証明書を作る

鍵のディレクトリには key_1_root.pem / key_2_intermediate.pem / key_3_leaf.pem
(秘密鍵) と、それぞれの _pub.pem を置く。証明書は同じディレクトリに
cert_root.b64 / cert_intermediate.b64 / cert_leaf.b64 / cert_leaf_chain.b64 を作る。

証明書の中身 (212 バイト + 8 バイトの TLV ヘッダ)
    TLV       予約 (0) | 種類 0x0201 | 長さ 0xD4 (すべてリトルエンディアン)
    ヘッダ    版 1.0 | 署名 ECDSA P-256 | 鍵 ECDSA P-256 | 役割 | 用途 | 予約
              ライフサイクル | OEM 制約 | 拡張の長さ | SoC クラス | SoC ID (16)
              権限マスク (16)
    公開鍵    X || Y (64)
    拡張      SHA-256 (32、拡張なしなので 0)
    署名      r || s (64)。TLV を除く先頭から拡張までを、発行元の秘密鍵で署名する

秘密鍵をなくすとデバイスを Open に戻せなくなる。安全な場所に控えを取ること。
"""

import base64
import os
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

KEYS = ["key_1_root", "key_2_intermediate", "key_3_leaf"]
ROLE_ROOT, ROLE_INTERMEDIATE, ROLE_LEAF = 1, 2, 3
PERMISSIONS = 0x00005077		# ST の既定値 (DA_Config.xml の Permission と合わせる)

TLV_TYPE_CERT = 0x0201
SIGNED_END = 0x9C			# 署名の対象はこのオフセットの手前まで (TLV の後から)


def cert_body(role, pub, permissions):
	n = pub.public_numbers()
	header = struct.pack("<BBBBBBHHHII", 1, 0, 1, 1, role, 1, 0, 0, 0, 0, 0)
	header += bytes(16)						# SoC ID
	header += struct.pack("<I", permissions) + bytes(12)	# 権限マスク
	return header + n.x.to_bytes(32, "big") + n.y.to_bytes(32, "big") + bytes(32)


def make_cert(role, subject_pub, issuer_priv, permissions=PERMISSIONS):
	body = cert_body(role, subject_pub, permissions)
	der = issuer_priv.sign(body, ec.ECDSA(hashes.SHA256()))
	r, s = decode_dss_signature(der)
	cert = body + r.to_bytes(32, "big") + s.to_bytes(32, "big")
	tlv = struct.pack("<HHI", 0, TLV_TYPE_CERT, len(cert))
	out = tlv + cert
	assert len(out) == 220 and len(body) + 8 == SIGNED_END
	return out


def load_priv(path):
	return serialization.load_pem_private_key(open(path, "rb").read(), None)


def cmd_keys(outdir):
	os.makedirs(outdir, exist_ok=True)
	for k in KEYS:
		path = os.path.join(outdir, k + ".pem")
		if os.path.exists(path):
			sys.exit(f"{path} がすでにあります。上書きしません")
	for k in KEYS:
		priv = ec.generate_private_key(ec.SECP256R1())
		open(os.path.join(outdir, k + ".pem"), "wb").write(priv.private_bytes(
			serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
			serialization.NoEncryption()))
		open(os.path.join(outdir, k + "_pub.pem"), "wb").write(priv.public_key().public_bytes(
			serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
	print(f"鍵を作りました: {outdir}")


def cmd_certs(keydir):
	root, inter, leaf = (load_priv(os.path.join(keydir, k + ".pem")) for k in KEYS)
	certs = {
		"cert_root.b64": make_cert(ROLE_ROOT, root.public_key(), root),
		"cert_intermediate.b64": make_cert(ROLE_INTERMEDIATE, inter.public_key(), root),
		"cert_leaf.b64": make_cert(ROLE_LEAF, leaf.public_key(), inter),
	}
	certs["cert_leaf_chain.b64"] = (certs["cert_root.b64"] + certs["cert_intermediate.b64"]
					+ certs["cert_leaf.b64"])
	for name, data in certs.items():
		open(os.path.join(keydir, name), "wb").write(base64.b64encode(data))
	print(f"証明書を作りました: {keydir}")


if __name__ == "__main__":
	if len(sys.argv) != 3 or sys.argv[1] not in ("keys", "certs"):
		sys.exit(__doc__)
	(cmd_keys if sys.argv[1] == "keys" else cmd_certs)(sys.argv[2])
