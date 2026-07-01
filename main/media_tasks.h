#pragma once
// Full-duplex media engine: three FreeRTOS tasks (mic/TX, RTP-RX, playout)
// replacing the original single-task half-duplex media_loop(). TX and
// Playout run on CPU1 (otherwise idle); RX stays on CPU0 near Wi-Fi/lwIP.
// A jitter buffer (PlayoutBuffer) decouples playout pacing from RTP arrival
// jitter, so the speaker never blocks on the network.
#include <lwip/sockets.h>
#include "sip_uac.h"

// Runs full-duplex media until the peer hangs up (BYE detected via
// uac.checkHangup(), polled here exactly as the original media_loop did).
// Blocks the calling task until the call ends; spawns and tears down its
// own three media tasks internally. Drop-in replacement call site for the
// old media_loop(uac, rtp_sock, dst).
void media_run_full_duplex(SipUac &uac, int rtp_sock, const sockaddr_in &dst);
