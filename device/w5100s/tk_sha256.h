/*
 *----------------------------------------------------------------------
 *    Minimal SHA-256 + HMAC-SHA256 for μT-Kernel (Cortex-M0+)
 *    Public domain implementation, no external dependencies.
 *----------------------------------------------------------------------
 */

#ifndef __TK_SHA256_H__
#define __TK_SHA256_H__

#include <tk/tkernel.h>

typedef struct {
    UW state[8];
    UW count[2];
    UB buf[64];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const UB *data, UW len);
void sha256_final(SHA256_CTX *ctx, UB digest[32]);

/* HMAC-SHA256: key + message → 32-byte digest */
void hmac_sha256(const UB *key, UW key_len,
                 const UB *msg, UW msg_len,
                 UB digest[32]);

/* digest を hex 文字列に変換 (64 chars + NUL, buf は 65 bytes 以上) */
void sha256_to_hex(const UB digest[32], char *hex);

#endif /* __TK_SHA256_H__ */
