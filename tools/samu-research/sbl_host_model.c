/*
 * Firmware 9.00 SAMU host-side MESSAGE SENDER behavioral model.
 * Clean-room pseudocode based on executable instruction inspection.
 * NOT Sony's original C. NOT a PS4 driver. No hardware I/O.
 * Always offline; this model does not provide a way to access SAMU memory.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HOST_BUFFER_BYTES 0x100000u
#define MESSAGE_OFFSET 0x1000u
#define MESSAGE_STATUS_OFFSET 0x1004u
#define MESSAGE_QWORD_OFFSET 0x1010u
#define MSG_MIN_LEN 24u
#define MSG_MAX_LEN 4096u
#define ERR_INVAL (-22)

struct sbl_host_model;
typedef void (*host_submission_callback)(struct sbl_host_model *state,
                                          uint64_t guest_gpu_addr, void *opaque);
struct sbl_host_model {
    uint8_t *host_buffer;
    size_t host_buffer_bytes;  /* explicit buffer extent in offline test state */
    uint64_t mapped_gpu_base;
    host_submission_callback on_submit;
    void *opaque;
};

/* Mirrors the known externally visible operations, not locks, irq setup,
 * GPU page maps or SAMU acceptance/authorization.  The callback models a
 * synthetic device and must never access actual hardware. */
static int32_t send_msg_model(struct sbl_host_model *state, void *msg,
                              size_t len, bool copyback)
{
    if (len < MSG_MIN_LEN || len > MSG_MAX_LEN)
        return ERR_INVAL;
    if (!state || !msg || !state->host_buffer || !state->on_submit)
        return ERR_INVAL;
    if (state->host_buffer_bytes < MESSAGE_OFFSET ||
        len > state->host_buffer_bytes - MESSAGE_OFFSET ||
        state->mapped_gpu_base > UINT64_MAX - MESSAGE_OFFSET)
        return ERR_INVAL;
    /* Reject overlapping source and mock device storage. This is a safety
     * rule for the test model, not an assertion about Sony's original ABI. */
    const uintptr_t src = (uintptr_t)msg;
    const uintptr_t arena = (uintptr_t)state->host_buffer;
    if (len > UINTPTR_MAX - src || state->host_buffer_bytes > UINTPTR_MAX - arena)
        return ERR_INVAL;
    if (src < arena + state->host_buffer_bytes && arena < src + len)
        return ERR_INVAL;
    memcpy(state->host_buffer + MESSAGE_OFFSET, msg, len);
    memset(state->host_buffer + MESSAGE_QWORD_OFFSET, 0, 8);
    state->on_submit(state, state->mapped_gpu_base + MESSAGE_OFFSET, state->opaque);
    if (copyback)
        memcpy(msg, state->host_buffer + MESSAGE_OFFSET, len);
    int32_t status;
    memcpy(&status, state->host_buffer + MESSAGE_STATUS_OFFSET, sizeof(status));
    return status;
}

/* Sony's 0xffffffff999e4ed0 wrapper passes copyback=0; the
 * 0xffffffff999e4fd0 wrapper passes copyback=1. */
static int32_t send_no_copyback_model(struct sbl_host_model *s, void *msg, size_t n)
{
    return send_msg_model(s, msg, n, false);
}
static int32_t send_with_copyback_model(struct sbl_host_model *s, void *msg, size_t n)
{
    return send_msg_model(s, msg, n, true);
}

/* 0xffffffff999f9060 has a stricter top-level service limit. The actual
 * implementation also uses synchronized slots and retries; this isolated
 * check is not a complete implementation of that function. */
static int32_t service_size_guard_model(size_t size)
{
    return size >= 169u ? ERR_INVAL : 0;
}

#ifdef SBL_OFFLINE_TEST
static int callback_invocations;
static uint64_t last_gpuva;
static void synthetic_device(struct sbl_host_model *s, uint64_t addr, void *opaque)
{
    (void)opaque;
    ++callback_invocations;
    last_gpuva = addr;
    int32_t mock_result = 7;
    memcpy(s->host_buffer + MESSAGE_STATUS_OFFSET, &mock_result, sizeof(mock_result));
    s->host_buffer[MESSAGE_OFFSET + 24] = 0xA5;
}
static void assert_true(bool ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
int main(void)
{
    static uint8_t buffer[HOST_BUFFER_BYTES];
    uint8_t message[32] = {0};
    struct sbl_host_model state = {buffer, sizeof(buffer), 0x40000000, synthetic_device, NULL};
    assert_true(send_msg_model(&state, message, 23, true) == -22,
                "23-byte message rejected");
    assert_true(send_msg_model(&state, message, 4097, false) == -22,
                "oversize message rejected before copying");
    assert_true(callback_invocations == 0, "invalid messages not submitted");
    assert_true(send_no_copyback_model(&state, message, sizeof(message)) == 7,
                "submit and status read");
    assert_true(message[24] == 0, "no-copyback preserved caller buffer");
    assert_true(send_with_copyback_model(&state, message, sizeof(message)) == 7,
                "copyback submit");
    assert_true(message[24] == 0xA5, "copyback updated caller buffer");
    assert_true(last_gpuva == 0x40001000, "mapped GPU address offset");
    assert_true(service_size_guard_model(168) == 0, "168-byte service accepted");
    assert_true(service_size_guard_model(169) == -22, "169-byte service rejected");
    assert_true(send_msg_model(NULL, message, 32, false) == ERR_INVAL,
                "null context rejected");
    assert_true(send_msg_model(&state, NULL, 32, false) == ERR_INVAL,
                "null message rejected");
    assert_true(send_msg_model(&state, state.host_buffer + MESSAGE_OFFSET + 1,
                               32, true) == ERR_INVAL,
                "overlapping synthetic message rejected");
    state.host_buffer_bytes = MESSAGE_OFFSET + 23;
    assert_true(send_msg_model(&state, message, 32, false) == ERR_INVAL,
                "insufficient host buffer rejected");
    state.host_buffer_bytes = sizeof(buffer);
    state.mapped_gpu_base = UINT64_MAX;
    assert_true(send_msg_model(&state, message, 32, false) == ERR_INVAL,
                "GPUVA overflow rejected");
    puts("PASS: 14 offline host-model assertions");
    return 0;
}
#endif
