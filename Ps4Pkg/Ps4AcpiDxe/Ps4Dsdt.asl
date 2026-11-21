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
 *   - _S0 and _S5 only (no S3: nothing restores the PS4 from suspend),
 *   - \_SB.PTAD: ACPI Time and Alarm Device (ACPI000E) on the Aeolia/Belize RTC counter. The PS4 has no
 *     PC-AT CMOS RTC; Ps4AcpiDxe says so in the FADT (IAPC_BOOT_ARCH "CMOS RTC not present", revision 6),
 *     and an OS that honours that flag (Windows 8+: HalpSetPlatformFlags, Linux: acpi_tad RTC class device)
 *     takes its wall clock from _GRT/_SRT here instead of I/O 0x70/0x71. Same state as EFI GetTime/SetTime:
 *     the mailbox of Ps4RtcDxe (Ps4Pkg/Include/Guid/Ps4RtcMailbox.h), unix = 2012-01-01 + counter + offset.
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
    Name (RTCB, 0x5A5A5A5A5A5A000B)         // RTC counter registers (00:14.4 BAR2 + 0x130), 0 = none
    Name (RTMB, 0x5A5A5A5A5A5A000C)         // RTC mailbox (Guid/Ps4RtcMailbox.h), 0 = none

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

        Device (PTAD)
        {
            Name (_HID, "ACPI000E")
            Name (_UID, Zero)

            // PS4_RTC_MAILBOX: +0 Signature 'P4RT', +8 Flags, +0x10 Offset (INT64), +0x18 TimeZone, +0x1A Daylight
            OperationRegion (MBOX, SystemMemory, RTMB, 0x20)
            Field (MBOX, AnyAcc, NoLock, Preserve)
            {
                MSIG,   32,
                MVER,   32,
                MFLG,   32,
                Offset (0x10),
                MOFS,   64,
                MTZ,    16,
                MDL,    8
            }

            // counter: four 32-bit registers, one byte each, low byte first
            OperationRegion (CNTR, SystemMemory, RTCB, 0x10)
            Field (CNTR, DWordAcc, NoLock, Preserve)
            {
                CB0,    32,
                CB1,    32,
                CB2,    32,
                CB3,    32
            }

            Method (_STA, 0, Serialized)
            {
                If ((RTMB == Zero) || (RTCB == Zero))
                {
                    Return (Zero)
                }

                If ((MSIG != 0x54523450) || ((MFLG & One) == Zero))
                {
                    Return (Zero)                       // Ps4RtcDxe found no usable counter
                }

                Return (0x0F)
            }

            Method (_GCP, 0, NotSerialized)
            {
                Return (0x04)                           // bit 2: get/set real time; no wake timers
            }

            Method (RAWC, 0, Serialized)
            {
                Return ((CB0 & 0xFF) | ((CB1 & 0xFF) << 0x08) | ((CB2 & 0xFF) << 0x10) | ((CB3 & 0xFF) << 0x18))
            }

            // counter value once two reads 100 us apart agree; Ones when it never settles
            Method (CNOW, 0, Serialized)
            {
                Local0 = RAWC ()
                Local2 = 0x15
                While (Local2)
                {
                    Stall (0x64)
                    Local1 = RAWC ()
                    If (Local1 == Local0)
                    {
                        Return (Local0)
                    }

                    Local0 = Local1
                    Local2--
                }

                Return (Ones)
            }

            Method (_GRT, 0, Serialized)
            {
                Name (TBUF, Buffer (0x10) {})
                CreateWordField (TBUF, 0x00, YEAR)
                CreateByteField (TBUF, 0x02, MNTH)
                CreateByteField (TBUF, 0x03, DAYM)
                CreateByteField (TBUF, 0x04, HOUR)
                CreateByteField (TBUF, 0x05, MINU)
                CreateByteField (TBUF, 0x06, SECO)
                CreateByteField (TBUF, 0x07, VALD)
                CreateWordField (TBUF, 0x08, MSEC)
                CreateWordField (TBUF, 0x0A, TZON)
                CreateByteField (TBUF, 0x0C, DAYL)
                Local0 = CNOW ()
                If (Local0 == Ones)
                {
                    Return (TBUF)                       // Valid = 0
                }

                Local1 = ((0x4EFFA200 + Local0) + MOFS) // unix seconds (2012-01-01 = 1325376000)
                Divide (Local1, 0x00015180, Local3, Local2)  // Local3 = second of day, Local2 = days since 1970
                // days -> civil date (H. Hinnant), all terms non-negative after 1970
                Local2 += 0x000AFA6C                    // + 719468
                Local4 = (Local2 / 0x00023AB1)          // era (146097 days)
                Local5 = (Local2 - (Local4 * 0x00023AB1))
                Local6 = ((((Local5 - (Local5 / 0x05B4)) + (Local5 / 0x8EAC)) - (Local5 / 0x00023AB0)) / 0x016D)
                Local7 = (Local5 - (((Local6 * 0x016D) + (Local6 / 0x04)) - (Local6 / 0x64)))
                Local0 = (((Local7 * 0x05) + 0x02) / 0x99)
                DAYM = ((Local7 - (((Local0 * 0x99) + 0x02) / 0x05)) + One)
                If (Local0 < 0x0A)
                {
                    MNTH = (Local0 + 0x03)
                }
                Else
                {
                    MNTH = (Local0 - 0x09)
                }

                Local1 = (Local6 + (Local4 * 0x0190))
                If (MNTH <= 0x02)
                {
                    Local1++
                }

                YEAR = Local1
                HOUR = (Local3 / 0x0E10)
                MINU = ((Local3 / 0x3C) % 0x3C)
                SECO = (Local3 % 0x3C)
                VALD = One
                MSEC = Zero
                TZON = MTZ
                DAYL = MDL
                Return (TBUF)
            }

            Method (_SRT, 1, Serialized)
            {
                If (SizeOf (Arg0) < 0x10)
                {
                    Return (0xFFFFFFFF)
                }

                CreateWordField (Arg0, 0x00, YEAR)
                CreateByteField (Arg0, 0x02, MNTH)
                CreateByteField (Arg0, 0x03, DAYM)
                CreateByteField (Arg0, 0x04, HOUR)
                CreateByteField (Arg0, 0x05, MINU)
                CreateByteField (Arg0, 0x06, SECO)
                CreateWordField (Arg0, 0x0A, TZON)
                CreateByteField (Arg0, 0x0C, DAYL)
                If ((((YEAR < 0x07B2) || (YEAR > 0x270F)) || ((MNTH < One) || (MNTH > 0x0C))) ||
                    (((DAYM < One) || (DAYM > 0x1F)) || (((HOUR > 0x17) || (MINU > 0x3B)) || (SECO > 0x3B))))
                {
                    Return (0xFFFFFFFF)
                }

                Local0 = CNOW ()
                If (Local0 == Ones)
                {
                    Return (0xFFFFFFFF)
                }

                // civil date -> days since 1970 (H. Hinnant)
                Local1 = YEAR
                If (MNTH <= 0x02)
                {
                    Local1--
                }

                Local2 = (Local1 / 0x0190)              // era
                Local3 = (Local1 - (Local2 * 0x0190))   // year of era
                If (MNTH > 0x02)
                {
                    Local4 = (MNTH - 0x03)
                }
                Else
                {
                    Local4 = (MNTH + 0x09)
                }

                Local5 = (((((Local4 * 0x99) + 0x02) / 0x05) + DAYM) - One)
                Local6 = ((((Local3 * 0x016D) + (Local3 / 0x04)) - (Local3 / 0x64)) + Local5)
                Local7 = (((Local2 * 0x00023AB1) + Local6) - 0x000AFA6C)
                Local7 = ((((Local7 * 0x00015180) + (HOUR * 0x0E10)) + (MINU * 0x3C)) + SECO)
                MOFS = (Local7 - (0x4EFFA200 + Local0))
                MTZ = TZON
                MDL = DAYL
                MFLG |= 0x08                            // PS4_RTC_FLAG_SET
                Return (Zero)
            }
        }
    }
}
