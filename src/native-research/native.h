#ifndef EVII_NATIVE_RESEARCH_H
#define EVII_NATIVE_RESEARCH_H

#include <stdint.h>

/* This module is a generic loader + diagnostic scaffold, not a SAMU exploit. */
#define RESEARCH_STAGE_UNIMPLEMENTED 0x7ffe

struct config_block {
    char magic[8];
    uint8_t ip[4];
    uint8_t port[2];
    uint16_t reserved;
};

extern struct config_block scaffold_cfg __attribute__((visibility("hidden")));
int scaffold_main(void);

/* User extension: returns RESEARCH_STAGE_UNIMPLEMENTED until implemented. */
int research_stage_run(void);

/* Non-secret diagnostic notes, max 128 bytes each; may return negative on I/O error. */
int research_stage_note(const char *message);

#endif
