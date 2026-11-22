/* libFuzzer entry point: wholly synthetic host-buffer state, no PCI/MMIO. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "sbl_host_model.c"

static void synthetic_completion(struct sbl_host_model *s, uint64_t addr, void *opaque)
{
    (void)addr; (void)opaque;
    const int32_t status = 11;
    memcpy(s->host_buffer + MESSAGE_STATUS_OFFSET, &status, sizeof(status));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 5) return 0;
    /* Fixed backing buffers; only their declared logical extents vary. */
    uint8_t *arena = (uint8_t *)malloc(MESSAGE_OFFSET + MSG_MAX_LEN + 32);
    uint8_t *request = (uint8_t *)malloc(MSG_MAX_LEN + 64);
    if (!arena || !request) abort();
    memset(arena, 0x66, MESSAGE_OFFSET + MSG_MAX_LEN + 32);
    memset(request, 0x55, MSG_MAX_LEN + 64);
    size_t n = ((size_t)data[0] | ((size_t)data[1] << 8)) % (MSG_MAX_LEN + 33);
    size_t logical = ((size_t)data[2] | ((size_t)data[3] << 8)) % (MESSAGE_OFFSET + MSG_MAX_LEN + 1);
    const bool copyback = (data[4] & 1) != 0;
    uint64_t base = (data[4] & 2) ? UINT64_MAX : 0x20000000;
    struct sbl_host_model s = {arena, logical, base, synthetic_completion, NULL};
    const int32_t result = send_msg_model(&s, request, n, copyback);
    bool expected_valid = n >= MSG_MIN_LEN && n <= MSG_MAX_LEN &&
        logical >= MESSAGE_OFFSET && n <= logical - MESSAGE_OFFSET &&
        base <= UINT64_MAX - MESSAGE_OFFSET;
    if (expected_valid && result != 11) abort();
    if (!expected_valid && result != ERR_INVAL) abort();
    /* The synthetic model must never write outside the message region. */
    for (size_t i = 0; i < MESSAGE_OFFSET; ++i)
        if (arena[i] != 0x66) abort();
    for (size_t i = MESSAGE_OFFSET + MSG_MAX_LEN;
         i < MESSAGE_OFFSET + MSG_MAX_LEN + 32; ++i)
        if (arena[i] != 0x66) abort();
    free(request);
    free(arena);
    return 0;
}
