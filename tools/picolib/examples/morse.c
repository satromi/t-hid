/*
 * morse.c — LED にモールス信号で任意文字列を点滅し、同時に MQTT に通知する
 *
 * "SOS" を GP22 に送出しながら mcp/kb-000001/morse にデコード結果を publish。
 * 短点 (dot)=200ms、長点 (dash)=600ms、文字間ギャップ=400ms、語間ギャップ=800ms。
 *
 * **Pico W + mintlsplit で安全な pin**: GP18-22 のみ自由利用可
 *   GP25=CYW43, GP14-17=KB col, GP2-13=KB row, GP0-1=UART, GP8-9=I2C
 *
 * autoboot に仕込むと「電源投入直後に LED が SOS を刻む」ビジュアルデモに:
 *   python tools/wasm_upload.py morse.wasm --slot 0 --store 0 \
 *                                          --name morse --auto-boot
 */

#include "../picolib.h"

#define LED_PIN  22    /* GP22 — Pico W/mintlsplit で空いている安全な GPIO */
#define DOT_MS   200
#define DASH_MS  600
#define GAP_MS   200    /* element 間 */
#define LGAP_MS  400    /* 文字間 (元の element gap と合わせて 600ms になる) */

/* A-Z 0-9 のモールス表。0=dot, 1=dash。末尾 2 はターミネータ */
static const unsigned char MORSE[36][6] = {
    {0,1,2,0,0,0},        /* A .-    */
    {1,0,0,0,2,0},        /* B -...  */
    {1,0,1,0,2,0},        /* C -.-.  */
    {1,0,0,2,0,0},        /* D -..   */
    {0,2,0,0,0,0},        /* E .     */
    {0,0,1,0,2,0},        /* F ..-.  */
    {1,1,0,2,0,0},        /* G --.   */
    {0,0,0,0,2,0},        /* H ....  */
    {0,0,2,0,0,0},        /* I ..    */
    {0,1,1,1,2,0},        /* J .---  */
    {1,0,1,2,0,0},        /* K -.-   */
    {0,1,0,0,2,0},        /* L .-..  */
    {1,1,2,0,0,0},        /* M --    */
    {1,0,2,0,0,0},        /* N -.    */
    {1,1,1,2,0,0},        /* O ---   */
    {0,1,1,0,2,0},        /* P .--.  */
    {1,1,0,1,2,0},        /* Q --.-  */
    {0,1,0,2,0,0},        /* R .-.   */
    {0,0,0,2,0,0},        /* S ...   */
    {1,2,0,0,0,0},        /* T -     */
    {0,0,1,2,0,0},        /* U ..-   */
    {0,0,0,1,2,0},        /* V ...-  */
    {0,1,1,2,0,0},        /* W .--   */
    {1,0,0,1,2,0},        /* X -..-  */
    {1,0,1,1,2,0},        /* Y -.--  */
    {1,1,0,0,2,0},        /* Z --..  */
    {1,1,1,1,1,2},        /* 0 ----- */
    {0,1,1,1,1,2},        /* 1 .---- */
    {0,0,1,1,1,2},        /* 2 ..--- */
    {0,0,0,1,1,2},        /* 3 ...-- */
    {0,0,0,0,1,2},        /* 4 ....- */
    {0,0,0,0,0,2},        /* 5 ..... */
    {1,0,0,0,0,2},        /* 6 -.... */
    {1,1,0,0,0,2},        /* 7 --... */
    {1,1,1,0,0,2},        /* 8 ---.. */
    {1,1,1,1,0,2},        /* 9 ----. */
};

static void pulse(int ms)
{
    gpio_write(LED_PIN, 1);
    delay_ms((uint32_t)ms);
    gpio_write(LED_PIN, 0);
    delay_ms(GAP_MS);
}

static void send_char(char c)
{
    int idx = -1;
    if (c >= 'A' && c <= 'Z') idx = c - 'A';
    else if (c >= 'a' && c <= 'z') idx = c - 'a';
    else if (c >= '0' && c <= '9') idx = 26 + (c - '0');
    else if (c == ' ') { delay_ms(800); return; }
    else return;                                    /* 非対応文字は無視 */

    const unsigned char *p = MORSE[idx];
    int i;
    for (i = 0; i < 6; i++) {
        if (p[i] == 2) break;
        pulse(p[i] ? DASH_MS : DOT_MS);
    }
    delay_ms(LGAP_MS);
}

__attribute__((export_name("run")))
void run(void)
{
    const char *msg = "SOS";
    LOG("morse: sending SOS");
    mqtt_pub("mcp/kb-000001/morse", 19, (const uint8_t *)msg, 3);

    const char *p = msg;
    while (*p) {
        send_char(*p);
        p++;
    }
    LOG("morse: done");
}
