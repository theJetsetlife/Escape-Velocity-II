/* Record writer — used by both the userland and ring0 halves (same blob, same buffer). */
#ifndef DIAG_REC_H
#define DIAG_REC_H

#include "diag.h"

/* every symbol is local to the blob: no GOT, no PLT, pure RIP-relative code */
#pragma GCC visibility push(hidden)

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

static inline u32 dstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

/* Reserve a record; returns its data pointer (n bytes) or 0 when the buffer is full. */
static inline void *rec_begin(struct diag_ctx *c, u32 tag, u64 a, u64 b, u32 n)
{
    u64 need = sizeof(struct diag_rec) + ((n + 7u) & ~7u);
    if (c->len + need > c->cap) { c->flags |= 1; return 0; }
    struct diag_rec *r = (struct diag_rec *)(c->out + c->len);
    r->tag = tag; r->len = n; r->a = a; r->b = b;
    u8 *d = (u8 *)(void *)(r + 1);
    for (u32 i = n; i < ((n + 7u) & ~7u); i++) d[i] = 0;
    c->len += need;
    c->nrec++;
    return d;
}

static inline void rec(struct diag_ctx *c, u32 tag, u64 a, u64 b, const void *d, u32 n)
{
    u8 *p = (u8 *)rec_begin(c, tag, a, b, n);
    if (p && n) memcpy(p, d, n);
}

static inline void rec_s(struct diag_ctx *c, u32 tag, u64 a, u64 b, const char *s)
{
    rec(c, tag, a, b, s, dstrlen(s));
}

#endif
