#pragma once
// ─────────────────────────────────────────────────────────────────────────
//  PoC configuration — EDIT THESE before flashing.
//  (Kept as #defines for spike simplicity; promote to menuconfig later.)
//
//  Topology:  board REGISTERs as ext 500 (no auth) to the pocket-dial SIP
//  server, then calls ext 102 THROUGH the server. The server brokers
//  signaling only — RTP audio flows peer-to-peer straight to ext 102's phone.
// ─────────────────────────────────────────────────────────────────────────

// Wi-Fi STA credentials (SIP rides Wi-Fi; the board's LoRa is unused).
#define POC_WIFI_SSID        "CHANGE_ME"
#define POC_WIFI_PASS        "CHANGE_ME"

// pocket-dial SIP server (registrar/proxy) currently running on the LAN.
#define POC_SIP_SERVER_IP    "192.168.12.2"
#define POC_SIP_SERVER_PORT  5060

// Our identity and who we call. pocket-dial is an OPEN registrar (no auth).
#define POC_SIP_EXT_SELF     "500"   // we register as this extension
#define POC_SIP_EXT_CALLEE   "102"   // we call this extension via the server
#define POC_SIP_REG_EXPIRES  3600

// Local ports.
#define POC_SIP_LOCAL_PORT   5060
#define POC_RTP_LOCAL_PORT   4000             // even port; RTCP would be +1

// Audio: G.711 µ-law (PCMU, payload type 0), 8 kHz mono, 20 ms frames.
#define POC_SAMPLE_RATE_HZ   8000
#define POC_FRAME_SAMPLES    160              // 8000 Hz * 0.020 s
#define POC_RTP_PAYLOAD_PCMU 0
