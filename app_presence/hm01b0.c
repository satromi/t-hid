/*
 * hm01b0.c — Pico4ML のカメラ (Himax HM01B0、モノクロ) を RP2040 の PIO と DMA で読む
 *
 *   配線 (Pico4ML):
 *     GPIO3  MCLK   PIO0 SM1 で CPU クロックの 1/4 (31.25MHz) を出す
 *     GPIO4  SDA    レジスタ設定用の I2C (ソフトウェアで操作)
 *     GPIO5  SCL
 *     GPIO6  D0     画素データ (1 ビットのシリアル出力、LSB から)
 *     GPIO14 PCLK   画素クロック (有効な画素の間だけ出る)
 *     GPIO16 VSYNC  フレームの有効期間
 *
 *   取り込み: PIO0 SM0 が VSYNC の終わりを待ってから、PCLK の立ち上がりごとに D0 を
 *   1 ビットずつ読み、32 ビットたまるごとに RX FIFO へ送る。DMA ch0 がそれを
 *   バッファへ移す。1 フレーム分 (164x162 = 26568 バイト) で DMA が終わる。
 *
 *   画像は 2x2 ビニング (164x162)。1 ビット出力ではセンサ内部のクロックを
 *   MCLK の 1/8 にする必要がある。
 */
#include <tk/tkernel.h>
#include <tk/syslib.h>

#include "hm01b0.h"

/*----------------------------------------------------------------------
 * ピン
 */
#define PIN_MCLK	3
#define PIN_SDA		4
#define PIN_SCL		5
#define PIN_D0		6
#define PIN_PCLK	14
#define PIN_VSYNC	16

#define SENSOR_ADDR	0x24

#define SYS_CLK_HZ	125000000
#define MCLK_HZ		(SYS_CLK_HZ / 4)
#define SENSOR_DIV	8		/* 1 ビット出力のときの内部クロックの分周 */

/*----------------------------------------------------------------------
 * RP2040 のレジスタ
 */
#define REG(a)			(*(volatile UW *)(a))

#define RESETS_RESET_R		0x4000C000
#define RESETS_DONE_R		0x4000C008
#define RST_PIO0		(1u << 10)
#define RST_DMA			(1u << 2)

#define IO_CTRL(n)		(0x40014004 + (n) * 8)
#define PADS(n)			(0x4001C004 + (n) * 4)
#define PAD_IE			(1u << 6)
#define PAD_OD			(1u << 7)
#define FUNC_SIO		5
#define FUNC_PIO0		6

#define SIO_IN			0xD0000004
#define SIO_OUT_SET		0xD0000014
#define SIO_OUT_CLR		0xD0000018
#define SIO_OE_SET		0xD0000024
#define SIO_OE_CLR		0xD0000028

#define PIO0			0x50200000
#define PIO_CTRL		(PIO0 + 0x000)
#define PIO_RXF(sm)		(PIO0 + 0x020 + (sm) * 4)
#define PIO_INSTR_MEM(i)	(PIO0 + 0x048 + (i) * 4)
#define PIO_SM(sm, off)		(PIO0 + 0x0C8 + (sm) * 0x18 + (off))
#define SM_CLKDIV		0x00
#define SM_EXECCTRL		0x04
#define SM_SHIFTCTRL		0x08
#define SM_INSTR		0x10
#define SM_PINCTRL		0x14

#define SM_IMAGE		0
#define SM_CLOCK		1

#define DMA_CH			0
#define DMA_READ_ADDR		(0x50000000 + DMA_CH * 0x40 + 0x00)
#define DMA_WRITE_ADDR		(0x50000000 + DMA_CH * 0x40 + 0x04)
#define DMA_TRANS_COUNT		(0x50000000 + DMA_CH * 0x40 + 0x08)
#define DMA_CTRL_TRIG		(0x50000000 + DMA_CH * 0x40 + 0x0C)
#define DMA_ABORT		(0x50000000 + 0x444)
#define DMA_EN			(1u << 0)
#define DMA_SIZE_WORD		(2u << 2)
#define DMA_INCR_WRITE		(1u << 5)
#define DMA_CHAIN_TO(ch)	((UW)(ch) << 11)
#define DMA_TREQ(n)		((UW)(n) << 15)
#define DMA_BUSY		(1u << 24)
#define DREQ_PIO0_RX0		4

/*----------------------------------------------------------------------
 * PIO のプログラム
 *   0-1  MCLK:  set pins,1 / set pins,0 (2 クロックずつ → 1/4)
 *   2    画像:  wait 0 gpio VSYNC      (今のフレームの終わりまで待つ)
 *   3-5         wait 1 gpio PCLK / in pins,1 / wait 0 gpio PCLK (繰り返し)
 */
#define PROG_CLOCK	0
#define PROG_IMAGE	2
static const UH pio_program[] = {
	0xE001,				/* set pins, 1 */
	0xE000,				/* set pins, 0 */
	0x2000 | PIN_VSYNC,		/* wait 0 gpio VSYNC */
	0x2080 | PIN_PCLK,		/* wait 1 gpio PCLK */
	0x4001,				/* in pins, 1 */
	0x2000 | PIN_PCLK,		/* wait 0 gpio PCLK */
};
#define PIO_SET_PINDIRS_1	0xE081
#define PIO_JMP(addr)		(0x0000 | (addr))

/*----------------------------------------------------------------------
 * I2C (ソフトウェアで操作。書き込みだけ)
 */
LOCAL void delay_us(UW us)
{
	UW n = us * (SYS_CLK_HZ / 1000000 / 3);	/* 1 周 3 クロック */

	__asm__ volatile (
		".syntax unified\n"
		"1: subs %0, #1\n"
		"   bne 1b\n"
		: "+l"(n) : : "cc");
}

LOCAL void pin_put(INT pin, BOOL high)
{
	REG(high ? SIO_OUT_SET : SIO_OUT_CLR) = 1u << pin;
	delay_us(1);
}

LOCAL void i2c_start(void)
{
	pin_put(PIN_SDA, TRUE);
	pin_put(PIN_SCL, TRUE);
	pin_put(PIN_SDA, FALSE);
	pin_put(PIN_SCL, FALSE);
}

LOCAL void i2c_stop(void)
{
	pin_put(PIN_SDA, FALSE);
	pin_put(PIN_SCL, TRUE);
	pin_put(PIN_SDA, TRUE);
}

/* 1 バイト送る。ACK が返れば TRUE */
LOCAL BOOL i2c_write_byte(UB data)
{
	BOOL ack;
	INT i;

	for (i = 0; i < 8; i++) {
		pin_put(PIN_SDA, (data & (0x80 >> i)) != 0);
		pin_put(PIN_SCL, TRUE);
		pin_put(PIN_SCL, FALSE);
	}
	REG(SIO_OE_CLR) = 1u << PIN_SDA;
	delay_us(1);
	pin_put(PIN_SCL, TRUE);
	ack = (REG(SIO_IN) & (1u << PIN_SDA)) == 0;
	pin_put(PIN_SCL, FALSE);
	REG(SIO_OE_SET) = 1u << PIN_SDA;
	return ack;
}

LOCAL BOOL reg_write(UH reg, UB val)
{
	BOOL ok;

	i2c_start();
	ok = i2c_write_byte(SENSOR_ADDR << 1) &&
	     i2c_write_byte((UB)(reg >> 8)) &&
	     i2c_write_byte((UB)reg) &&
	     i2c_write_byte(val);
	i2c_stop();
	return ok;
}

/*----------------------------------------------------------------------
 * センサの設定
 */
typedef struct {
	UH	reg;
	UB	val;
} T_REG;

/* 初期設定 (待機状態。自動露出あり、1 ビットのシリアル出力) */
LOCAL const T_REG init_regs[] = {
	{ 0x0103, 0x00 },	/* ソフトウェアリセット */
	{ 0x0100, 0x00 },	/* 待機 */
	{ 0x0101, 0x03 },	/* 上下左右を反転 (Pico4ML の取り付け向き) */
	{ 0x0350, 0x7F },
	{ 0x1000, 0x43 },	/* 黒レベル補正 */
	{ 0x1001, 0x40 },
	{ 0x1002, 0x32 },
	{ 0x1003, 0x08 },
	{ 0x1006, 0x01 },
	{ 0x1007, 0x08 },
	{ 0x1008, 0x00 },	/* 欠陥画素補正 */
	{ 0x1009, 0xA0 },
	{ 0x100A, 0x60 },
	{ 0x100B, 0x90 },
	{ 0x100C, 0x40 },
	{ 0x2000, 0x05 },	/* 動き検出なし、露出の統計あり */
	{ 0x2100, 0x01 },	/* 自動露出 */
	{ 0x2101, 0x5F },	/*   目標の明るさ */
	{ 0x2102, 0x0A },
	{ 0x2103, 0x03 },
	{ 0x2104, 0x05 },
	{ 0x2107, 0x02 },
	{ 0x2108, 0x03 },
	{ 0x2109, 0x03 },
	{ 0x210A, 0x00 },
	{ 0x210B, 0x80 },
	{ 0x210C, 0x40 },
	{ 0x210D, 0x20 },
	{ 0x210E, 0x00 },	/*   フリッカ補正なし */
	{ 0x210F, 0x00 },
	{ 0x2110, 0x85 },
	{ 0x2111, 0x00 },
	{ 0x2112, 0x70 },
	{ 0x2150, 0x02 },
	{ 0x3011, 0x70 },	/* 8 ビット */
	{ 0x3022, 0x01 },
	{ 0x3044, 0x0A },
	{ 0x3045, 0x00 },
	{ 0x3047, 0x0A },
	{ 0x3050, 0xC0 },
	{ 0x3051, 0x42 },
	{ 0x3052, 0x50 },
	{ 0x3053, 0x00 },
	{ 0x3054, 0x03 },
	{ 0x3055, 0xF7 },
	{ 0x3056, 0xF8 },
	{ 0x3057, 0x29 },
	{ 0x3058, 0x1F },
	{ 0x3059, 0x22 },	/* 1 ビットのシリアル出力 */
	{ 0x3060, 0x20 },	/* 内部クロック = MCLK/8、LSB から、クロックはデータの間だけ */
	{ 0x3062, 0xCC },
	{ 0x3064, 0x00 },
	{ 0x3065, 0x04 },
	{ 0x3067, 0x00 },	/* MCLK を使う */
	{ 0x3068, 0x20 },	/* PCLK の立ち上がりでデータが確定 */
	{ 0x0104, 0x01 },	/* 設定を反映 */
};

/* 設定を順に書く。応答 (ACK) がなかった数を返す */
LOCAL INT write_regs(const T_REG *regs, INT n)
{
	INT i, nak = 0;

	for (i = 0; i < n; i++) {
		if (!reg_write(regs[i].reg, regs[i].val)) nak++;
		if (regs[i].reg == 0x0103) tk_dly_tsk(200);	/* リセットの完了待ち */
	}
	return nak;
}

/*
 * 目的のフレームレートに近くなる 1 行の長さと行数を選ぶ
 *   ビニング時の最小値: 1 行 215 クロック、172 行
 */
LOCAL void frame_timing(INT fps, UW *line_len, UW *line_cnt)
{
	const UW min_ll = 215, min_lc = 172;
	UW cpf = (MCLK_HZ / SENSOR_DIV) / (UW)fps;
	UW max_lc = (cpf + min_ll - 1) / min_ll;
	UW best_diff = 0xFFFFFFFF, best_ll = min_ll, best_lc = min_lc;
	UW lc, ll, diff;

	for (lc = max_lc; lc >= min_lc; lc--) {
		for (ll = cpf / lc; ll <= cpf / lc + 1; ll++) {
			if (ll < min_ll) continue;
			diff = (lc * ll > cpf) ? lc * ll - cpf : cpf - lc * ll;
			if (diff < best_diff) {
				best_diff = diff;
				best_ll = ll;
				best_lc = lc;
			}
		}
	}
	*line_len = best_ll;
	*line_cnt = best_lc;
}

LOCAL void start_streaming(INT fps)
{
	UW ll, lc, maxint;
	INT timeout;

	frame_timing(fps, &ll, &lc);
	maxint = lc - 2;	/* 露光が (行数 - 2) を超えるとフレームが飛ぶ */
	{
		const T_REG regs[] = {
			{ 0x0340, (UB)(lc >> 8) },	/* 1 フレームの行数 */
			{ 0x0341, (UB)lc },
			{ 0x0342, (UB)(ll >> 8) },	/* 1 行の長さ */
			{ 0x0343, (UB)ll },
			{ 0x0383, 0x03 },		/* 横 2 画素ごと */
			{ 0x0387, 0x03 },		/* 縦 2 行ごと */
			{ 0x0390, 0x03 },		/* 2x2 ビニング */
			{ 0x1012, 0x03 },		/* VSYNC をずらす */
			{ 0x3010, 0x00 },		/* QVGA ではない */
			{ 0x2105, (UB)(maxint >> 8) },	/* 露光の上限 */
			{ 0x2106, (UB)maxint },
			{ 0x0104, 0x01 },		/* 設定を反映 */
			{ 0x0100, 0x01 },		/* 撮影開始 */
		};
		write_regs(regs, sizeof(regs) / sizeof(regs[0]));
	}

	/* 最初の VSYNC を待つ */
	for (timeout = 0; timeout < 500; timeout++) {
		if (REG(SIO_IN) & (1u << PIN_VSYNC)) break;
		tk_dly_tsk(1);
	}
}

/*----------------------------------------------------------------------
 * PIO と DMA
 */
LOCAL void unreset(UW bits)
{
	REG(RESETS_RESET_R) &= ~bits;
	while ((REG(RESETS_DONE_R) & bits) != bits) ;
}

LOCAL void pin_func(INT pin, UW func, BOOL input)
{
	REG(PADS(pin)) = (REG(PADS(pin)) & ~PAD_OD) | (input ? PAD_IE : 0);
	REG(IO_CTRL(pin)) = func;
}

LOCAL void pio_setup(void)
{
	UINT i;

	unreset(RST_PIO0 | RST_DMA);
	for (i = 0; i < sizeof(pio_program) / sizeof(pio_program[0]); i++) {
		REG(PIO_INSTR_MEM(i)) = pio_program[i];
	}

	/* MCLK: 2 命令を 2 クロックずつ → CPU クロックの 1/4 */
	REG(PIO_SM(SM_CLOCK, SM_CLKDIV)) = 2u << 16;
	REG(PIO_SM(SM_CLOCK, SM_EXECCTRL)) = ((PROG_CLOCK + 1) << 12) | (PROG_CLOCK << 7);
	REG(PIO_SM(SM_CLOCK, SM_PINCTRL)) = (1u << 26) | (PIN_MCLK << 5);	/* SET 1 本 */
	REG(PIO_SM(SM_CLOCK, SM_INSTR)) = PIO_SET_PINDIRS_1;
	REG(PIO_SM(SM_CLOCK, SM_INSTR)) = PIO_JMP(PROG_CLOCK);
	pin_func(PIN_MCLK, FUNC_PIO0, FALSE);
	REG(PIO_CTRL) |= 1u << SM_CLOCK;

	/* 画像: 右シフトで 32 ビットごとに自動で FIFO へ (RX FIFO は 8 段) */
	REG(PIO_SM(SM_IMAGE, SM_CLKDIV)) = 1u << 16;
	REG(PIO_SM(SM_IMAGE, SM_EXECCTRL)) = ((PROG_IMAGE + 3) << 12) | ((PROG_IMAGE + 1) << 7);
	REG(PIO_SM(SM_IMAGE, SM_SHIFTCTRL)) = (1u << 31) | (1u << 18) | (1u << 16);
	REG(PIO_SM(SM_IMAGE, SM_PINCTRL)) = PIN_D0 << 15;			/* IN の基点 */
	pin_func(PIN_D0, FUNC_SIO, TRUE);
	pin_func(PIN_PCLK, FUNC_SIO, TRUE);
	pin_func(PIN_VSYNC, FUNC_SIO, TRUE);
	REG(SIO_OE_CLR) = (1u << PIN_D0) | (1u << PIN_PCLK) | (1u << PIN_VSYNC);
}

LOCAL void i2c_setup(void)
{
	pin_func(PIN_SDA, FUNC_SIO, TRUE);
	pin_func(PIN_SCL, FUNC_SIO, TRUE);
	REG(SIO_OUT_SET) = (1u << PIN_SDA) | (1u << PIN_SCL);
	REG(SIO_OE_SET) = (1u << PIN_SDA) | (1u << PIN_SCL);
}

EXPORT ER hm01b0_init(INT fps)
{
	pio_setup();
	i2c_setup();
	tk_dly_tsk(50);		/* MCLK を入れてからセンサが動き出すまで */
	if (write_regs(init_regs, sizeof(init_regs) / sizeof(init_regs[0])) > 0) {
		return E_IO;	/* センサが応答しない */
	}
	start_streaming(fps);
	return E_OK;
}

EXPORT ER hm01b0_capture(UB *buf, TMO tmout)
{
	const UW words = HM01B0_COLS * HM01B0_ROWS / 4;
	TMO waited = 0;

	/* 画像の SM を止めて最初からやり直す (FIFO も空にする) */
	REG(PIO_CTRL) &= ~(1u << SM_IMAGE);
	REG(PIO_SM(SM_IMAGE, SM_SHIFTCTRL)) &= ~(1u << 31);
	REG(PIO_SM(SM_IMAGE, SM_SHIFTCTRL)) |= 1u << 31;
	REG(PIO_CTRL) |= (1u << (4 + SM_IMAGE)) | (1u << (8 + SM_IMAGE));
	REG(PIO_SM(SM_IMAGE, SM_INSTR)) = PIO_JMP(PROG_IMAGE);

	REG(DMA_READ_ADDR) = PIO_RXF(SM_IMAGE);
	REG(DMA_WRITE_ADDR) = (UW)buf;
	REG(DMA_TRANS_COUNT) = words;
	REG(DMA_CTRL_TRIG) = DMA_EN | DMA_SIZE_WORD | DMA_INCR_WRITE |
			     DMA_CHAIN_TO(DMA_CH) | DMA_TREQ(DREQ_PIO0_RX0);

	REG(PIO_CTRL) |= 1u << SM_IMAGE;

	while (REG(DMA_CTRL_TRIG) & DMA_BUSY) {
		if (tmout != TMO_FEVR && waited >= tmout) {
			REG(DMA_ABORT) = 1u << DMA_CH;
			while (REG(DMA_ABORT) & (1u << DMA_CH)) ;
			REG(PIO_CTRL) &= ~(1u << SM_IMAGE);
			return E_TMOUT;
		}
		tk_dly_tsk(2);
		waited += 2;
	}
	REG(PIO_CTRL) &= ~(1u << SM_IMAGE);
	__asm__ volatile ("dmb" ::: "memory");
	return E_OK;
}
