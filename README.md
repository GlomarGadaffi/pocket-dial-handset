# tincan

**A SIP voice endpoint on an ESP32-S3 — registers to a PBX, places a call, and carries two-way G.711 audio peer-to-peer.** A working answer to the long-standing "SIP voice on ESP32" ask in [ESPHome feature-request #3112](https://github.com/esphome/feature-requests/issues/3112).

> **Status: validated proof-of-concept.** The full path — `REGISTER → INVITE → ring → answer → bidirectional RTP audio → BYE → clean teardown` — has been confirmed on real silicon (LilyGO T3-S3 MVSRBoard) against a live [pocket-dial](https://github.com/GlomarGadaffi/pocket-dial) server. It is **not** production firmware: half-duplex, no jitter buffer, no digest auth (see [Constraints](#constraints--engineering-notes)). It is a clean, honest base to build on.

---

## What it does

pocket-dial (and its upstream, BarGabriel/SipServer) is a SIP **registrar/proxy** — it brokers signaling but never touches audio. The #3112 ask is the other half: a SIP **endpoint** that actually moves voice. tincan is that endpoint. It speaks just enough SIP to register and set up a call, then streams G.711/RTP **directly to the far end** (the proxy stays out of the media path), so a $10 MCU coordinates and carries a real phone call.

```
        ESP32-S3 (tincan)                      pocket-dial PBX            ext 102 (phone)
   mic ─I2S─► G.711 ─► RTP ─┐                  192.168.x.2:5060            192.168.x.181
                            ├─ SIP REGISTER/INVITE/ACK/BYE ─►  ──routes──►  rings / answers
   spk ◄I2S─ G.711 ◄─ RTP ─┘                                                     │
        ▲                                                                        │
        └──────────────── RTP (G.711 µ-law, UDP) flows PEER-TO-PEER ─────────────┘
```

---

## Target hardware

**LilyGO T3-S3 MVSRBoard** — board revision **V1.1** (silkscreen). ESP32-S3 (dual Xtensa LX7 @ 240 MHz), 4 MB flash, QSPI PSRAM present but unused.

| Function | Part | Bus / pins |
|---|---|---|
| Microphone | **MP34DT05-A** PDM MEMS | **I2S0** (PDM-RX): CLK `IO15`, DATA `IO48`, EN `IO35` |
| Speaker amp | **MAX98357A** class-D | **I2S1** (std-TX): BCLK `IO40`, LRCLK `IO41`, DIN `IO39`, SD_MODE `IO38` |
| Haptic | vibration motor | `IO46` (call-state buzz) |
| Push-to-talk | BOOT button | `IO0` (held = talk, released = listen) |

> **Why the split:** on the ESP32-S3, PDM RX is only available on I2S0, so the mic owns I2S0 and the speaker (standard Philips mode) takes I2S1 — two independent controllers. All board specifics live in [`main/board_mvsr.h`](main/board_mvsr.h); a different board is a single-file swap. *(V1.0 boards ship an MSM261 I2S mic on BCLK 47/WS 15/DATA 48 and need `i2s_channel_init_std_mode` instead — noted in the header.)*

**Memory budget:** firmware image ≈ 830 KB in a 1.5 MB factory partition (`SINGLE_APP_LARGE`); ≈ 289 KB internal RAM free for heap at runtime. Comfortable headroom.

---

## Firmware architecture

Single-task design — `app_main` (main task, CPU0) runs setup, then the media loop. No dynamic allocation in the hot path; SIP message buffers are stack-local and bounded.

```
boot ─► NVS ─► audio_init (PDM mic + amp, amp OFF) ─► Wi-Fi STA (blocks until IP)
     ─► SIP REGISTER ext 500 (no auth)  ─► INVITE ext 102 via proxy
     ─► [180 Ringing, up to ~120 s]      ─► 200 OK ─► ACK ─► amp ON
     ─► media loop (push-to-talk, 20 ms frames)
     ─► peer BYE detected ─► amp OFF ─► idle
```

- **Audio path:** PDM mic captured at **16 kHz** through the S3's hardware PDM→PCM filter (`SOC_I2S_SUPPORTS_PDM2PCM`), decimated 2:1 to the **8 kHz** G.711 rate; speaker fed at 8 kHz on the second I2S controller.
- **Codec / framing:** G.711 µ-law (PCMU, RTP payload type 0), 8 kHz mono, **20 ms / 160-sample frames**. The blocking I2S read paces the loop — no software timer needed.
- **Half-duplex by design:** PTT gates direction, so mic and speaker are never live simultaneously → **acoustic echo cancellation is sidestepped entirely**, removing the single hardest part of ESP32 two-way voice.
- **Amp gating:** `SD_MODE` is held low except during a connected call — eliminates the continuous I2S-underrun click while booting/ringing/idle.
- **Clean teardown:** the media loop polls the signaling socket (non-blocking) for an inbound `BYE`; on hangup it stops, mutes the amp, and idles. RTP underrun feeds a silence frame so the DMA never repeats a stale buffer.
- **SIP layer:** vendored from pocket-dial (MIT) for message (de)serialization; the user-agent **client** logic (`main/sip_uac.cpp`) — REGISTER/INVITE/ACK/BYE construction, response parsing, SDP endpoint extraction — is original.

---

## Build / flash / run

Requires **ESP-IDF** (validated on **v5.2.1**; also builds on **v6.0.1** — the CMake guards the v6 `esp_driver_*` component split).

```sh
# 1. Configure your network + targets
#    edit main/poc_config.h: POC_WIFI_SSID / POC_WIFI_PASS
#    (defaults: server 192.168.12.2:5060, register as 500, call 102)

# 2. Build + flash + monitor
idf.py set-target esp32s3
idf.py -p <PORT> flash monitor
```

A peer at **ext 102** must be registered to the same pocket-dial server and able to answer. On boot the board auto-dials it; answer, hold **BOOT** to talk, release to listen, hang up to end.

---

## Validation

Confirmed end-to-end on a LilyGO T3-S3 MVSRBoard ↔ pocket-dial server (Raspberry Pi), serial + server-side `journalctl` cross-checked:

```
registered as 500
INVITE 102 → 100 Trying → 180 Ringing → 200 OK → ACK
P2P media to 192.168.12.181  (two-way G.711, intelligible both directions)
<- BYE (peer hung up) → call ended → idle    (no looping, no clicking)
```

---

## Constraints & engineering notes

The footguns that bit during bring-up — documented so they don't bite again:

- **`thread_local` TLS bloat is lethal on small RTOS tasks.** The vendored `IDGen` originally used `thread_local std::mt19937` (~2.5 KB). ESP-IDF copies the TLS template into *every* task stack, overflowing the idle (~1.5 KB) and IPC (~1.3 KB) tasks at creation → boot crash (`esp_ipc_init` assert on v6.0.1; SMP-scheduler `LoadProhibited` on v5.2.1 — same bug, two faces). Fixed by using `esp_random()` (stateless, zero TLS). **Audit vendored C++ for `thread_local` before putting it on an MCU.**
- **IDF v6 driver split:** `driver/i2s_std.h`, `driver/i2s_pdm.h`, `driver/gpio.h` moved to `esp_driver_i2s` / `esp_driver_gpio` on v6.0. CMake selects per `IDF_VERSION_MAJOR`.
- **Flash mode:** DIO (the board's flash didn't benefit from QIO; DIO is the safe default here).
- **No jitter buffer** — direct play-out. Fine on a quiet LAN; will need a small adaptive buffer for congested networks.
- **No acoustic echo cancellation** — intentional (half-duplex PTT). Required before full-duplex.
- **No digest auth** — relies on pocket-dial's open registrar. Add MD5 digest (RFC 3261 §22) for a real PBX.

---

## What's real vs. stubbed

| Component | State |
|---|---|
| Board pin map + dual-I2S bring-up | ✅ real, on silicon |
| PDM mic capture (16 kHz → 8 kHz decimate) | ✅ real |
| G.711 µ-law codec | ✅ real (CCITT/Sun reference) |
| RTP framing + P2P media transport | ✅ real |
| SIP UAC: REGISTER / INVITE / ACK / BYE | ✅ real |
| Push-to-talk half-duplex + clean BYE teardown | ✅ real |
| Jitter buffer | ⚠️ none (direct play-out) |
| Digest auth (MD5) | ❌ open registrar only |
| Acoustic echo cancellation / full duplex | ❌ roadmap |
| Incoming calls (answer, not just originate) | ❌ roadmap |

## Roadmap

Adaptive jitter buffer · digest auth · full-duplex + AEC · inbound call handling · dial-on-button (vs auto-dial on boot) · packaging as an ESPHome external component.

---

## License & attribution

**MIT** — see [LICENSE](LICENSE). Dual copyright:

- **Original work** (audio pipeline, G.711, RTP, the `sip_uac` SIP client, Wi-Fi/board integration, application code) — © 2026 GlomarGadaffi.
- **Vendored SIP message parser** in [`components/sip_core/`](components/sip_core/) (`SipMessage`, `SipSdpMessage`, `SipMessageFactory`, `SipMessageTypes`, `IDGen`) — © 2022 BarGabriel, derived via [pocket-dial](https://github.com/GlomarGadaffi/pocket-dial) from **[BarGabriel/SipServer](https://github.com/BarGabriel/SipServer)** (MIT).

Also referenced (credited, no obligation): the **G.711** CCITT/Sun reference companding algorithm, and LilyGO's [T3-S3-MVSRBoard](https://github.com/Xinyuan-LilyGO/T3-S3-MVSRBoard) `pin_config.h` for the pin map. Built on MIT code only — **no copyleft dependencies**.
