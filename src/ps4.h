/* Clean-room PS4 payload types + primitives. Written from our RE; see ../SOURCES.md.
 * No libPS4/freebsd-headers are linked in — only referenced for convention. */
#ifndef PS4_H
#define PS4_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed   long long s64;
typedef unsigned long      size_t;

/* Verification-first: nothing here is trusted from the book or memory.
 * Each claim is tagged with the PRIMARY artifact it was re-derived from in-session.
 *
 *   #11  = kexec(fn, uap)  -> runs fn in ring0 via a sysent[11] gadget.
 *          [VERIFIED] gadget bytes at kbase+0x4c7ad (0xffffffff894687ad) in the retail
 *          9.00 dump read as FF 26 = `jmp qword ptr [rsi]`; kbase=0xffffffff8941c000.
 *          [VERIFIED] loader Linux_Loader_1G_VRAM.bin has `mov rax,0xb; syscall` thunk.
 *   #153 = kexec_load(...) -> our own sys_kexec, installed at runtime by kernel_main.
 *          [VERIFIED] loader has the `mov r10,rcx; mov rax,153(0x99); syscall` thunk @0x4d2ca.
 *   [UNVERIFIED-HERE] exact arg->register marshalling below is per the SysV->syscall ABI
 *   and the recovered thunks; re-confirm against the running payload before relying on it. */
long kexec(void *fn, void *uap);                                   /* syscall 11  */
long kexec_load(void *kernel, u64 kernel_size,
                void *initrd, u64 initrd_size,
                const char *cmdline, int packed_args);             /* syscall 153 */

#endif
