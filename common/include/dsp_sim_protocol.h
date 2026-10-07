/*
 * Copyright (c) 2026 Hailo Technologies Ltd. All rights reserved.
 *
 * Wire format between a host process and DSP firmware running inside the
 * Xtensa instruction set simulator.
 *
 * The record is a serialisation of what the XRP kernel driver places in shared
 * memory for the DSP: a namespace id, the in/out data blobs and a list of
 * buffers with their access flags. The firmware side rebuilds a
 * "struct xrp_dsp_cmd" from it and hands that to the unchanged XRP dispatch
 * path, so the firmware cannot tell a simulated command from a real one.
 *
 * Both sides are little-endian 32-bit-field readers. The DSP is 32-bit, so
 * every size and count is uint32_t.
 */

#ifndef _DSP_SIM_PROTOCOL_H
#define _DSP_SIM_PROTOCOL_H

#include <stdint.h>

/* Namespace id length, mirroring XRP_DSP_CMD_NAMESPACE_ID_SIZE. Repeated here
   so host code can include this header without the firmware's XRP headers. */
#define DSP_SIM_NSID_SIZE (16)

/* Upper bound on buffers in one command. Both sides size fixed storage from
   this, so it has to cover the worst command the public API documents:
   dsp_blend() takes up to 50 overlays, and an A420 overlay is four planes, so
   200 overlay planes plus the background's. The kernel driver has no such
   limit; this one exists only because the firmware cannot allocate. */
#define DSP_SIM_MAX_BUFFERS (256)

#define DSP_SIM_MAGIC_READY (0x59445221u) /* "!RDY" - firmware is up */
#define DSP_SIM_MAGIC_REQUEST (0x51455251u)
#define DSP_SIM_MAGIC_RESPONSE (0x50535251u)

/* Buffer access flags, matching XRP_DSP_BUFFER_FLAG_READ / _WRITE. */
#define DSP_SIM_BUFFER_READ (0x1u)
#define DSP_SIM_BUFFER_WRITE (0x2u)

/* Response status placed in cmd_flags, matching XRP_DSP_CMD_FLAG_*. */
#define DSP_SIM_CMD_FLAG_RESPONSE_VALID (0x00000002u)
#define DSP_SIM_CMD_FLAG_DELIVERY_FAIL (0x00000008u)

typedef struct {
    uint32_t magic;   /* DSP_SIM_MAGIC_READY */
    uint32_t version; /* DSP_SIM_PROTOCOL_VERSION */
    uint32_t arena_size;
} dsp_sim_ready_t;

#define DSP_SIM_PROTOCOL_VERSION (4)

typedef struct {
    uint32_t magic; /* DSP_SIM_MAGIC_REQUEST */
    uint32_t has_nsid;
    uint8_t nsid[DSP_SIM_NSID_SIZE];
    uint32_t in_data_size;
    uint32_t out_data_size;
    uint32_t n_buffers;
} dsp_sim_request_t;

/* An entry that has its own storage rather than sharing another entry's. */
#define DSP_SIM_NO_ALIAS (0xFFFFFFFFu)

typedef struct {
    uint32_t access_flags; /* this entry's access, as the library declared it */
    uint32_t size;
    /* One command can name the same buffer more than once: two crops into one
       destination, an image that is both source and result. Hardware hands the
       DSP the same physical address each time, so the entries share storage and
       one entry's writes are visible through the others. An entry that repeats
       an earlier one carries that entry's index here; the earlier entry, the
       group leader, carries DSP_SIM_NO_ALIAS. */
    uint32_t alias_of;
    /* Union of access_flags across the group. Only meaningful on a leader, and
       it decides whether the buffer travels back. */
    uint32_t group_flags;
} dsp_sim_buffer_t;

typedef struct {
    uint32_t magic;     /* DSP_SIM_MAGIC_RESPONSE */
    uint32_t cmd_flags; /* the dsp_cmd->flags the firmware completed with */
    uint32_t n_buffers;
} dsp_sim_response_t;

/*
 * Wire order
 *
 *   host -> dsp   dsp_sim_request_t
 *                 dsp_sim_buffer_t x n_buffers
 *                 in_data_size bytes
 *                 every group leader, size bytes each, in order
 *
 *   dsp -> host   dsp_sim_response_t
 *                 uint32_t map_flags x n_buffers  (access the firmware performed)
 *                 out_data_size bytes
 *                 each group leader whose group_flags has WRITE: size bytes
 *
 * A group leader travels to the DSP whatever its access flags say, because that
 * is what hardware does: the DSP is handed the buffer's physical address and
 * sees whatever it already holds. An operation that writes only part of its
 * destination - a crop into a corner, a privacy mask over a few rows - leaves
 * the rest as it found it, and the tests check exactly that.
 *
 * The firmware answers exactly one response per request and never initiates a
 * record. A firmware crash shows up as end-of-file on the response pipe plus a
 * non-zero simulator exit code, which the host reports with the captured
 * firmware log.
 */

#endif /* _DSP_SIM_PROTOCOL_H */
