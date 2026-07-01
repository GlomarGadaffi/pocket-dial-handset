#pragma once
// Jitter buffer between RTP-RX and the speaker. Ported from drawbridge's
// PlayoutBuffer (C:\Users\desmo\Documents\antigravity\drawbridge\src\SIP\PlayoutBuffer.hpp)
// — zero ESP-IDF dependency, sized here for 8 kHz G.711 via poc_config.h.
#include <vector>
#include <mutex>
#include <cstdint>
#include <cstddef>
#include <atomic>

#include "poc_config.h"

#define PLAYOUT_TARGET_SAMPLES ((POC_JITTER_TARGET_MS) * (POC_SAMPLE_RATE_HZ) / 1000)
#define PLAYOUT_MAX_SAMPLES    ((POC_JITTER_MAX_MS) * (POC_SAMPLE_RATE_HZ) / 1000)

class PlayoutBuffer
{
public:
    // maxSamples is the HARD latency ceiling: a producer burst can never park
    // more than this much audio here (overrun drops oldest). Steady-state is
    // held far lower by the target-depth drain in read() (see _targetDepth).
    explicit PlayoutBuffer(size_t maxSamples = PLAYOUT_MAX_SAMPLES);
    ~PlayoutBuffer() = default;

    // Write linear PCM16 samples (called by the RTP-RX task).
    // If the buffer overflows, oldest samples are dropped to make room.
    // Returns the number of samples successfully written.
    size_t write(const int16_t* samples, size_t count);

    // Read linear PCM16 samples (called by the Playout task every ~20 ms).
    // Always fills 'out' with 'count' samples. If underrun, fills the rest with
    // low-amplitude comfort noise.
    // Returns true if all requested samples were read from real buffered audio,
    // false if an underrun occurred.
    bool read(int16_t* out, size_t count);

    // Get current buffered sample count
    size_t getLength() const;

    // Reset buffer (clear all samples, reset stats)
    void clear();

    // Statistics queries
    uint64_t getUnderruns() const { return _underruns.load(std::memory_order_relaxed); }
    uint64_t getOverruns() const { return _overruns.load(std::memory_order_relaxed); }
    size_t getTargetDepth() const { return _targetDepth.load(std::memory_order_relaxed); }

    // Set/Get target playout delay (in samples)
    void setTargetDepth(size_t samples);

private:
    std::vector<int16_t> _buffer;
    size_t _maxSamples;
    size_t _readPtr = 0;
    size_t _writePtr = 0;
    size_t _count = 0;

    mutable std::mutex _mutex;

    // Target playout depth = the standing jitter cushion read() drains toward
    // (POC_JITTER_TARGET_MS). read() drops the oldest excess whenever the
    // buffer drifts above target, so latency walks back DOWN instead of
    // accumulating. Lower = less delay but more underrun risk on a lossy/jittery
    // link; tune against getUnderruns().
    std::atomic<size_t> _targetDepth{PLAYOUT_TARGET_SAMPLES};

    // Statistics
    std::atomic<uint64_t> _underruns{0};
    std::atomic<uint64_t> _overruns{0};

    // Comfort-noise LCG state — instance member (not a function-local static)
    // so two PlayoutBuffer instances never share PRNG state. Only ever
    // touched under _mutex (from read()).
    uint32_t _noiseSeed = 123456789;
};
