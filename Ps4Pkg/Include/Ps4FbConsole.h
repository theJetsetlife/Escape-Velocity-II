/** @file
  Framebuffer debug console descriptor, shared by the directboot stage (writes it at takeover)
  and Ps4FbSerialPortLib (every EDK II module draws DEBUG() text through it).

  It lives at a fixed physical address inside the low 1 MiB that the stage marks reserved, so
  it is valid from the first instruction of the UPL entry (no HOB parsing needed) and the cursor
  is shared by all modules. Keep in sync with src/directboot/hook.c.
**/

#ifndef PS4_FB_CONSOLE_H_
#define PS4_FB_CONSOLE_H_

#define PS4_FBCON_ADDRESS  0x8000
#define PS4_FBCON_MAGIC    0x43463450u      // "P4FC"
#define PS4_FBCON_FIRST_ROW  7              // rows above are the stage's progress squares
#define PS4_FBCON_FLAG_DEBUGCON  0x1       // Reserved bit 0: also copy every byte to I/O port 0xE9.
                                            // Set ONLY by the QEMU harness (rebuild/qemu); the PS4
                                            // stage leaves Reserved = 0, so no stray port I/O there.

typedef struct {
  UINT32    Magic;
  UINT32    Version;
  UINT64    FbBase;          // linear ARGB8888 framebuffer (CPU physical, identity-mapped)
  UINT32    Width;
  UINT32    Height;
  UINT32    PitchBytes;
  UINT32    Col;             // cursor, in 8x19 cells
  UINT32    Row;
  UINT32    Reserved;        // flags: PS4_FBCON_FLAG_*
  // Version >= 2 (stage 2026-11-20): line timestamps = seconds since the stage published this
  UINT64    Tsc0;            // TSC when the stage took over the display
  UINT64    TscHz;           // TSC frequency measured by the stage (0 = no timestamps)
  // Version >= 3: boot-log ring every module appends to (ps4log saves it)
  UINT64    LogBase;         // physical, identity-mapped; PS4_LOG_HEADER then LogSize data bytes
  UINT32    LogSize;         // data bytes (power of two)
  UINT32    LogReserved;
} PS4_FBCON;

#define PS4_LOG_MAGIC  0x474F4C50u        // "PLOG"

typedef struct {
  UINT32    Magic;
  UINT32    Size;            // data bytes (= PS4_FBCON.LogSize)
  UINT64    Written;         // total bytes ever appended; data[Written % Size] is the next byte
} PS4_LOG_HEADER;

#endif
