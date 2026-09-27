/*
 * u2f_test.c — U2F 認証器 (app_h533/u2f) を PC 上で検証する
 *
 * 実機と同じ ctaphid.c / u2f.c / u2f_cert.c / u2f_store.c / u2f_crypto.c を
 * PC 向けにビルドし、ホスト (ブラウザ) 側の役を演じて CTAPHID パケットを
 * やり取りする。P-256 は micro-ecc、Flash は RAM 上の配列で代替する。
 *
 *   1. 鍵生成と保存、再起動 (再読み込み) で同じ鍵とカウンタが続くこと
 *   2. CTAPHID: INIT / PING (複数パケット) / 不正 SEQ / 別チャネルの BUSY /
 *      タイムアウト / 不正 CID
 *   3. U2F: VERSION / REGISTER / AUTHENTICATE の応答形式と署名の正しさ、
 *      利用者確認なしの 6985、他サイトのキーハンドルの 6A80、カウンタの増加
 *   4. カウンタの面切り替え (1 面 512 エントリを超えても単調増加)
 *   5. 保存済み鍵の破損を検出して再生成すること
 *
 * アテステーション証明書は attestation.der に書き出す (openssl で検証する)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "u2f.h"
#include "u2f_store.h"
#include "ctaphid.h"
#include "uECC.h"
#include "../../device/flash/stm32h5_flash.h"

/*======================================================================
 * 代替: 乱数 / Flash / μT-Kernel
 *====================================================================*/

LOCAL UW rng_state = 0x12345678;

INT u2f_host_random(UB *buf, UW len)
{
	UW i;
	for (i = 0; i < len; i++) {
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 17;
		rng_state ^= rng_state << 5;
		buf[i] = (UB)(rng_state >> 11);
	}
	return 1;
}

UB fake_flash[512 * 1024];
LOCAL UW erase_count;

ER h5_flash_erase(UW offset, UW len)
{
	UW start = offset & ~(8192u - 1);
	UW end = (offset + len + 8191u) & ~(8192u - 1);
	memset(&fake_flash[start], 0xFF, end - start);
	erase_count++;
	return E_OK;
}

ER h5_flash_program(UW offset, const void *data, UW len)
{
	const UB *src = (const UB *)data;
	UW i;
	if (offset % 16) return E_PAR;
	for (i = 0; i < len; i++) {
		if (fake_flash[offset + i] != 0xFF) return E_IO;	/* 未消去への書き込み */
		fake_flash[offset + i] = src[i];
	}
	return E_OK;
}

ER h5_flash_write(UW offset, const void *data, UW len)
{
	ER err = h5_flash_erase(offset, len);
	return (err == E_OK) ? h5_flash_program(offset, data, len) : err;
}

void *Kmalloc(size_t size) { return malloc(size); }
void Kfree(void *p) { free(p); }

/*======================================================================
 * ホスト役: 送受信パケットの記録
 *====================================================================*/

LOCAL INT failures;
#define CHECK(cond, ...) do { \
	if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } \
} while (0)

#define MAX_OUT		64
LOCAL UB out_pkts[MAX_OUT][CTAPHID_PKT_SIZE];
LOCAL INT n_out;
LOCAL BOOL present;	/* 次の確認要求に応じるか */
LOCAL UW now;

LOCAL ER cap_send(const UB pkt[CTAPHID_PKT_SIZE])
{
	if (n_out < MAX_OUT) memcpy(out_pkts[n_out], pkt, CTAPHID_PKT_SIZE);
	n_out++;
	return E_OK;
}

LOCAL BOOL cap_present(void)
{
	BOOL r = present;
	present = FALSE;	/* 1 回で消費 */
	return r;
}

LOCAL void put_cid(UB *p, UW cid)
{
	p[0] = (UB)(cid >> 24); p[1] = (UB)(cid >> 16); p[2] = (UB)(cid >> 8); p[3] = (UB)cid;
}

LOCAL UW get_cid(const UB *p)
{
	return ((UW)p[0] << 24) | ((UW)p[1] << 16) | ((UW)p[2] << 8) | p[3];
}

/* メッセージをパケットに分けて送る (ホスト → 認証器) */
LOCAL void host_send(UW cid, UB cmd, const UB *data, UW len)
{
	UB pkt[CTAPHID_PKT_SIZE];
	UW off, n;
	UB seq = 0;

	memset(pkt, 0, sizeof(pkt));
	put_cid(pkt, cid);
	pkt[4] = 0x80 | cmd;
	pkt[5] = (UB)(len >> 8);
	pkt[6] = (UB)len;
	n = (len < 57) ? len : 57;
	memcpy(&pkt[7], data, n);
	ctaphid_recv(pkt, now);
	for (off = n; off < len; off += n) {
		memset(pkt, 0, sizeof(pkt));
		put_cid(pkt, cid);
		pkt[4] = seq++;
		n = (len - off < 59) ? len - off : 59;
		memcpy(&pkt[5], &data[off], n);
		ctaphid_recv(pkt, now);
	}
}

/* 記録したパケットから応答メッセージを組み立てる */
LOCAL BOOL host_recv(UW *cid, UB *cmd, UB *data, UW *len)
{
	UW bcnt, got, n;
	INT i;
	UB seq = 0;

	if (n_out == 0) return FALSE;
	*cid = get_cid(out_pkts[0]);
	if (!(out_pkts[0][4] & 0x80)) return FALSE;
	*cmd = out_pkts[0][4] & 0x7F;
	bcnt = ((UW)out_pkts[0][5] << 8) | out_pkts[0][6];
	got = (bcnt < 57) ? bcnt : 57;
	memcpy(data, &out_pkts[0][7], got);
	for (i = 1; i < n_out && got < bcnt; i++) {
		if (get_cid(out_pkts[i]) != *cid || out_pkts[i][4] != seq++) return FALSE;
		n = (bcnt - got < 59) ? bcnt - got : 59;
		memcpy(&data[got], &out_pkts[i][5], n);
		got += n;
	}
	*len = bcnt;
	return got == bcnt;
}

/* 1 往復: 送って応答を受け取る */
LOCAL BOOL transact(UW cid, UB cmd, const UB *req, UW req_len,
		    UB *rcmd, UB *resp, UW *resp_len)
{
	UW rcid;
	n_out = 0;
	host_send(cid, cmd, req, req_len);
	if (!host_recv(&rcid, rcmd, resp, resp_len)) return FALSE;
	return rcid == cid;
}

LOCAL UH sw_of(const UB *resp, UW len)
{
	return (len >= 2) ? (UH)((resp[len - 2] << 8) | resp[len - 1]) : 0;
}

/* 拡張長の APDU を作る */
LOCAL UW make_apdu(UB ins, UB p1, const UB *data, UW len, UB *out)
{
	UW o = 0;
	out[o++] = 0x00; out[o++] = ins; out[o++] = p1; out[o++] = 0x00;
	if (len > 0) {
		out[o++] = 0x00; out[o++] = (UB)(len >> 8); out[o++] = (UB)len;
		memcpy(&out[o], data, len); o += len;
	}
	out[o++] = 0x00; out[o++] = 0x00;	/* Le */
	return o;
}

/*======================================================================
 * DER 解析 (最小限)
 *====================================================================*/

/* TLV の長さを読み、ヘッダ長を返す */
LOCAL UW der_len(const UB *p, UW *len)
{
	if (p[1] < 0x80) { *len = p[1]; return 2; }
	if (p[1] == 0x81) { *len = p[2]; return 3; }
	*len = ((UW)p[2] << 8) | p[3];
	return 4;
}

/* ECDSA-Sig-Value (DER) → r||s */
LOCAL BOOL der_to_rs(const UB *der, UW der_len_total, UB rs[64], UW *consumed)
{
	UW seq_len, h, i;
	const UB *p;
	UW part;

	if (der[0] != 0x30) return FALSE;
	h = der_len(der, &seq_len);
	p = der + h;
	memset(rs, 0, 64);
	for (part = 0; part < 2; part++) {
		UW l;
		if (p[0] != 0x02) return FALSE;
		l = p[1];
		if (l == 0 || l > 33) return FALSE;
		/* 先頭の 0x00 はビット 7 が立つ値の前にだけ許される */
		if (l > 1 && p[2] == 0x00 && !(p[3] & 0x80)) return FALSE;
		for (i = 0; i < l; i++) {
			INT dst = (INT)(32 - l + i);
			if (dst >= 0) rs[part * 32 + dst] = p[2 + i];
			else if (p[2 + i] != 0x00) return FALSE;
		}
		p += 2 + l;
	}
	*consumed = h + seq_len;
	return *consumed <= der_len_total && (UW)(p - der) == *consumed;
}

/* 証明書の subjectPublicKeyInfo から 04 X Y を探す */
LOCAL BOOL cert_pubkey(const UB *cert, UW len, UB pub[64])
{
	UW i;
	for (i = 0; i + 67 <= len; i++) {
		if (cert[i] == 0x03 && cert[i + 1] == 0x42 && cert[i + 2] == 0x00 && cert[i + 3] == 0x04) {
			memcpy(pub, &cert[i + 4], 64);
			return TRUE;
		}
	}
	return FALSE;
}

/*======================================================================
 * テスト
 *====================================================================*/

LOCAL void boot(void)
{
	T_CTAPHID_IF ifc;
	CHECK(u2f_crypto_init() == E_OK, "crypto init / self-test");
	CHECK(u2f_init() == E_OK, "u2f init");
	ifc.send = cap_send;
	ifc.user_present = cap_present;
	ifc.wink = NULL;
	ifc.random = u2f_random;
	ctaphid_init(&ifc);
}

LOCAL UW do_init(void)
{
	UB nonce[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	UB resp[64], rcmd;
	UW rlen, cid;

	CHECK(transact(CTAPHID_BROADCAST_CID, CTAPHID_INIT, nonce, 8, &rcmd, resp, &rlen), "INIT transact");
	CHECK(rcmd == CTAPHID_INIT && rlen == 17, "INIT resp cmd=%02x len=%u", rcmd, rlen);
	CHECK(memcmp(resp, nonce, 8) == 0, "INIT nonce echo");
	cid = get_cid(&resp[8]);
	CHECK(cid != 0 && cid != CTAPHID_BROADCAST_CID, "INIT cid");
	CHECK(resp[12] == 2, "protocol version");
	return cid;
}

LOCAL UB app1[32], app2[32], chal[32];
LOCAL UB reg_pub[64], reg_kh[64];

LOCAL void test_register(UW cid)
{
	UB req[512], resp[1024], data[64], rcmd, rs[64], hash[32], att_pub[64];
	UB msg[1 + 32 + 32 + 64 + 65];
	UW rlen, n, pos, cert_len, h, used;
	FILE *fp;

	printf("[register]\n");
	memcpy(&data[0], chal, 32);
	memcpy(&data[32], app1, 32);
	n = make_apdu(0x01, 0x00, data, 64, req);

	present = FALSE;
	CHECK(transact(cid, CTAPHID_MSG, req, n, &rcmd, resp, &rlen), "REGISTER (no presence)");
	CHECK(sw_of(resp, rlen) == 0x6985 && rlen == 2, "no presence → 6985 (got %04x)", sw_of(resp, rlen));

	present = TRUE;
	CHECK(transact(cid, CTAPHID_MSG, req, n, &rcmd, resp, &rlen), "REGISTER");
	CHECK(sw_of(resp, rlen) == 0x9000, "REGISTER SW %04x", sw_of(resp, rlen));
	printf("  response %u bytes in %d packets\n", rlen, n_out);
	CHECK(n_out > 1, "multi-packet response");

	pos = 0;
	CHECK(resp[pos++] == 0x05, "reserved byte 05");
	CHECK(resp[pos++] == 0x04, "uncompressed point");
	memcpy(reg_pub, &resp[pos], 64); pos += 64;
	CHECK(uECC_valid_public_key(reg_pub, uECC_secp256r1()), "credential pubkey on curve");
	CHECK(resp[pos++] == 64, "KH length");
	memcpy(reg_kh, &resp[pos], 64); pos += 64;

	/* 証明書 */
	CHECK(resp[pos] == 0x30, "cert SEQUENCE");
	h = der_len(&resp[pos], &cert_len);
	cert_len += h;
	fp = fopen("attestation.der", "wb");
	if (fp) { fwrite(&resp[pos], 1, cert_len, fp); fclose(fp); }
	CHECK(cert_pubkey(&resp[pos], cert_len, att_pub), "cert pubkey");
	printf("  attestation cert %u bytes (attestation.der)\n", cert_len);
	pos += cert_len;

	/* アテステーション署名: 00 || app || chal || KH || 04 pub */
	CHECK(der_to_rs(&resp[pos], rlen - 2 - pos, rs, &used), "signature DER");
	pos += used;
	CHECK(pos == rlen - 2, "no trailing bytes (pos=%u rlen=%u)", pos, rlen);
	n = 0;
	msg[n++] = 0x00;
	memcpy(&msg[n], app1, 32); n += 32;
	memcpy(&msg[n], chal, 32); n += 32;
	memcpy(&msg[n], reg_kh, 64); n += 64;
	msg[n++] = 0x04;
	memcpy(&msg[n], reg_pub, 64); n += 64;
	u2f_sha256(msg, n, hash);
	CHECK(uECC_verify(att_pub, hash, 32, rs, uECC_secp256r1()), "attestation signature valid");
}

LOCAL UW test_authenticate(UW cid, UB p1, const UB *app, BOOL with_presence, UH expect_sw,
			   UW prev_counter)
{
	UB req[512], resp[256], data[129], rcmd, rs[64], hash[32], msg[69];
	UW rlen, n, counter = 0, used;

	memcpy(&data[0], chal, 32);
	memcpy(&data[32], app, 32);
	data[64] = 64;
	memcpy(&data[65], reg_kh, 64);
	n = make_apdu(0x02, p1, data, 129, req);

	present = with_presence;
	CHECK(transact(cid, CTAPHID_MSG, req, n, &rcmd, resp, &rlen), "AUTH transact");
	CHECK(sw_of(resp, rlen) == expect_sw, "AUTH p1=%02x SW %04x (expected %04x)",
	      p1, sw_of(resp, rlen), expect_sw);
	if (expect_sw != 0x9000) return prev_counter;

	counter = ((UW)resp[1] << 24) | ((UW)resp[2] << 16) | ((UW)resp[3] << 8) | resp[4];
	CHECK(counter == prev_counter + 1, "counter %u (prev %u)", counter, prev_counter);
	CHECK(resp[0] == ((p1 == 0x03) ? 0x01 : 0x00), "user presence flag %02x", resp[0]);
	CHECK(der_to_rs(&resp[5], rlen - 7, rs, &used) && 5 + used == rlen - 2, "AUTH signature DER");

	memcpy(&msg[0], app, 32);
	memcpy(&msg[32], resp, 5);		/* flags || counter */
	memcpy(&msg[37], chal, 32);
	u2f_sha256(msg, 69, hash);
	CHECK(uECC_verify(reg_pub, hash, 32, rs, uECC_secp256r1()), "AUTH signature valid");
	return counter;
}

LOCAL void test_ctaphid(UW cid)
{
	UB big[300], resp[400], rcmd, pkt[64];
	UW rlen, i;

	printf("[ctaphid]\n");
	for (i = 0; i < sizeof(big); i++) big[i] = (UB)(i * 7);
	CHECK(transact(cid, CTAPHID_PING, big, sizeof(big), &rcmd, resp, &rlen), "PING 300");
	CHECK(rcmd == CTAPHID_PING && rlen == sizeof(big) && memcmp(resp, big, rlen) == 0, "PING echo");

	/* 不正 SEQ: 初期パケットの後に SEQ=1 */
	n_out = 0;
	memset(pkt, 0, sizeof(pkt));
	put_cid(pkt, cid); pkt[4] = 0x80 | CTAPHID_PING; pkt[5] = 0; pkt[6] = 100;
	ctaphid_recv(pkt, now);
	memset(pkt, 0, sizeof(pkt));
	put_cid(pkt, cid); pkt[4] = 1;
	ctaphid_recv(pkt, now);
	CHECK(n_out == 1 && out_pkts[0][4] == (0x80 | CTAPHID_ERROR) && out_pkts[0][7] == CTAPHID_ERR_INVALID_SEQ,
	      "invalid SEQ error");

	/* 別チャネルが組み立て中なら BUSY */
	n_out = 0;
	memset(pkt, 0, sizeof(pkt));
	put_cid(pkt, cid); pkt[4] = 0x80 | CTAPHID_PING; pkt[6] = 100;
	ctaphid_recv(pkt, now);
	memset(pkt, 0, sizeof(pkt));
	put_cid(pkt, cid + 1); pkt[4] = 0x80 | CTAPHID_PING; pkt[6] = 1;
	ctaphid_recv(pkt, now);
	CHECK(n_out == 1 && get_cid(out_pkts[0]) == cid + 1 && out_pkts[0][7] == CTAPHID_ERR_CHANNEL_BUSY,
	      "channel busy");

	/* タイムアウト */
	n_out = 0;
	ctaphid_poll(now + CTAPHID_MSG_TIMEOUT_MS + 1);
	CHECK(n_out == 1 && get_cid(out_pkts[0]) == cid && out_pkts[0][7] == CTAPHID_ERR_MSG_TIMEOUT,
	      "message timeout");

	/* CID 0 と、ブロードキャストでの MSG */
	n_out = 0;
	memset(pkt, 0, sizeof(pkt));
	pkt[4] = 0x80 | CTAPHID_MSG;
	ctaphid_recv(pkt, now);
	CHECK(n_out == 1 && out_pkts[0][7] == CTAPHID_ERR_INVALID_CHANNEL, "cid 0");
	n_out = 0;
	put_cid(pkt, CTAPHID_BROADCAST_CID);
	ctaphid_recv(pkt, now);
	CHECK(n_out == 1 && out_pkts[0][7] == CTAPHID_ERR_INVALID_CHANNEL, "MSG on broadcast");

	/* 未知のコマンド */
	n_out = 0;
	memset(pkt, 0, sizeof(pkt));
	put_cid(pkt, cid); pkt[4] = 0x80 | 0x10;
	ctaphid_recv(pkt, now);
	CHECK(n_out == 1 && out_pkts[0][7] == CTAPHID_ERR_INVALID_CMD, "unknown command");
}

int main(void)
{
	UW cid, counter;
	UB req[16], resp[64], rcmd;
	UW rlen, n, i;

	memset(fake_flash, 0xFF, sizeof(fake_flash));
	u2f_host_random(app1, 32);
	u2f_host_random(app2, 32);
	u2f_host_random(chal, 32);

	printf("[boot] fresh flash\n");
	boot();
	cid = do_init();

	printf("[version]\n");
	n = make_apdu(0x03, 0x00, NULL, 0, req);
	CHECK(transact(cid, CTAPHID_MSG, req, n, &rcmd, resp, &rlen), "VERSION");
	CHECK(rlen == 8 && memcmp(resp, "U2F_V2", 6) == 0 && sw_of(resp, rlen) == 0x9000, "U2F_V2");
	req[1] = 0x55;
	CHECK(transact(cid, CTAPHID_MSG, req, n, &rcmd, resp, &rlen) && sw_of(resp, rlen) == 0x6D00, "unknown INS");

	test_ctaphid(cid);
	test_register(cid);

	printf("[authenticate]\n");
	test_authenticate(cid, 0x07, app1, FALSE, 0x6985, 0);		/* check-only, 自分の KH */
	test_authenticate(cid, 0x07, app2, FALSE, 0x6A80, 0);		/* 他サイト */
	test_authenticate(cid, 0x03, app1, FALSE, 0x6985, 0);		/* 確認なし */
	counter = test_authenticate(cid, 0x03, app1, TRUE, 0x9000, 0);
	counter = test_authenticate(cid, 0x03, app1, TRUE, 0x9000, counter);
	counter = test_authenticate(cid, 0x08, app1, FALSE, 0x9000, counter);
	test_authenticate(cid, 0x03, app2, TRUE, 0x6A80, counter);	/* 他サイト */
	printf("  counter=%u\n", counter);

	printf("[reboot] keys and counter persist\n");
	boot();
	cid = do_init();
	CHECK(u2f_counter_get() == counter, "counter after reboot %u", u2f_counter_get());
	counter = test_authenticate(cid, 0x03, app1, TRUE, 0x9000, counter);

	printf("[counter] sector rollover\n");
	erase_count = 0;
	for (i = 0; i < 1100; i++) {
		UW v;
		if (u2f_counter_next(&v) != E_OK || v != counter + 1) {
			CHECK(0, "counter_next at %u (v=%u counter=%u)", i, v, counter);
			break;
		}
		counter = v;
	}
	printf("  counter=%u, sector erases=%u\n", counter, erase_count);
	CHECK(erase_count >= 2, "both sectors used");
	boot();
	cid = do_init();
	CHECK(u2f_counter_get() == counter, "counter after rollover + reboot %u", u2f_counter_get());

	printf("[corrupt keys] regenerate\n");
	fake_flash[H5_FLASH_U2F_KEYS_OFFSET + 20] ^= 0x01;
	boot();
	cid = do_init();
	/* 鍵が変わったので以前のキーハンドルは使えない */
	test_authenticate(cid, 0x07, app1, FALSE, 0x6A80, counter);

	printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
