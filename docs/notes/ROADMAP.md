# **PS4 Directboot — Ultimate Engineering Roadmap**

Native UEFI development

**Final objective:** Build a complete, reliable, hardware-functional UEFI firmware for the original PlayStation 4 Fat (Aeolia, firmware 9.00), using a native Directboot chain with EDK II.

The product must be capable of running a generic EFI Shell, enumerating real hardware, using USB keyboards and storage, executing UEFI applications, and providing stable firmware services.

The EFI Shell is our test environment. **A usable UEFI platform is the finished product.**

## **1. Final architecture**

PS4 Fat — OrbisOS 9.00

Sony secure boot completes normally

WebKitty — PSFree + Lapse

Browser exploit, kernel patches and payload delivery

PS4 Payload SDK Bootstrap

Validate entry, syscall 11 callback and image staging

Native Directboot Bootstrap

Load firmware, validate shared-memory contract and prepare takeover

PS4 Hardware Ownership Layer

CPU/SMP, memory, DMA, Aeolia, interrupts, GPU

Custom PS4 EDK II Platform

PS4-specific platform services and device drivers

Fully Functional UEFI

Boot services, runtime services, EFI applications and hardware

## **2. Non-negotiable design decisions**

1. **RAM-resident firmware:** Do not overwrite Sony's permanent firmware.
2. **Native execution:** No intermediate operating system or emulator.
3. **Shared memory and Aeolia glue are foundational:** Their address mappings and ownership requirements must be understood before finalizing physical staging.
4. **Hardware ownership precedes unrestricted device initialization:** The new firmware must not inherit unsafe DMA or interrupt activity.
5. **Standard UEFI interfaces:** Extend EDK II with PS4-specific drivers rather than replacing established UEFI infrastructure.
6. **Real hardware testing:** No component is declared working based solely on compilation, simulation, or source inspection.

## **3. The critical discovery: Aeolia glue and shared memory**

The supplied PS4 Linux 7.3.3 source establishes two distinct southbridge mechanisms that the firmware must account for.

PS4 address and memory relationships

**AMD Jaguar CPU and memory controller**

**System GDDR5**

CPU/GPU shared physical memory, firmware allocations, framebuffer and reserved regions

**PCI / Aeolia glue**

Address windows, device registers, MSI routing and southbridge memory access

Firmware staging RAM

Must be independently allocated and reserved

Southbridge SPM

Device-shared registers, ICC request/reply buffers

### **Confirmed source facts**

| Source                          | Observed behavior                                                                            |
| ------------------------------- | -------------------------------------------------------------------------------------------- |
| `drivers/ps4/sb-aeolia.c:49–53` | Writes function-specific glue BAR masks and base registers                                   |
| `sb-aeolia.c:504`               | Programs Aeolia memory function BAR2 with base `0xC0000000` and mask `0x3FFFFFFF`            |
| `drivers/ps4/ps4sb.h:245–251`   | Defines southbridge SPM offsets, including ICC at `0x2C000` and boot parameters at `0x2F000` |
| `drivers/ps4/sb-icc.c:580–614`  | Resolves memory function BAR5, reserves the ICC SPM range and maps it through `ioremap()`    |

These operations are related, but the sources do not prove that the `0xC0000000` window is general-purpose RAM or that all Aeolia memory windows have the same backing.

**Our firmware must distinguish three things:** ordinary shared GDDR5, reserved Directboot handoff RAM, and southbridge-exposed shared memory. The physical addresses and device-visible mappings must be documented before enabling hardware.

## **4. Complete development phases**

**00**

Freeze architecture and establish evidence

Source review

Define the exact PS4 Fat hardware target, firmware 9.00 offsets, source revisions, image formats and known hardware behavior.

Deliverable: Authoritative source manifest, hardware register ledger, reproducible build environment.

Exit criterion: Every source and binary is hashed; unverified assumptions are labeled.

**01**

Verify payload staging

Source review

Trace WebKitty's selected launcher through the SDK entrypoint, executable mappings, payload size, relocation expectations and callback ABI.

Deliverable: Minimal diagnostic payload, staging format specification, execution trace and failure codes.

Exit criterion: Real PS4 confirms delivery, actual execution and privileged callback results independently.

**02**

Build the memory and glue-address contract

Next

Inventory RAM, GDDR5 carveouts, glue PCI resources, SPM, high MMIO BARs, framebuffer, IOMMU mappings and current address translations.

Deliverable: Physical address map, memory ownership ledger and read-only register inventory.

Exit criterion: No unexplained overlaps, unverified RAM claims or unsupported physical-to-device-address assumptions.

**03**

Implement the firmware staging bridge

Pending

Validate image headers and bounds, allocate owned physical RAM, place firmware and handoff data, set up integrity checks and preserve allocations.

Deliverable: Native stage manager, memory descriptor structure, relocation validator and persistent handoff block.

Exit criterion: Image and handoff survive a controlled test transition without corruption.

**04**

Native hardware takeover

Pending

Coordinate eight Jaguar CPU cores, establish bootstrap ownership, manage GDT/IDT/CR3/stack and device DMA and interrupt state.

Deliverable: PS4 CPU transition layer and explicit hardware-ownership state machine.

Exit criterion: Each transition invariant is observed on hardware before firmware entry.

**05**

Aeolia and AMD-Vi platform control

Pending

Implement glue window inventory, interrupt-source masking, MSI controller initialization, IOMMU lifecycle and protected southbridge memory handling.

Deliverable: PS4 glue, southbridge, interrupt and DMA modules.

Exit criterion: Interrupts and DMA are enabled only with known destinations and validated ownership.

**06**

Native EDK II platform initialization

Pending

Choose a coherent PS4 EDK II entry architecture and establish memory descriptors, platform data, CPU state, firmware volumes and service dependencies.

Deliverable: PS4 EDK II platform package, build configuration, ACPI/resource descriptions and firmware image.

Exit criterion: EDK II reliably initializes without invalid memory mappings or service dependencies.

**07**

Display and interactive firmware

Pending

Implement framebuffer-backed GOP, UEFI console protocols, timers, USB xHCI and keyboard input.

Deliverable: Visible interactive EFI Shell and input/display drivers.

Exit criterion: Shell is responsive and remains stable across repeated boots.

**08**

PCI, USB storage and bootable media

Pending

Implement safe Sony PCI enumeration, BAR handling, USB mass storage, EFI Block I/O, disk partitions, and FAT Simple File System.

Deliverable: PCI bridge drivers and USB storage stack.

Exit criterion: USB drives are listed, files read correctly, and EFI applications launch from removable media.

**09**

Expand real hardware services

Pending

Support RTC/time, reset, power management, Ethernet where feasible, AHCI/SD and other inventoried controllers with explicit ownership policies.

Deliverable: Device-specific DXE modules, power and runtime services.

Exit criterion: Each supported controller passes independent functional and recovery tests.

**10**

UEFI compliance and reliability

Pending

Exercise memory allocation, events, timers, protocols, image loading, driver binding, boot management, runtime calls and ExitBootServices where applicable.

Deliverable: Automated EFI tests, hardware regression suite and compatibility report.

Exit criterion: Firmware passes a defined functional test matrix without untracked faults.

**11**

Release and maintenance

Pending

Create repeatable builds, signed or hashed release manifests, startup diagnostics, documentation and hardware recovery procedures.

Deliverable: Versioned RAM-resident UEFI distribution and reproducible engineering repository.

Exit criterion: Release criteria meet documented reliability, usability and hardware-safety targets.

**Dependency rule:** The sequence is a set of verification gates, not a requirement to develop everything strictly serially. We can develop EDK II drivers and unit tests on the workstation while hardware staging and takeover are being validated. But we should not activate a hardware-dependent feature before its prerequisites pass.

## **5. Shared-memory implementation contract**

This is one of the most important design documents to finish before writing hardware-transition assembly.

**Proposed memory ownership lifecycle**

1\. OrbisOS-owned

Firmware payload and initial buffers allocated under the existing OS

2\. Reserved for handoff

Physical backing validated; images, tables and stack protected

3\. Ownership transfer pending

CPU synchronization, DMA retirement and page-table validation

4\. EDK II-owned

Firmware controls the allocations and describes them accurately to UEFI memory services

### **Required handoff information**

| Field                            | Why it matters                                             |
| -------------------------------- | ---------------------------------------------------------- |
| Physical base and region size    | Locate and bound the actual memory allocation              |
| CPU virtual address              | Locate the region before page-table changes                |
| Device-visible DMA address       | Distinguish IOMMU translations from CPU physical addresses |
| Memory type and cache attributes | Prevent inappropriate RAM/MMIO caching                     |
| Owner and lifetime               | Prevent premature reuse or release                         |
| Executable image and entrypoint  | Validate firmware placement                                |
| ACPI and framebuffer descriptors | Preserve firmware-visible platform resources               |
| Page tables and stack            | Protect execution during the transition                    |
| Integrity metadata               | Detect incompatible or corrupted handoff structures        |

The handoff layout should be versioned, validated before use, and checked again at firmware entry.

**Important:** Aeolia SPM must not be treated as a general staging buffer. The source shows device-specific shared-memory structures for southbridge communication. The new firmware should reserve ordinary physical RAM for its executable image and use southbridge memory only through a validated device protocol.

## **6. UEFI feature acceptance matrix**

A UEFI Shell that appears but cannot use real hardware is not an acceptable final release.

| Subsystem                  | Acceptance requirement                                               | Priority |
| -------------------------- | -------------------------------------------------------------------- | -------- |
| Firmware initialization    | Repeated native starts without unexpected hangs                      | Required |
| GOP and display            | Stable framebuffer output, console text and supported graphics modes | Required |
| Keyboard                   | Reliable input, including repeated typing and key events             | Required |
| PCI                        | Valid devices and resource windows without destructive probing       | Required |
| USB xHCI                   | Controller initialization and device discovery                       | Required |
| USB storage                | Block I/O, FAT filesystem access and safe reads/writes               | Required |
| EFI applications           | LoadImage/StartImage and clean application exit                      | Required |
| Memory services            | Valid memory maps, allocations and resource protection               | Required |
| Interrupts and timers      | Functional timer events and deterministic MSI handling               | Required |
| ACPI and platform tables   | Valid checksums, topology and resource descriptions                  | Required |
| Reset and runtime services | Supported operations behave as documented                            | Required |
| Ethernet, AHCI, SD, audio  | Individual functional tests and documented support status            | Target   |

These are proposed acceptance requirements. They do not describe functionality already established by the native Directboot implementation.

For a release candidate, I would require at least 20 consecutive successful cold-start test cycles, 30 minutes of uninterrupted interactive operation per stability test, repeated USB media transfers with verified file hashes, and no unexplained firmware memory-map changes or storage corruption.

## **7. Current source readiness**

| Component                | Current evidence                                  | Next action                             |
| ------------------------ | ------------------------------------------------- | --------------------------------------- |
| WebKitty                 | Source available                                  | Verify launch ABI and staging           |
| PSFree + Lapse           | Source available                                  | Verify firmware 9.00 runtime behavior   |
| PS4 Payload SDK          | Source available; library build previously passed | Validate on real hardware               |
| Aeolia glue              | Relevant Linux reference driver available         | Produce address/ownership specification |
| Southbridge SPM          | ICC-related source available                      | Document BAR5 layout and safe access    |
| AMD-Vi / MSI             | Driver references available                       | Design native interrupt/DMA ownership   |
| Native Directboot bridge | Not demonstrated                                  | Implement and test                      |
| Native EDK II platform   | Not demonstrated                                  | Build PS4 platform package              |
| Functional UEFI release  | Not demonstrated                                  | Complete hardware acceptance testing    |

## **8. What we should work on first**

## **First engineering work package**

**PS4 Native Memory, Glue and Staging Contract — Version 1**

Four connected deliverables, before hardware takeover

`Ps4MemoryMap`

Enumerate and classify physical RAM, GDDR5 reservations, MMIO, firmware allocations and framebuffer memory.

`Ps4AeoliaGlue`

Identify glue registers, actual BAR assignments, SPM regions, address-window behavior and read-only diagnostic facilities.

`Ps4FirmwareStaging`

Validate the EDK II image and reserve suitable physical memory without relying on assumed addresses.

`Ps4HandoffContract`

Define CPU entry state, physical mappings, image metadata, allocation lifetime and transfer of ownership.

The first hardware deliverable should be a **read-only diagnostic payload** that reports real addresses and resource state from firmware 9.00. We should compare that evidence against the supplied source before choosing any fixed physical layout or programming glue registers.

The direct staging and takeover work can then proceed against measured hardware properties rather than guesses.

## **Final development principle**

The project has three distinct completion levels:

| Level             | Definition                                                                                    |
| ----------------- | --------------------------------------------------------------------------------------------- |
| **Bootable**      | EDK II starts and an EFI Shell appears                                                        |
| **Functional**    | Console, keyboard, USB storage, PCI, memory and essential firmware protocols work             |
| **Release-ready** | Functionality is repeatable, stable, documented, and regression-tested on actual PS4 hardware |

**Our target is release-ready native PS4 UEFI.**

The most important immediate dependency is not another exploit or another firmware binary. It is establishing a correct, hardware-verified relationship between **Aeolia glue addressing, southbridge shared memory, physical GDDR5 ownership and firmware staging**. Once that contract is complete, the remaining native Directboot and EDK II development has a defensible foundation.
