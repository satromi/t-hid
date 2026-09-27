/*
 * st7735.c — Pico4ML の液晶 (ST7735S、80x160、縦長) を SPI1 で描く
 *
 *   配線 (Pico4ML): GPIO10 SCK / GPIO11 MOSI (SPI1)、GPIO13 CS、GPIO9 DC、GPIO7 RST
 *   80x160 のパネルは ST7735 の 132x162 の表示メモリの列 24 から始まる。
 */
#include <tk/tkernel.h>
#include <tk/syslib.h>

#include "st7735.h"

#define PIN_RST		7
#define PIN_DC		9
#define PIN_SCK		10
#define PIN_MOSI	11
#define PIN_CS		13

#define COL_OFFSET	24
#define ROW_OFFSET	0

#define REG(a)		(*(volatile UW *)(a))

#define RESETS_RESET_R	0x4000C000
#define RESETS_DONE_R	0x4000C008
#define RST_SPI1	(1u << 17)
#define IO_CTRL(n)	(0x40014004 + (n) * 8)
#define FUNC_SPI	1
#define FUNC_SIO	5
#define SIO_OUT_SET	0xD0000014
#define SIO_OUT_CLR	0xD0000018
#define SIO_OE_SET	0xD0000024

#define SPI1		0x40040000
#define SSPCR0		(SPI1 + 0x00)
#define SSPCR1		(SPI1 + 0x04)
#define SSPDR		(SPI1 + 0x08)
#define SSPSR		(SPI1 + 0x0C)
#define SSPCPSR		(SPI1 + 0x10)
#define SR_TNF		(1u << 1)
#define SR_BSY		(1u << 4)

/* ST7735 のコマンド */
#define SWRESET		0x01
#define SLPOUT		0x11
#define NORON		0x13
#define INVOFF		0x20
#define DISPON		0x29
#define CASET		0x2A
#define RASET		0x2B
#define RAMWR		0x2C
#define MADCTL		0x36
#define COLMOD		0x3A
#define MADCTL_BGR	0x08

LOCAL void pin(INT n, BOOL high)
{
	REG(high ? SIO_OUT_SET : SIO_OUT_CLR) = 1u << n;
}

LOCAL void spi_wait_idle(void)
{
	while (REG(SSPSR) & SR_BSY) ;
}

LOCAL void spi_write(const UB *buf, INT len)
{
	while (len-- > 0) {
		while (!(REG(SSPSR) & SR_TNF)) ;
		REG(SSPDR) = *buf++;
	}
	spi_wait_idle();
}

LOCAL void cmd(UB c, const UB *data, INT len)
{
	pin(PIN_CS, FALSE);
	pin(PIN_DC, FALSE);
	spi_write(&c, 1);
	if (len > 0) {
		pin(PIN_DC, TRUE);
		spi_write(data, len);
	}
	pin(PIN_CS, TRUE);
}

LOCAL void window(INT x, INT y, INT w, INT h)
{
	UB ca[4], ra[4];
	INT x0 = x + COL_OFFSET, x1 = x + w - 1 + COL_OFFSET;
	INT y0 = y + ROW_OFFSET, y1 = y + h - 1 + ROW_OFFSET;

	ca[0] = (UB)(x0 >> 8); ca[1] = (UB)x0; ca[2] = (UB)(x1 >> 8); ca[3] = (UB)x1;
	ra[0] = (UB)(y0 >> 8); ra[1] = (UB)y0; ra[2] = (UB)(y1 >> 8); ra[3] = (UB)y1;
	cmd(CASET, ca, 4);
	cmd(RASET, ra, 4);
}

EXPORT void st7735_init(void)
{
	static const UB frmctr[] = { 0x01, 0x2C, 0x2D };
	static const UB frmctr3[] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D };
	static const UB invctr[] = { 0x07 };
	static const UB pwctr1[] = { 0xA2, 0x02, 0x84 };
	static const UB pwctr2[] = { 0xC5 };
	static const UB pwctr3[] = { 0x0A, 0x00 };
	static const UB pwctr4[] = { 0x8A, 0x2A };
	static const UB pwctr5[] = { 0x8A, 0xEE };
	static const UB vmctr1[] = { 0x0E };
	static const UB madctl[] = { MADCTL_BGR };
	static const UB colmod[] = { 0x05 };	/* 16 ビット/画素 */
	static const UB gamma_p[] = { 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
				      0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10 };
	static const UB gamma_n[] = { 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
				      0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10 };

	REG(RESETS_RESET_R) &= ~RST_SPI1;
	while (!(REG(RESETS_DONE_R) & RST_SPI1)) ;

	/* SPI1: 8 ビット、モード 0、125MHz / (2 * 5) = 12.5MHz */
	REG(SSPCPSR) = 2;
	REG(SSPCR0) = (4u << 8) | 7;
	REG(SSPCR1) = 1u << 1;		/* 有効 */

	REG(IO_CTRL(PIN_SCK)) = FUNC_SPI;
	REG(IO_CTRL(PIN_MOSI)) = FUNC_SPI;
	REG(IO_CTRL(PIN_CS)) = FUNC_SIO;
	REG(IO_CTRL(PIN_DC)) = FUNC_SIO;
	REG(IO_CTRL(PIN_RST)) = FUNC_SIO;
	pin(PIN_CS, TRUE);
	pin(PIN_DC, TRUE);
	REG(SIO_OE_SET) = (1u << PIN_CS) | (1u << PIN_DC) | (1u << PIN_RST);

	pin(PIN_RST, FALSE);
	tk_dly_tsk(5);
	pin(PIN_RST, TRUE);
	tk_dly_tsk(120);

	cmd(SWRESET, NULL, 0);
	tk_dly_tsk(150);
	cmd(SLPOUT, NULL, 0);
	tk_dly_tsk(255);
	cmd(0xB1, frmctr, 3);
	cmd(0xB2, frmctr, 3);
	cmd(0xB3, frmctr3, 6);
	cmd(0xB4, invctr, 1);
	cmd(0xC0, pwctr1, 3);
	cmd(0xC1, pwctr2, 1);
	cmd(0xC2, pwctr3, 2);
	cmd(0xC3, pwctr4, 2);
	cmd(0xC4, pwctr5, 2);
	cmd(0xC5, vmctr1, 1);
	cmd(INVOFF, NULL, 0);
	cmd(MADCTL, madctl, 1);
	cmd(COLMOD, colmod, 1);
	cmd(0xE0, gamma_p, sizeof(gamma_p));
	cmd(0xE1, gamma_n, sizeof(gamma_n));
	cmd(NORON, NULL, 0);
	tk_dly_tsk(10);
	cmd(DISPON, NULL, 0);
	tk_dly_tsk(100);

	st7735_fill(0, 0, LCD_W, LCD_H, LCD_BLACK);
}

/* 表示メモリへの書き込みを始める (CS を下げたまま戻る) */
LOCAL void begin_write(INT x, INT y, INT w, INT h)
{
	UB c = RAMWR;

	window(x, y, w, h);
	pin(PIN_CS, FALSE);
	pin(PIN_DC, FALSE);
	spi_write(&c, 1);
	pin(PIN_DC, TRUE);
}

LOCAL void end_write(void)
{
	spi_wait_idle();
	pin(PIN_CS, TRUE);
}

EXPORT void st7735_fill(INT x, INT y, INT w, INT h, UH color)
{
	INT n = w * h;

	if (w <= 0 || h <= 0) return;
	begin_write(x, y, w, h);
	while (n-- > 0) {
		while (!(REG(SSPSR) & SR_TNF)) ;
		REG(SSPDR) = color >> 8;
		while (!(REG(SSPSR) & SR_TNF)) ;
		REG(SSPDR) = color & 0xFF;
	}
	end_write();
}

EXPORT void st7735_gray(INT x, INT y, INT w, INT h, const UB *img)
{
	INT n = w * h;
	UH c;

	begin_write(x, y, w, h);
	while (n-- > 0) {
		UB v = *img++;
		c = RGB565(v, v, v);
		while (!(REG(SSPSR) & SR_TNF)) ;
		REG(SSPDR) = c >> 8;
		while (!(REG(SSPSR) & SR_TNF)) ;
		REG(SSPDR) = c & 0xFF;
	}
	end_write();
}

/*
 * 7 セグメント: a (上) b (右上) c (右下) d (下) e (左下) f (左上) g (中央)
 */
LOCAL const UB seg_of_digit[10] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

LOCAL void draw_digit(INT x, INT y, INT d, INT s, UH color)
{
	UB m = seg_of_digit[d];
	INT w = 5 * s, t = s, h = 5 * s;	/* 横棒の長さ、太さ、縦棒の長さ */

	st7735_fill(x, y, w, 11 * s, LCD_BLACK);
	if (m & 0x01) st7735_fill(x, y, w, t, color);
	if (m & 0x02) st7735_fill(x + w - t, y, t, h + t, color);
	if (m & 0x04) st7735_fill(x + w - t, y + h, t, h + t, color);
	if (m & 0x08) st7735_fill(x, y + 2 * h, w, t, color);
	if (m & 0x10) st7735_fill(x, y + h, t, h + t, color);
	if (m & 0x20) st7735_fill(x, y, t, h + t, color);
	if (m & 0x40) st7735_fill(x, y + h, w, t, color);
}

EXPORT INT st7735_number(INT x, INT y, UW value, INT scale, UH color)
{
	UB digits[10];
	INT n = 0, i;

	do {
		digits[n++] = (UB)(value % 10);
		value /= 10;
	} while (value > 0 && n < 10);
	for (i = n - 1; i >= 0; i--) {
		draw_digit(x, y, digits[i], scale, color);
		x += 6 * scale;
	}
	return n * 6 * scale;
}
