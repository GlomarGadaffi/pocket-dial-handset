# pocketdial-voice-poc

A **spike** to prove the missing half of [ESPHome feature request #3112](https://github.com/esphome/feature-requests/issues/3112):
a SIP **endpoint with real audio media** on an ESP32-S3 — mic → SIP, SIP → speaker.

> **Status: dev spike, not a product.** Built to validate (or kill) the "all-in-one
> ESP32 SIP voice" idea before committing to a repo/license structure. Target board
> is the only integrated dev board on hand; all board specifics live in one header.

## Why this exists

- **pocket-dial** (MIT) is a great SIP *registrar/proxy* on ESP32 — but it deliberately
  never touches audio (RTP flows peer-to-peer).
- **chrta/sip_call** (AGPL) initiates SIP calls on a GPIO trigger — but has no media path.
- **#3112** is fundamentally an **audio** problem, present in neither. This spike builds
  exactly that gap, reusing pocket-dial's MIT SIP parser for the fiddly bits.

## Target hardware

**LilyGO T3-S3 MVSRBoard** (ESP32-S3, 4 MB flash) — silkscreen rev **V1.0**:
- Mic: **MSM261S4030H0R** I2S MEMS  → I2S1 (BCLK 47, WS 15, DATA 48, EN 35)
- Amp: **MAX98357A** I2S class-D     → I2S0 (BCLK 40, LRCLK 41, DATA 39, SD 38)
- Vibration motor (PWM, GPIO46) — simulates the doorbell "actuation"
- PTT = BOOT button (GPIO0)

> ⚠️ If your silkscreen says **V1.1**, the mic is a **PDM** part (MP34DT05) and needs
> the I2S PDM-RX init instead of standard mode. Adjust `audio_io.c`.

## The design (deliberately half-duplex)

Push-to-talk gates direction, so mic and speaker are never live at once →
**no acoustic echo cancellation needed**, which removes the single hardest part of
ESP32 two-way voice. AEC is a later upgrade, not a blocker.

```
 BOOT held : mic --I2S--> [G.711 µ-law] --> [RTP] --UDP--> ext 102 phone (P2P)
 BOOT up   : ext 102 phone --UDP--> [RTP] --> [G.711] --I2S--> MAX98357A speaker
```

Codec: G.711 µ-law (PCMU, PT 0), 8 kHz mono, 20 ms / 160-sample frames.
Signaling: `REGISTER` ext **500** (no auth — pocket-dial is an open registrar) →
`INVITE` ext **102** *through* the server → `200 OK → ACK` → `BYE`. The pocket-dial
server proxies signaling only; the `200 OK` carries ext 102's own SDP, so **RTP audio
flows peer-to-peer** straight between the board and the phone.

## Build / flash / test

1. **Edit `main/poc_config.h`** — set `POC_WIFI_SSID`/`POC_WIFI_PASS`. Defaults already
   target the pocket-dial server at `192.168.12.2:5060`, register as ext `500`, call ext `102`.
2. **Have ext 102 registered** to the same pocket-dial server (a softphone or IP phone)
   and ready to answer.
3. Build & flash with ESP-IDF v5.1+ / v6.x:
   ```
   idf.py set-target esp32s3
   idf.py -p <PORT> flash monitor
   ```

### Definition of done (the credibility gate)

> Power on → board joins Wi-Fi → **REGISTERs as ext 500** with the pocket-dial server →
> motor buzzes → **INVITEs ext 102 through the server** → 102 answers → **hold BOOT and
> talk: you're heard on 102's phone; release BOOT: 102's audio plays out the board
> speaker.** Intelligible both ways, with RTP flowing board↔102 directly (P2P).

If that works, the "all-in-one" idea is real and worth a proper repo. If the audio is
garbage, we learn the hard truth cheaply.

## What's real vs. stubbed

| Part | State |
|---|---|
| Board pin map, dual-I2S bring-up | ✅ real |
| G.711 µ-law encode/decode | ✅ real (CCITT reference) |
| RTP framing (TX) | ✅ real |
| REGISTER (ext 500, no auth) + INVITE/ACK/BYE via server | ✅ real (parser vendored from pocket-dial) |
| Push-to-talk half-duplex media loop | ✅ real |
| **Jitter buffer** | ⚠️ none — direct play-out (fine on a quiet LAN; add later) |
| **Mic 8 kHz clocking** | ⚠️ verify on bench — if MSM261 needs ≥16 kHz, capture & decimate |
| **Digest auth (MD5)** | ❌ not needed — pocket-dial is an open registrar |
| **Acoustic echo cancellation** | ❌ intentionally avoided via PTT |
| **Incoming calls / full duplex** | ❌ later milestone |

## License

This spike vendors SIP parser files from pocket-dial (**MIT**). The eventual product's
license (notably whether to fork the **AGPL** sip_call) is an open decision — deliberately
deferred until there's something worth shipping.
