/*
 * st7735.h — Pico4ML の液晶 (ST7735S、80x160、縦長) を SPI1 で描く
 */
#ifndef ST7735_H
#define ST7735_H

#include <tk/typedef.h>

#define LCD_W	80
#define LCD_H	160

#define RGB565(r, g, b)	((UH)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define LCD_BLACK	RGB565(0, 0, 0)
#define LCD_WHITE	RGB565(255, 255, 255)
#define LCD_GREEN	RGB565(0, 200, 0)
#define LCD_RED		RGB565(220, 0, 0)
#define LCD_YELLOW	RGB565(230, 200, 0)
#define LCD_GRAY	RGB565(80, 80, 80)
#define LCD_CYAN	RGB565(0, 200, 230)

void st7735_init(void);
void st7735_fill(INT x, INT y, INT w, INT h, UH color);

/* グレースケール画像 (w x h、1 画素 1 バイト) をそのまま描く */
void st7735_gray(INT x, INT y, INT w, INT h, const UB *img);

/* 数字を 7 セグメント風に描く。桁の幅は 6*scale、高さは 11*scale。描いた幅を返す */
INT st7735_number(INT x, INT y, UW value, INT scale, UH color);

#endif /* ST7735_H */
