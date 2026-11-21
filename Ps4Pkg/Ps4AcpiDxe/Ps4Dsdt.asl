/*
 * Ps4Dsdt.asl - DSDT that Ps4AcpiDxe installs in place of Sony's on the PS4.
 *
 * Sony's DSDT (9.00, "SIE ORBIS") describes PCI0 as PNP0A03 with _CRS = only I/O 0xCF8: no bus range,
 * no I/O or memory windows, so an OS cannot place any BAR in the root bridge (Windows: ACPI_BIOS_ERROR 0x2,
 * reproduced in QEMU 2026-11-20 by an overlap of the same class). This DSDT gives:
 *   - \_PR.C000-C007 Processor objects (Sony's AMD POWERNOW SSDT references them by these names),
 *   - \_SB.PCI0: PNP0A08/PNP0A03, _CBA = ECAM, _CRS = bus 0..BUSM, I/O 0-0xCF7 + 0xD00-0xFFFF,
 *     memory [M1MN..M1MX] (top of low RAM .. ECAM-1) + [M2MN..M2MX] (ECAM end .. 0xFEBFFFFF)
 *     + optional 64-bit [M64N..M64X] (above TOM2),
 *   - \_SB.MBRS: PNP0C02 reserving the ECAM range,
 *   - _S0 and _S5 only (no S3: nothing restores the PS4 from suspend).
 * No _PRT: the PS4 has no IOAPIC, devices use MSI; a route to a GSI nobody serves is worse than none.
 *
 * The Name()s below are placeholders: Ps4AcpiDxe finds each DWord/QWord constant in the compiled AML and
 * overwrites it with the value measured at boot. Regenerate Ps4Dsdt.h with tools/mkdsdt.sh after edits.
 */
DefinitionBlock ("", "DSDT", 2, "PS4FW ", "PS4DSDT ", 0x00000001)
{
    Name (ECMB, 0x5A5A0001)                 // ECAM base
    Name (ECML, 0x5A5A0002)                 // ECAM length
    Name (BUSM, 0x5A5A0003)                 // last bus number
    Name (M1MN, 0x5A5A0004)                 // 32-bit window 1
    Name (M1MX, 0x5A5A0005)
    Name (M2MN, 0x5A5A0006)                 // 32-bit window 2
    Name (M2MX, 0x5A5A0007)
    Name (M64E, 0x5A5A000A)                 // 64-bit window enabled
    Name (M64N, 0x5A5A5A5A5A5A0008)         // 64-bit window
    Name (M64X, 0x5A5A5A5A5A5A0009)

    Scope (_PR)
    {
        Processor (C000, 0x00, 0x00000000, 0x00) {}
        Processor (C001, 0x01, 0x00000000, 0x00) {}
        Processor (C002, 0x02, 0x00000000, 0x00) {}
        Processor (C003, 0x03, 0x00000000, 0x00) {}
        Processor (C004, 0x04, 0x00000000, 0x00) {}
        Processor (C005, 0x05, 0x00000000, 0x00) {}
        Processor (C006, 0x06, 0x00000000, 0x00) {}
        Processor (C007, 0x07, 0x00000000, 0x00) {}
    }

    Name (_S0, Package (0x04) { Zero, Zero, Zero, Zero })
    Name (_S5, Package (0x04) { 0x05, 0x05, Zero, Zero })

    Name (PICM, Zero)
    Method (_PIC, 1, NotSerialized)
    {
        PICM = Arg0
    }

    Scope (_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_UID, Zero)
            Name (_SEG, Zero)
            Name (_BBN, Zero)
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }

            Method (_CBA, 0, NotSerialized)
            {
                Return (ECMB)
            }

            Name (RBUF, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0x0000, 0x0000, 0x00FF, 0x0000, 0x0100,,, BUS0)
                IO (Decode16, 0x0CF8, 0x0CF8, 0x01, 0x08)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0000, 0x0CF7, 0x0000, 0x0CF8,,, , TypeStatic, DenseTranslation)
                WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
                    0x0000, 0x0D00, 0xFFFF, 0x0000, 0xF300,,, , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x00000000, 0x80000000, 0x8FFFFFFF, 0x00000000, 0x10000000,,, MW1, AddressRangeMemory, TypeStatic)
                DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x00000000, 0xFC000000, 0xFCFFFFFF, 0x00000000, 0x01000000,,, MW2, AddressRangeMemory, TypeStatic)
            })
            Name (R64, ResourceTemplate ()
            {
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
                    0x0000000000000000, 0x0000000400000000, 0x00000004FFFFFFFF, 0x0000000000000000,
                    0x0000000100000000,,, MW3, AddressRangeMemory, TypeStatic)
            })

            Method (_CRS, 0, Serialized)
            {
                CreateWordField (RBUF, \_SB.PCI0.BUS0._MAX, BMAX)
                CreateWordField (RBUF, \_SB.PCI0.BUS0._LEN, BLEN)
                BMAX = BUSM
                BLEN = (BUSM + One)
                CreateDWordField (RBUF, \_SB.PCI0.MW1._MIN, A1MN)
                CreateDWordField (RBUF, \_SB.PCI0.MW1._MAX, A1MX)
                CreateDWordField (RBUF, \_SB.PCI0.MW1._LEN, A1LN)
                A1MN = M1MN
                A1MX = M1MX
                A1LN = ((M1MX - M1MN) + One)
                CreateDWordField (RBUF, \_SB.PCI0.MW2._MIN, A2MN)
                CreateDWordField (RBUF, \_SB.PCI0.MW2._MAX, A2MX)
                CreateDWordField (RBUF, \_SB.PCI0.MW2._LEN, A2LN)
                A2MN = M2MN
                A2MX = M2MX
                A2LN = ((M2MX - M2MN) + One)
                If (M64E)
                {
                    CreateQWordField (R64, \_SB.PCI0.MW3._MIN, A3MN)
                    CreateQWordField (R64, \_SB.PCI0.MW3._MAX, A3MX)
                    CreateQWordField (R64, \_SB.PCI0.MW3._LEN, A3LN)
                    A3MN = M64N
                    A3MX = M64X
                    A3LN = ((M64X - M64N) + One)
                    Return (ConcatenateResTemplate (RBUF, R64))
                }

                Return (RBUF)
            }
        }

        Device (MBRS)
        {
            Name (_HID, EisaId ("PNP0C02"))
            Name (_UID, One)
            Name (MBUF, ResourceTemplate ()
            {
                Memory32Fixed (ReadOnly, 0xF8000000, 0x04000000, ECM0)
            })
            Method (_CRS, 0, Serialized)
            {
                CreateDWordField (MBUF, \_SB.MBRS.ECM0._BAS, EBAS)
                CreateDWordField (MBUF, \_SB.MBRS.ECM0._LEN, ELEN)
                EBAS = ECMB
                ELEN = ECML
                Return (MBUF)
            }
        }
    }
}
