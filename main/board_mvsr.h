#pragma once
// ─────────────────────────────────────────────────────────────────────────
//  LilyGO T3-S3 MVSRBoard  —  pin map  (silkscreen rev V1.0)
//  Source: official pinout + Xinyuan-LilyGO/T3-S3-MVSRBoard.
//
//  Mic  : MSM261S4030H0R  I2S MEMS  (V1.1 swaps to MP34DT05 PDM — different!)
//  Amp  : MAX98357A        I2S class-D, 9 dB gain
//  Two INDEPENDENT I2S buses -> full-duplex capable (we use half-duplex here).
//
//  *** This is the DEV board. Keep all board specifics in this one header so
//      the eventual product board is a single-file swap. ***
// ─────────────────────────────────────────────────────────────────────────

// ── Microphone (I2S RX) ───────────────────────────────────────────────────
#define MVSR_MIC_I2S_PORT     I2S_NUM_1
#define MVSR_MIC_BCLK         GPIO_NUM_47
#define MVSR_MIC_WS           GPIO_NUM_15
#define MVSR_MIC_DATA         GPIO_NUM_48   // SD / DOUT of the mic
#define MVSR_MIC_EN           GPIO_NUM_35   // drive HIGH to power the mic

// ── Speaker (I2S TX -> MAX98357A) ─────────────────────────────────────────
#define MVSR_SPK_I2S_PORT     I2S_NUM_0
#define MVSR_SPK_BCLK         GPIO_NUM_40
#define MVSR_SPK_LRCLK        GPIO_NUM_41   // WS
#define MVSR_SPK_DATA         GPIO_NUM_39
#define MVSR_SPK_SD_MODE      GPIO_NUM_38   // drive HIGH to enable amp (default gain)

// ── Vibration motor (PWM) — used to simulate the doorbell "actuation" ─────
#define MVSR_MOTOR            GPIO_NUM_46

// ── Push-to-talk button — reuse the BOOT button for the dev spike ─────────
//    Pressed = LOW. Held = TALK (mic->RTP); released = LISTEN (RTP->spk).
#define MVSR_PTT_BUTTON       GPIO_NUM_0

// ── Optional peripherals (not used by the spike, documented for later) ────
#define MVSR_OLED_SDA         GPIO_NUM_18
#define MVSR_OLED_SCL         GPIO_NUM_17
