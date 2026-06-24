# pocket-dial-handset

ESP32-S3 SIP voice endpoint: push-to-talk G.711 over Wi-Fi with peer-to-peer RTP. registers to the pocket-dial SIP server (an open registrar, no auth), calls a fixed extension through it, and runs a half-duplex media loop — pressing the button (BOOT pin) sends compressed audio, releasing listens. signaling goes through the server; RTP flows peer-to-peer.

## flow

1. Wi-Fi bring-up (NVS credentials)
2. SIP INVITE to configured PBX with hostname/port
3. on 200 OK, enter media loop
4. **PTT held** → mic (I2S ADC) → G.711 μ-law encode → RTP TX (sequenced)
5. **PTT released** → RTP RX → G.711 decode → speaker (MAX98357A amp or internal DAC)
6. **BYE received** → call ends, return to idle

## hardware

- **ESP32-S3** main processor (FreeRTOS)
- **I2S microphone** — configurable sample rate (typically 8 kHz), 16-bit PCM
- **Speaker output** — MAX98357A amplifier (or codec DAC)
- **Button** — BOOT pin for push-to-talk (active LOW)
- **Motor** — GPIO-actuated buzz on call connect (optional)

board variants supported via `board_mvsr.h` (pin definitions, audio I/O config).

## codec & transport

- **audio codec**: G.711 μ-law (8 kbps, 20ms frames, 160 samples/frame)
- **RTP** — standard SIP media transport with sequence/timestamp/SSRC
- **half-duplex** — no acoustic echo cancellation required by design

## configuration

`poc_config.h` — compile-time config, edit before flashing:
- `POC_WIFI_SSID` / `POC_WIFI_PASS` — Wi-Fi STA credentials (SIP rides Wi-Fi; the board's LoRa is unused)
- `POC_SIP_SERVER_IP` / `POC_SIP_SERVER_PORT` — pocket-dial registrar/proxy (default `192.168.12.2:5060`)
- `POC_SIP_EXT_SELF` / `POC_SIP_EXT_CALLEE` — own extension (`500`) and callee (`102`)
- `POC_SIP_LOCAL_PORT` / `POC_RTP_LOCAL_PORT` — local SIP (5060) and RTP (4000; RTCP would be +1)
- audio: PCMU (G.711 µ-law, PT 0), 8 kHz mono, 160 samples per 20 ms frame; PDM mic captured at 16 kHz, decimated 2:1

pin definitions and audio I/O are in `board_mvsr.h`.

## notes

proof-of-concept for ESP32 SIP voice. designed for low-latency half-duplex scenarios (field comms, homelabs, intercom). not intended for full-duplex or high-quality voice (would require AEC, jitter buffers, adaptive bitrate).
