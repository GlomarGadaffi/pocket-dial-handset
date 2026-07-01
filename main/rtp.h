#pragma once
// Minimal RTP (RFC 3550) — just enough to carry G.711 frames for the spike.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RTP_HEADER_LEN 12

#ifdef __cplusplus
extern "C" {
#endif

// Per-stream state the sender increments each frame.
typedef struct {
    uint16_t seq;
    uint32_t timestamp;   // +POC_FRAME_SAMPLES per 20 ms G.711 frame
    uint32_t ssrc;
} rtp_tx_t;

// Write a 12-byte RTP header (V=2, no padding/ext/CSRC) into `out` (>=12 bytes).
static inline void rtp_write_header(uint8_t *out, uint8_t pt, uint8_t marker,
                                    uint16_t seq, uint32_t ts, uint32_t ssrc)
{
    out[0]  = 0x80;                                       // V=2
    out[1]  = (uint8_t)((marker ? 0x80 : 0) | (pt & 0x7F));
    out[2]  = (uint8_t)(seq >> 8);
    out[3]  = (uint8_t)(seq & 0xFF);
    out[4]  = (uint8_t)(ts >> 24);
    out[5]  = (uint8_t)(ts >> 16);
    out[6]  = (uint8_t)(ts >> 8);
    out[7]  = (uint8_t)(ts & 0xFF);
    out[8]  = (uint8_t)(ssrc >> 24);
    out[9]  = (uint8_t)(ssrc >> 16);
    out[10] = (uint8_t)(ssrc >> 8);
    out[11] = (uint8_t)(ssrc & 0xFF);
}

// ── RTP receive-side parsing + basic validation ────────────────────────────
// The original half-duplex spike never needed this: it just skipped a fixed
// 12-byte offset and trusted the payload blindly. Full duplex runs the RX
// path continuously and unattended, so it needs real header sanity checks.

typedef struct {
    uint8_t  version;
    uint8_t  pt;              // payload type (7 bits)
    bool     marker;
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
    size_t   payload_offset;  // bytes to skip to reach the payload (>=12, +4*CC)
} rtp_rx_hdr_t;

// Parse + sanity-check an inbound RTP header. Returns false (reject the
// packet) on: too short, wrong version, or the extension bit set — no
// legitimate peer in this point-to-point topology sends header extensions,
// so we don't parse them (simplification, not a real limitation here).
static inline bool rtp_parse_header(const uint8_t *pkt, size_t len, rtp_rx_hdr_t *out)
{
    if (!pkt || !out || len < RTP_HEADER_LEN) return false;
    uint8_t b0 = pkt[0], b1 = pkt[1];
    out->version = (uint8_t)(b0 >> 6);
    if (out->version != 2) return false;
    if (b0 & 0x20) return false;              // padding bit set — unsupported, reject
    if (b0 & 0x10) return false;              // extension bit set — unsupported, reject
    uint8_t cc = (uint8_t)(b0 & 0x0F);
    out->marker = (b1 & 0x80) != 0;
    out->pt     = (uint8_t)(b1 & 0x7F);
    out->seq    = (uint16_t)((pkt[2] << 8) | pkt[3]);
    out->ts     = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16) |
                  ((uint32_t)pkt[6] << 8)  |  (uint32_t)pkt[7];
    out->ssrc   = ((uint32_t)pkt[8] << 24) | ((uint32_t)pkt[9] << 16) |
                  ((uint32_t)pkt[10] << 8) |  (uint32_t)pkt[11];
    out->payload_offset = RTP_HEADER_LEN + 4u * cc;
    if (len < out->payload_offset) return false;
    return true;
}

// Per-stream sequence-number tracking (loss / out-of-order), 16-bit-wraparound-safe.
typedef struct {
    bool     init;
    uint16_t expected;
    uint32_t received;
    uint32_t lost;
    uint32_t out_of_order;
} rtp_rx_track_t;

static inline void rtp_rx_track_update(rtp_rx_track_t *t, uint16_t seq)
{
    t->received++;
    if (!t->init) {
        t->init = true;
        t->expected = (uint16_t)(seq + 1);
        return;
    }
    int16_t delta = (int16_t)(seq - t->expected);
    if (delta == 0) {
        t->expected = (uint16_t)(seq + 1);
    } else if (delta > 0) {
        t->lost += (uint32_t)delta;           // gap: `delta` packets presumed lost
        t->expected = (uint16_t)(seq + 1);
    } else {
        t->out_of_order++;                    // late/duplicate — don't move expected back
    }
}

#ifdef __cplusplus
}
#endif
