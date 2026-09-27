/*
 *----------------------------------------------------------------------
 *    Minimal SHA-256 + HMAC-SHA256 (FIPS 180-4 / RFC 2104)
 *    Pure C, no libc dependency, Cortex-M0+ safe.
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <string.h>
#include "tk_sha256.h"

/* SHA-256 constants */
LOCAL const UW K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z) (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROR(x,2)^ROR(x,13)^ROR(x,22))
#define EP1(x) (ROR(x,6)^ROR(x,11)^ROR(x,25))
#define SIG0(x) (ROR(x,7)^ROR(x,18)^((x)>>3))
#define SIG1(x) (ROR(x,17)^ROR(x,19)^((x)>>10))

LOCAL void sha256_transform(SHA256_CTX *ctx, const UB data[64])
{
    UW a,b,c,d,e,f,g,h,t1,t2,w[64];
    INT i;

    for (i = 0; i < 16; i++)
        w[i] = ((UW)data[i*4]<<24)|((UW)data[i*4+1]<<16)|
               ((UW)data[i*4+2]<<8)|data[i*4+3];
    for (i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + w[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }

    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

EXPORT void sha256_init(SHA256_CTX *ctx)
{
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
    ctx->count[0]=ctx->count[1]=0;
}

EXPORT void sha256_update(SHA256_CTX *ctx, const UB *data, UW len)
{
    UW i, idx, space;
    idx = (ctx->count[0] >> 3) & 0x3F;
    ctx->count[0] += len << 3;
    if (ctx->count[0] < (len << 3)) ctx->count[1]++;
    ctx->count[1] += len >> 29;
    space = 64 - idx;
    if (len >= space) {
        memcpy(ctx->buf + idx, data, space);
        sha256_transform(ctx, ctx->buf);
        for (i = space; i + 63 < len; i += 64)
            sha256_transform(ctx, data + i);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(ctx->buf + idx, data + i, len - i);
}

EXPORT void sha256_final(SHA256_CTX *ctx, UB digest[32])
{
    UB pad[64];
    UW idx, plen;
    INT i;

    memset(pad, 0, 64);
    pad[0] = 0x80;

    idx = (ctx->count[0] >> 3) & 0x3F;
    plen = (idx < 56) ? (56 - idx) : (120 - idx);
    sha256_update(ctx, pad, plen);

    /* Append length in bits (big-endian) */
    UB bits[8];
    for (i = 0; i < 4; i++) {
        bits[i]   = (UB)(ctx->count[1] >> (24 - i*8));
        bits[i+4] = (UB)(ctx->count[0] >> (24 - i*8));
    }
    sha256_update(ctx, bits, 8);

    for (i = 0; i < 8; i++) {
        digest[i*4]   = (UB)(ctx->state[i] >> 24);
        digest[i*4+1] = (UB)(ctx->state[i] >> 16);
        digest[i*4+2] = (UB)(ctx->state[i] >> 8);
        digest[i*4+3] = (UB)(ctx->state[i]);
    }
}

/*----------------------------------------------------------------------
 * HMAC-SHA256 (RFC 2104)
 */
EXPORT void hmac_sha256(const UB *key, UW key_len,
                        const UB *msg, UW msg_len,
                        UB digest[32])
{
    SHA256_CTX ctx;
    UB k_pad[64];
    UB tk[32];
    UW i;

    /* Key longer than block size → hash it */
    if (key_len > 64) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, tk);
        key = tk;
        key_len = 32;
    }

    /* Inner padding */
    memset(k_pad, 0x36, 64);
    for (i = 0; i < key_len; i++) k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, msg, msg_len);
    sha256_final(&ctx, digest);

    /* Outer padding */
    memset(k_pad, 0x5C, 64);
    for (i = 0; i < key_len; i++) k_pad[i] ^= key[i];

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, digest, 32);
    sha256_final(&ctx, digest);
}

/*----------------------------------------------------------------------
 * digest (32 bytes) → hex 文字列 (64 chars + NUL)
 */
EXPORT void sha256_to_hex(const UB digest[32], char *hex)
{
    static const char hx[] = "0123456789abcdef";
    INT i;
    for (i = 0; i < 32; i++) {
        hex[i*2]   = hx[digest[i] >> 4];
        hex[i*2+1] = hx[digest[i] & 0xF];
    }
    hex[64] = '\0';
}

#endif /* CPU_RP2040 */
