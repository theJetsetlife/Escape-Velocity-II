/* acpi.c - RSDP (sysctl machdep.acpi_root, then the 0xE0000 scan), XSDT/RSDT, every table + DSDT +
 * FACS, with checksums. Remembers the HPET base for timers/. RAM reads only, each range page-walked. */
#include "../diag.h"

static int sum_ok(const u8 *p, u32 n) { u8 s = 0; for (u32 i = 0; i < n; i++) s += p[i]; return s == 0; }

static void table(struct prb_ctx *c, u64 pa, u32 depth, u32 *bad)
{
    if (!prb_ram_ok(c, pa, 36)) { rec_s(c, T_NOTE, pa, 0, "acpi table not mapped"); (*bad)++; return; }
    const u8 *h = hal_phys(c, pa, 36);
    u32 len = *(const u32 *)(h + 4);
    if (len < 36 || len > 0x40000 || !prb_ram_ok(c, pa, len)) { rec_s(c, T_NOTE, pa, len, "acpi table length bad"); (*bad)++; return; }
    h = hal_phys(c, pa, len);
    int ok = sum_ok(h, len);
    if (!ok) (*bad)++;
    u8 meta[8]; prb_copy(meta, h, 4); prb_copy(meta + 4, &len, 4);
    rec(c, T_ACPI, pa, (u64)ok, meta, 8);
    rec(c, T_PHYS, pa, 4, h, len);
    if (depth) return;
    if (prb_memeq(h, "FACP", 4)) {
        u64 dsdt = *(const u32 *)(h + 40);
        if (len >= 148 && *(const u64 *)(h + 140)) dsdt = *(const u64 *)(h + 140);
        if (dsdt) table(c, dsdt, 1, bad);
        u64 facs = *(const u32 *)(h + 36);
        if (facs && prb_ram_ok(c, facs, 64)) rec(c, T_PHYS, facs, 4, hal_phys(c, facs, 64), 64);
    }
    if (prb_memeq(h, "HPET", 4) && len >= 56) c->hpet = *(const u64 *)(h + 44);   /* GAS address */
}

void dx_acpi(struct prb_ctx *c)
{
    if (!c->dmap) { diag(c, D_ACPI, ST_UNSUPPORTED, "no proven direct map"); return; }
    u64 rsdp = 0;
    if (c->acpi_root && prb_ram_ok(c, c->acpi_root, 20) && prb_memeq(hal_phys(c, c->acpi_root, 20), "RSD PTR ", 8))
        rsdp = c->acpi_root;
    for (u64 p = 0xe0000; !rsdp && p < 0x100000; p += 16)
        if (prb_ram_ok(c, p, 20) && prb_memeq(hal_phys(c, p, 20), "RSD PTR ", 8) && sum_ok(hal_phys(c, p, 20), 20))
            rsdp = p;
    if (!rsdp) { diag(c, D_ACPI, ST_FAILED, "no RSDP"); return; }
    const u8 *r = hal_phys(c, rsdp, 20);
    u32 rlen = (r[15] >= 2 && prb_ram_ok(c, rsdp, 36)) ? 36 : 20;
    r = hal_phys(c, rsdp, rlen);
    rec(c, T_PHYS, rsdp, 3, r, rlen);
    u64 root = *(const u32 *)(r + 16); u32 esz = 4;
    if (rlen == 36 && *(const u64 *)(r + 24)) { root = *(const u64 *)(r + 24); esz = 8; }
    u32 bad = 0;
    table(c, root, 1, &bad);
    if (!prb_ram_ok(c, root, 36)) { diag(c, D_ACPI, ST_FAILED, "root table not mapped"); return; }
    const u8 *t = hal_phys(c, root, 36);
    u32 len = *(const u32 *)(t + 4);
    if (len < 36 || len > 0x1000 || !prb_ram_ok(c, root, len)) { diag(c, D_ACPI, ST_FAILED, "root table bad"); return; }
    t = hal_phys(c, root, len);
    for (u32 o = 36; o + esz <= len; o += esz) {
        u64 e = esz == 8 ? *(const u64 *)(t + o) : *(const u32 *)(t + o);
        if (e) table(c, e, 0, &bad);
    }
    diag(c, D_ACPI, bad ? ST_FAILED : ST_OK, bad ? "some tables unreadable or with bad checksums" : "all tables read, checksums valid");
}
