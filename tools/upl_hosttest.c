/* Host test for src/upl_load.c against the real out/upl-ps4.elf (no hardware, no jump).
 *   cc -O1 -DUPL_HOST_TEST -I../src upl_hosttest.c ../src/upl_load.c -o upl_hosttest
 *   ./upl_hosttest ../out/upl-ps4.elf
 * Loads at a fixed "physical" address inside this process and checks: relocations applied and
 * landing inside the image, FVs placed with an _FVH header, a well-formed HOB list containing
 * PHIT, resource descriptors, ExtraData("uefi_fv"), ACPI, graphics and the PS4 info HOB. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "upl_load.h"

#define REGION 0x200000000ull
#define RSIZE  (1ull << 20)    /* stage region since run 05:56: FVs stay in the ELF copy */

static int fails;
#define CHECK(c, ...) do { if (c) printf("  ok   " __VA_ARGS__); else { printf("  FAIL " __VA_ARGS__); fails++; } printf("\n"); } while (0)

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "../out/upl-ps4.elf", "rb");
    if (!f) { perror("elf"); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *elf = malloc(n);
    if (fread(elf, 1, n, f) != (size_t)n) return 2;
    fclose(f);
    if (mmap((void *)REGION, RSIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != (void *)REGION) {
        perror("mmap region"); return 2;
    }

    static struct upl_inputs in;
    static ps4_directboot_info info = { .version = PS4_DIRECTBOOT_INFO_VERSION, .size = sizeof info,
                                        .unix_time = 1795000000, .tsc_hz = 1593719000 };
    in.elf = elf; in.elf_size = n;
    in.region_phys = REGION; in.region_size = RSIZE;
    in.fb_base = 0x6e008000; in.fb_size = 7680 * 1080; in.fb_w = 1920; in.fb_h = 1080; in.fb_stride = 7680; in.fb_bpp = 32;
    in.rsdp = 0xe0000;
    in.mm[0] = (struct upl_memmap_entry){ 0, 0x100000, UPL_MM_RESERVED };
    in.mm[1] = (struct upl_memmap_entry){ 0x100000, 0x58d00000, UPL_MM_RAM };
    in.mm[2] = (struct upl_memmap_entry){ 0x58e00000, 0x27200000, UPL_MM_RESERVED };
    in.mm[3] = (struct upl_memmap_entry){ 0x100000000ull, 0x30000000, UPL_MM_RESERVED };
    in.mm[4] = (struct upl_memmap_entry){ 0x130000000ull, 0x144000000ull, UPL_MM_RAM };
    in.mm_count = 5;
    in.ps4 = &info;

    const char *err = upl_load(&in);
    printf("upl_load: %s\n", err ? err : "ok (stopped before jump)");
    CHECK(err == 0, "upl_load returned success");
    if (err) return 1;
    CHECK(in.relocs_applied > 0, "%llu R_X86_64_RELATIVE relocations applied", (unsigned long long)in.relocs_applied);
    CHECK(in.test_entry > REGION && in.test_entry < REGION + 0x100000, "entry 0x%llx inside image", (unsigned long long)in.test_entry);
    CHECK(in.test_stack == REGION + RSIZE, "stack top = region top");

    /* walk the HOB list */
    const unsigned char *h = (const unsigned char *)in.test_hobs;
    int phit = 0, res = 0, xd = 0, acpi = 0, gfx = 0, ps4 = 0, end = 0, cpu = 0;
    efi_guid g_xd = GUID_UPL_EXTRA_DATA, g_acpi = GUID_UPL_ACPI_TABLE, g_gfx = GUID_EFI_GRAPHICS_INFO, g_ps4 = GUID_PS4_DIRECTBOOT_INFO, g_rb = GUID_UPL_PCI_ROOT_BRIDGES;
    int rbn = 0;
    for (int i = 0; i < 400; i++) {
        const hob_header *hh = (const hob_header *)h;
        if (hh->length < 8) { printf("  FAIL bad HOB length %u\n", hh->length); fails++; break; }
        if (hh->type == HOB_TYPE_HANDOFF) phit++;
        if (hh->type == HOB_TYPE_RESOURCE_DESCRIPTOR) res++;
        if (hh->type == HOB_TYPE_CPU) {
            cpu++;
            CHECK(h[8] >= 32 && h[8] <= 52 && h[9] == 16, "CPU HOB: %u memory bits, %u I/O bits", h[8], h[9]);
        }
        if (hh->type == HOB_TYPE_GUID_EXTENSION) {
            const hob_guid_type *gh = (const hob_guid_type *)h;
            const void *data = gh + 1;
            if (!memcmp(&gh->name, &g_xd, 16)) {
                const upl_extra_data *x = data;
                xd++;
                CHECK(!memcmp(x->entry[0].identifier, "uefi_fv", 7), "ExtraData entry[0] = uefi_fv");
                const unsigned char *fv = (const unsigned char *)x->entry[0].base;
                CHECK(!memcmp(fv + 0x28, "_FVH", 4), "uefi_fv at 0x%llx has _FVH signature", (unsigned long long)x->entry[0].base);
                CHECK((const unsigned char *)fv >= (const unsigned char *)in.elf && (const unsigned char *)fv + x->entry[0].size <= (const unsigned char *)in.elf + in.elf_size && !(x->entry[0].base & 0xf), "uefi_fv in place in the ELF copy, 16-byte aligned");
                if (x->count > 1) {
                    const unsigned char *bv = (const unsigned char *)x->entry[1].base;
                    CHECK(!memcmp(bv + 0x28, "_FVH", 4), "bds_fv at 0x%llx has _FVH signature", (unsigned long long)x->entry[1].base);
                }
            }
            if (!memcmp(&gh->name, &g_rb, 16)) rbn++;
            if (!memcmp(&gh->name, &g_acpi, 16)) {
                acpi++;
                unsigned long long r; memcpy(&r, (const unsigned char *)data + 4, 8);   /* edk2 pack(1) */
                CHECK(r == 0xe0000, "ACPI HOB Rsdp read at edk2 offset 4 = 0x%llx", r);
            }
            if (!memcmp(&gh->name, &g_gfx, 16)) gfx++;
            efi_guid g_pci = GUID_UPL_PCI_ROOT_BRIDGES;
            if (!memcmp(&gh->name, &g_pci, 16)) {
                const unsigned char *d = data;
                unsigned long long mb, ml; memcpy(&mb, d + 6 + 30 + 48, 8); memcpy(&ml, d + 6 + 30 + 56, 8);
                CHECK(d[4] == 1 && d[5] == 1 && mb == 0x80000000ull && ml == 0xf7ffffffull,
                      "PCI root bridge HOB: assigned, 1 bridge, Mem 0x%llx-0x%llx (edk2 packed offsets)", mb, ml);
            }
            if (!memcmp(&gh->name, &g_ps4, 16)) { ps4++; CHECK(((const ps4_directboot_info *)data)->tsc_hz == 1593719000, "PS4 info HOB payload intact"); }
        }
        if (hh->type == HOB_TYPE_END_OF_HOB_LIST) { end = 1; break; }
        h += hh->length;
    }
    CHECK(phit == 1, "one PHIT HOB");
    CHECK(cpu == 1, "one CPU HOB (required by DxeCore GCD)");
    CHECK(res == (int)in.mm_count, "%d resource descriptors", res);
    CHECK(xd == 1 && acpi == 1 && gfx == 1 && ps4 == 1, "ExtraData/ACPI/graphics/PS4 HOBs present");
    CHECK(rbn == 1, "exactly one PCI root bridges HOB");
    CHECK(end, "END_OF_HOB_LIST reached");
    printf(fails ? "RESULT: %d failure(s)\n" : "RESULT: all checks passed\n", fails);
    return fails != 0;
}
