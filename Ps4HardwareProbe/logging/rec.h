/* rec.h - record buffer shared by the userland and ring0 halves (and the host tests).
 * Ring0-safe: no libc. (libPS4's memcpy/memset are pointers into userland libc; ring0 code must
 * never call them, so this file has its own byte loops and the code avoids struct copies.) */
#ifndef PRB_REC_H
#define PRB_REC_H

#include "../core/probe.h"

static inline void prb_copy(void *d, const void *s, u32 n)
{
    volatile u8 *x = d; const u8 *y = s;
    for (u32 i = 0; i < n; i++) x[i] = y[i];
}
static inline void prb_zero(void *d, u32 n) { volatile u8 *x = d; for (u32 i = 0; i < n; i++) x[i] = 0; }
static inline u32 prb_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static inline void *rec_begin(struct prb_ctx *c, u32 tag, u64 a, u64 b, u32 len)
{
    u32 pad = (len + 7u) & ~7u, need = (u32)sizeof(struct prb_rec) + pad;
    if (c->len + need > c->cap) { c->flags |= 1; return 0; }
    struct prb_rec *r = (struct prb_rec *)(c->out + c->len);
    r->tag = tag; r->len = len; r->a = a; r->b = b;
    u8 *d = (u8 *)(r + 1);
    prb_zero(d + len, pad - len);
    c->len += need; c->nrec++;
    return d;
}

static inline void rec(struct prb_ctx *c, u32 tag, u64 a, u64 b, const void *p, u32 len)
{
    u8 *d = rec_begin(c, tag, a, b, len);
    if (d && len) prb_copy(d, p, len);
}

static inline void rec_s(struct prb_ctx *c, u32 tag, u64 a, u64 b, const char *s)
{
    rec(c, tag, a, b, s, prb_strlen(s) + 1);
}

/* every diagnostic ends with exactly one of these */
static inline void diag(struct prb_ctx *c, u32 id, u32 status, const char *why)
{
    rec_s(c, T_DIAG, id, status, why);
}

#endif
