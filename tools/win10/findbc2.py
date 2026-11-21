import sys, mmap, struct, collections
f = open(sys.argv[1], 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
codes = {0x5C:'HAL_INITIALIZATION_FAILED',0x5D:'UNSUPPORTED_PROCESSOR',0x78:'PHASE0_EXCEPTION',0x79:'MISMATCHED_HAL',
         0xA5:'ACPI_BIOS_ERROR',0x7D:'INSTALL_MORE_MEMORY',0x7B:'INACCESSIBLE_BOOT_DEVICE',0x101:'CLOCK_WATCHDOG_TIMEOUT',
         0x7F:'UNEXPECTED_KERNEL_MODE_TRAP',0x1E:'KMODE_EXCEPTION',0x3B:'SYSTEM_SERVICE_EXCEPTION',0x50:'PAGE_FAULT',
         0xD1:'DRIVER_IRQL',0x124:'WHEA',0x133:'DPC_WATCHDOG',0x139:'KERNEL_SECURITY_CHECK',0x31:'PHASE0_INITIALIZATION_FAILED',
         0x32:'PHASE1_INITIALIZATION_FAILED',0x9C:'MACHINE_CHECK',0xC4:'DRIVER_VERIFIER',0x1D3:'?',0x6B:'PROCESS1_INITIALIZATION_FAILED'}
for code, name in codes.items():
    pat = struct.pack('<Q', code); hits = collections.Counter(); i = m.find(pat)
    while i != -1:
        if i % 8 == 0 and i + 40 <= len(m):
            p = struct.unpack_from('<5Q', m, i)
            if any(p[1:]) and all(x < 0xffffffffffffffff for x in p[1:]) and (p[1] >> 48) in (0, 0xffff) and (p[2] >> 48) in (0, 0xffff):
                hits[p[1:]] += 1
        i = m.find(pat, i + 1)
    for k, n in hits.most_common(3):
        if n >= 2: print(n, hex(code), name, ' '.join(hex(x) for x in k))
