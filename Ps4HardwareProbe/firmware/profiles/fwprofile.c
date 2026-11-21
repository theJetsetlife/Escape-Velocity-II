/* fwprofile.c - firmware offset profiles = rows of src/directboot/fwtable.h (via fwsel.c).
 * Adding a version = adding a verified row there (tools/mkfwtable.py); the probe core is unchanged. */
#include "../fw.h"
#include "fwsel.h"

/* Point releases whose whole OpenGoldHEN import table equals a row's (docs/FIRMWARE_VERSIONS.md):
 * same kernel layout, so they use that row. The live-kernel check still decides. */
static const u16 alias[13][2] = {
    { 671, 672 }, { 801, 800 }, { 803, 800 }, { 852, 850 }, { 904, 903 }, { 950, 960 }, { 951, 960 },
    { 1001, 1000 }, { 1070, 1050 }, { 1071, 1050 }, { 1152, 1150 }, { 1202, 1200 }, { 1252, 1250 },
};

u16 fw_row_base(u16 ver)
{
    for (u32 i = 0; i < 13; i++) if (alias[i][0] == ver) return alias[i][1];
    return ver;
}

const void *fw_row_for(u16 ver)
{
    u16 base = fw_row_base(ver);
    for (u32 i = 0; i < db_fw_rows(); i++)
        if (db_fw_row(i)->ver == base) return db_fw_row(i);
    return 0;
}

int fw_row_verified(const void *row) { return (((const struct db_kofs *)row)->flags & DB_FW_VERIFIED) != 0; }
u16 fw_row_ver(const void *row) { return ((const struct db_kofs *)row)->ver; }
