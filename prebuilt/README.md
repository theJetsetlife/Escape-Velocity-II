# prebuilt binaries

Stages are built for PC 10.0.0.71:9031; retarget: `python3 tools/diag_recv.py patch <stage.bin> --ip A.B.C.D --port N` (changes 6 bytes; the sha256 then differs).

| Folder | Contents | Status |
|---|---|---|
| `hw-verified/` | stage + UEFI that booted to the UEFI Shell on a PS4 Fat 9.00 | **hardware-verified** |
| `io-build/` | same stage + UEFI with USB/SATA/SD/UART/DS4/Ethernet | QEMU only — next hardware run |
| `rebuild2-current/` | stage + UEFI of this tree when it was handed over (6 GiB, NTFS, ps4log, Ps4AcpiDxe incl. untested RAM-IOAPIC ID 0) | QEMU only |

```
ff86e20d802e035b706bcea8fd21f6688f3b16dae10173bf61ba8d529b815e68  hw-verified/directboot-ff86e20d.bin
5c32a84d5613ae7b551418322f8d0f7ed10ad21db42516286b0a7f984b6bccb7  hw-verified/upl-ps4-5c32a84d.elf
ff86e20d802e035b706bcea8fd21f6688f3b16dae10173bf61ba8d529b815e68  io-build/directboot-ff86e20d.bin
d7a705d25e994a3ddf7b35495bd12d124daa93804cab3d9fb4d52de905f23593  io-build/upl-ps4-d7a705d2.elf
abd4c03c1c23af157407b882dcd877b13c5d0a78b1004a1e825f0939ea5265ec  rebuild2-current/directboot-abd4c03c.bin
6675c172d55f4ff8817e12f2c256f07c1cc7c9ed9054ec10118a1e532c9cf472  rebuild2-current/upl-ps4-6675c172.elf
```
