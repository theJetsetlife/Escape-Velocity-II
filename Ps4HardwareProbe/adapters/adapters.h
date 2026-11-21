/* adapters.h - payload-loading environments. The payload cannot test for one safely (calling an
 * unimplemented syscall raises SIGSYS and kills the host process), so the operator declares it in the
 * config block (probe_recv.py patch --env) and an undeclared environment makes the payload return at once.
 * Every adapter provides raw `syscall` from payload memory, and a kexec syscall whose NUMBER depends on
 * the loader and the system software version: fn(td, uap) with uap[0] = fn, uap[1] = arg through a
 * `jmp [rsi]` sysent entry. Versions with no known kexec get no ring0 (D_KEXEC SKIPPED). */
#ifndef PRB_ADAPTERS_H
#define PRB_ADAPTERS_H

#include "../core/probe.h"

#define PRB_ADAPTER_MAXVER 40

struct prb_adapter {
    char name[16];
    u8   env;                        /* enum prb_env */
    u8   state;                      /* enum prb_vstate */
    u16  loader_port;                /* BinLoader TCP port (send once, never probe it) */
    u16  kexec_any;                  /* kexec syscall on any version (0 = only the listed ones) */
    u16  nver;
    u16  ver[PRB_ADAPTER_MAXVER];    /* versions the loader supports (its own sources) */
    u16  kexec_nr[PRB_ADAPTER_MAXVER];
};

const struct prb_adapter *adapter_for(u32 env);
/* kexec syscall number for this loader on version `ver` (0 = unknown version), 0 = none known */
u32 adapter_kexec_nr(const struct prb_adapter *a, u16 ver);

#endif
