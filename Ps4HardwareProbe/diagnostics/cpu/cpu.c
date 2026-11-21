/* cpu.c - CPUID (bounded by the reported maxima), topology, control registers, MSRs.
 * Model-specific MSRs only on family 16h (Jaguar, every PS4 CPU): an unimplemented MSR raises #GP
 * in ring0, so any other CPU gets architectural registers only. */
#include "../diag.h"

void dx_cpu(struct prb_ctx *c)
{
    u32 r[4];
    hal_cpuid(0, 0, r);
    u32 maxstd = r[0];
    hal_cpuid(0x80000000, 0, r);
    u32 maxext = r[0];
    static const u32 std[3] = { 0, 1, 7 };
    static const u32 ext[5] = { 0x80000000, 0x80000001, 0x80000005, 0x80000006, 0x80000008 };
    for (u32 i = 0; i < 3; i++) if (std[i] <= maxstd) { hal_cpuid(std[i], 0, r); rec(c, T_CPUID, std[i], 0, r, 16); }
    for (u32 i = 0; i < 5; i++)
        if (maxext >= 0x80000000u && ext[i] <= maxext) { hal_cpuid(ext[i], 0, r); rec(c, T_CPUID, ext[i], 0, r, 16); }
    hal_cpuid(1, 0, r);
    c->id.cpuid1_eax = r[0];
    u32 base = (r[0] >> 8) & 0xf, fam = base + (base == 0xf ? ((r[0] >> 20) & 0xff) : 0);
    c->fam16h = fam == 0x16;
    diag(c, D_CPU_ID, ST_OK, c->fam16h ? "AMD family 16h (Jaguar)" : "not family 16h: MSR/MMIO diagnostics limited");

    u32 apic = r[1] >> 24, logical = (r[1] >> 16) & 0xff, cores = 0;
    if (maxext >= 0x80000008u) { hal_cpuid(0x80000008, 0, r); cores = (r[2] & 0xff) + 1; }
    rec_s(c, T_NOTE, apic, (u64)logical | (u64)cores << 32, "topology: apic_id | logical, cores<<32");
    diag(c, D_CPU_TOPOLOGY, ST_OK, "this CPU from CPUID; every APIC id from the ACPI MADT");

    rec(c, T_CREG, 0, hal_rdcr(0), 0, 0);
    rec(c, T_CREG, 3, hal_rdcr(3), 0, 0);
    rec(c, T_CREG, 4, hal_rdcr(4), 0, 0);
    rec(c, T_CREG, 0x80, hal_rdmsr(0xc0000080), 0, 0);
    rec(c, T_CREG, 0x81, hal_rflags(), 0, 0);
    diag(c, D_CPU_CREGS, ST_OK, "CR0 CR3 CR4 EFER RFLAGS");

    if (!c->fam16h) { diag(c, D_CPU_MSR, ST_SKIPPED_SAFETY, "MSR allowlist is family-16h specific"); return; }
    static const u32 msrs[23] = {
        0x1b, 0xfe, 0x2ff, 0x250, 0x258, 0x259, 0x268, 0x269, 0x26a, 0x26b, 0x26c, 0x26d, 0x26e, 0x26f,
        0x277, 0xc0000081, 0xc0000082, 0xc0010010, 0xc0010015, 0xc001001a, 0xc001001d, 0xc0010058,
        0xc0010114,
    };
    for (u32 i = 0; i < 23; i++) rec(c, T_MSR, msrs[i], hal_rdmsr(msrs[i]), 0, 0);
    diag(c, D_CPU_MSR, ST_OK, "family 16h allowlist");
}
