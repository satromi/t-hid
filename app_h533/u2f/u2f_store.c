/*
 * u2f_store.c — U2F の鍵・証明書・カウンタの Flash 保存
 *
 * カウンタは認証のたびに増えるため、8KB セクタ 2 面を追記ログとして使う。
 * 1 エントリ 16 バイト (Flash の書き込み単位) で、値と反転値を持つ。
 * 有効な最大値が現在のカウンタ。書き込み中のセクタが一杯になったら
 * もう一方を消去してそこへ書く。消去中に電源が落ちても古い面に最大値が
 * 残るので、カウンタが戻ることはない。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "u2f_store.h"
#include "../../device/flash/stm32h5_flash.h"

#define KEYS_MAGIC	0x314B3255UL	/* "U2K1": 秘密は平文 */
#define KEYS_MAGIC_SEALED 0x324B3255UL	/* "U2K2": master と att_priv を SAES で暗号化 */

/* 暗号化する範囲 (master と att_priv は連続した 64 バイト) */
#define SEALED_OFS	offsetof(T_U2F_KEYS, master)
#define SEALED_LEN	(sizeof(((T_U2F_KEYS *)0)->master) + sizeof(((T_U2F_KEYS *)0)->att_priv))
#define CNT_MAGIC	0x31544E43UL	/* "CNT1" */
#define CNT_ENTRIES	(H5_FLASH_SECTOR_SIZE / sizeof(T_CNT_ENTRY))

typedef struct {
	UW	magic;
	UW	value;
	UW	inv;
	UW	unused;
} T_CNT_ENTRY;

LOCAL const UW cnt_sector[2] = { H5_FLASH_U2F_CNT_A_OFFSET, H5_FLASH_U2F_CNT_B_OFFSET };

/*======================================================================
 * 鍵
 *====================================================================*/

EXPORT ER u2f_store_load(T_U2F_KEYS *keys)
{
	UB check[32];

	memcpy(keys, H5_FLASH_PTR(H5_FLASH_U2F_KEYS_OFFSET), sizeof(*keys));
	if (keys->magic == KEYS_MAGIC_SEALED) {
		if (u2f_seal(keys->nonce, (UB *)keys + SEALED_OFS, SEALED_LEN) != E_OK) return E_IO;
	} else if (keys->magic != KEYS_MAGIC) {
		return E_NOEXS;
	}
	if (keys->cert_len == 0 || keys->cert_len > U2F_CERT_MAX) return E_IO;

	u2f_sha256((const UB *)keys, offsetof(T_U2F_KEYS, check), check);
	if (!u2f_equal(check, keys->check, 32)) return E_IO;
	return E_OK;
}

/* SAES が使えれば秘密を暗号化した写しを書き込む。keys 自体は平文のまま */
EXPORT ER u2f_store_save(T_U2F_KEYS *keys)
{
	LOCAL T_U2F_KEYS sealed;
	ER err;

	if (!u2f_crypto_uses_saes()) {
		keys->magic = KEYS_MAGIC;
		memset(keys->nonce, 0xFF, sizeof(keys->nonce));
		u2f_sha256((const UB *)keys, offsetof(T_U2F_KEYS, check), keys->check);
		return h5_flash_write(H5_FLASH_U2F_KEYS_OFFSET, keys, sizeof(*keys));
	}

	keys->magic = KEYS_MAGIC_SEALED;
	err = u2f_random(keys->nonce, sizeof(keys->nonce));
	if (err != E_OK) return err;
	u2f_sha256((const UB *)keys, offsetof(T_U2F_KEYS, check), keys->check);

	sealed = *keys;
	err = u2f_seal(sealed.nonce, (UB *)&sealed + SEALED_OFS, SEALED_LEN);
	if (err == E_OK) err = h5_flash_write(H5_FLASH_U2F_KEYS_OFFSET, &sealed, sizeof(sealed));
	memset(&sealed, 0, sizeof(sealed));
	return err;
}

EXPORT BOOL u2f_store_is_sealed(const T_U2F_KEYS *keys)
{
	return keys->magic == KEYS_MAGIC_SEALED;
}

/*======================================================================
 * 鍵セクタの隠蔽 (HDP)
 *   オプションバイトで鍵セクタを HDP 領域に設定しておくと、隠蔽レベル (HDPL)
 *   を 1 から 2 に上げた後は、再起動するまで CPU もデバッガも読めなくなる。
 *   鍵を RAM に読み込んだ後に呼ぶ。HDP 領域が未設定でも害はない。
 *====================================================================*/

#if !defined(U2F_HOST_TEST)

#define SBS_HDPLCR		(0x44000410UL + U2F_PERIPH_ALIAS)
#define SBS_HDPLSR		(0x44000414UL + U2F_PERIPH_ALIAS)
#define SBS_HDPL_1		0x51u
#define SBS_HDPL_2		0x8Au
#define SBS_HDPL_INCREMENT	0x6Au
#define RCC_APB3ENR_		(0x44020CA8UL + U2F_PERIPH_ALIAS)
#define RCC_APB3ENR_SBSEN	(1u << 1)
#define FLASH_HDP2R_CUR		(0x400221F8UL + U2F_PERIPH_ALIAS)	/* バンク 2 の HDP 領域 (8KB セクタ番号) */
#define KEYS_BANK2_SECTOR	((H5_FLASH_U2F_KEYS_OFFSET - 0x40000u) / H5_FLASH_SECTOR_SIZE)

EXPORT void u2f_store_hide(void)
{
	UW hdp = in_w(FLASH_HDP2R_CUR);
	UW strt = hdp & 0x1Fu, end = (hdp >> 16) & 0x1Fu;
	BOOL covered = (strt <= KEYS_BANK2_SECTOR && KEYS_BANK2_SECTOR <= end);
	UW level;

	out_w(RCC_APB3ENR_, in_w(RCC_APB3ENR_) | RCC_APB3ENR_SBSEN);
	(void)in_w(RCC_APB3ENR_);

	level = in_w(SBS_HDPLSR) & 0xFFu;
	if (level == SBS_HDPL_1) {
		UW i;

		out_w(SBS_HDPLCR, SBS_HDPL_INCREMENT);
		/* 書き込み直後は古い値が読めることがあるので、変わるまで読み直す */
		for (i = 0; i < 1000 && level == SBS_HDPL_1; i++) {
			level = in_w(SBS_HDPLSR) & 0xFFu;
		}
	}
	h5_flash_icache_invalidate();

	if (!covered) {
		tm_printf((UB *)"U2F: key sector not in HDP area (HDPL %02x)\n", level);
	} else {
		tm_printf((UB *)"U2F: key sector hidden by HDP (HDPL %s)\n",
			  (level == SBS_HDPL_2) ? "2" : "not raised");
	}
}

#else

EXPORT void u2f_store_hide(void) {}

#endif /* U2F_HOST_TEST */

/*======================================================================
 * カウンタ
 *====================================================================*/

LOCAL BOOL entry_valid(const T_CNT_ENTRY *e)
{
	return e->magic == CNT_MAGIC && e->inv == ~e->value;
}

/*
 * 面 s の中の最大値と、最初の空き位置 (無ければ CNT_ENTRIES)
 */
LOCAL BOOL scan_sector(INT s, UW *max, UW *free_idx)
{
	const T_CNT_ENTRY *e = (const T_CNT_ENTRY *)H5_FLASH_PTR(cnt_sector[s]);
	BOOL found = FALSE;
	UW i;

	*max = 0;
	*free_idx = CNT_ENTRIES;
	for (i = 0; i < CNT_ENTRIES; i++) {
		if (e[i].magic == 0xFFFFFFFFUL && e[i].value == 0xFFFFFFFFUL &&
		    e[i].inv == 0xFFFFFFFFUL && e[i].unused == 0xFFFFFFFFUL) {
			*free_idx = i;
			break;
		}
		if (entry_valid(&e[i]) && (!found || e[i].value > *max)) {
			*max = e[i].value;
			found = TRUE;
		}
	}
	return found;
}

/* 現在値と、次に書く面 */
LOCAL UW counter_state(INT *active, UW *free_idx)
{
	UW max[2], fr[2];
	BOOL has[2];

	has[0] = scan_sector(0, &max[0], &fr[0]);
	has[1] = scan_sector(1, &max[1], &fr[1]);

	if (has[1] && (!has[0] || max[1] > max[0])) {
		*active = 1; *free_idx = fr[1];
		return max[1];
	}
	*active = 0; *free_idx = fr[0];
	return has[0] ? max[0] : 0;
}

EXPORT UW u2f_counter_get(void)
{
	INT active;
	UW free_idx;
	return counter_state(&active, &free_idx);
}

EXPORT ER u2f_counter_next(UW *value)
{
	T_CNT_ENTRY e;
	INT active;
	UW free_idx, cur, off;
	ER err;

	cur = counter_state(&active, &free_idx);
	if (cur == 0xFFFFFFFEUL) return E_LIMIT;

	e.magic  = CNT_MAGIC;
	e.value  = cur + 1;
	e.inv    = ~e.value;
	e.unused = 0;

	if (free_idx >= CNT_ENTRIES) {
		/* 一杯: もう一方の面を消して先頭に書く */
		active ^= 1;
		err = h5_flash_erase(cnt_sector[active], H5_FLASH_SECTOR_SIZE);
		if (err != E_OK) return err;
		free_idx = 0;
	}
	off = cnt_sector[active] + free_idx * sizeof(T_CNT_ENTRY);
	err = h5_flash_program(off, &e, sizeof(e));
	if (err != E_OK) return err;

	*value = e.value;
	return E_OK;
}

EXPORT ER u2f_store_erase_all(void)
{
	ER err = h5_flash_erase(H5_FLASH_U2F_KEYS_OFFSET, H5_FLASH_SECTOR_SIZE);
	if (err == E_OK) err = h5_flash_erase(H5_FLASH_U2F_CNT_A_OFFSET, H5_FLASH_SECTOR_SIZE);
	if (err == E_OK) err = h5_flash_erase(H5_FLASH_U2F_CNT_B_OFFSET, H5_FLASH_SECTOR_SIZE);
	return err;
}
