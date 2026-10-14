// EarComp DSP core: an FFT-domain multiband compressor that reproduces the gain map
// measured from Mimi's engine (MimiTables.h), with an optional extra treble boost for
// the right ear. Real-time safe after prepare(): no allocation, no locks.
#pragma once
#include <Accelerate/Accelerate.h>
#include <atomic>

namespace earcomp {

class Processor {
public:
    static constexpr int kFFT = 256;           // same frame/hop as Mimi
    static constexpr int kHop = 64;
    static constexpr int kLog2FFT = 8;
    static constexpr int kBins = kFFT / 2 + 1;
    static constexpr int kFifoLatency = kFFT - kHop;   // input FIFO offset
    static constexpr int kLatency = kFFT - kHop;       // input->output delay (192, like Mimi)

    Processor();
    ~Processor();
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;

    void prepare(double sampleRate);           // not real-time safe
    void reset();                              // clears state, real-time safe
    // Stereo processing; in and out may be the same buffers. Any frame count.
    void process(const float* inL, const float* inR, float* outL, float* outR, int frames);

    // Parameters: set from any thread, read once per FFT frame on the audio thread.
    std::atomic<float> intensity{1.0f};        // 0..1, dry/wet blend like Mimi's slider
    std::atomic<int> variant{0};               // 0 Recommended, 1 Richer, 2 Softer
    std::atomic<float> rightTrebleDb{0.0f};    // extra gain for the right ear above ~4 kHz
    // Below this level the boosts (and the right-ear extra) fade out over kHissGuardRange dB,
    // so a faint hiss floor is not lifted. At or below kHissGuardOff it is disabled (pure Mimi).
    std::atomic<float> hissGuardDb{-100.0f};
    static constexpr float kHissGuardOff = -100.0f;
    static constexpr float kHissGuardRange = 20.0f;
    std::atomic<float> detectOctaves{3.0f};    // level-detector bandwidth (tuned to Mimi)
    std::atomic<float> smoothingMs{50.0f};     // level-detector time constant (tuned to Mimi)

private:
    void processFrame();
    void updateDetector(float octaves, float ms);

    double sampleRate_ = 48000.0;
    FFTSetup fft_ = nullptr;
    float window_[kFFT];                       // analysis window: sqrt-Hann
    float synthWindow_[kFFT];                  // synthesis window, zero over the first hop
    float inFifo_[2][kFFT];
    float outFifo_[2][kHop];
    float accum_[2][kFFT];
    int rover_ = kFifoLatency;

    float frame_[kFFT];
    float re_[2][kFFT / 2];
    float im_[2][kFFT / 2];
    float power_[2][kBins];
    float prefix_[kBins + 1];
    float levelDb_[kBins];                     // smoothed, channel-linked level per bin
    float gainL_[kBins];
    float gainR_[kBins];

    float binHz_[kBins];
    int tableIdx_[kBins];                      // table column below the bin frequency
    float tableFrac_[kBins];                   // interpolation weight toward the next column
    float rightRamp_[kBins];                   // 0..1 weight of the right-ear treble offset
    int detLo_[kBins];                         // detector window per bin (inclusive)
    int detHi_[kBins];
    float levelCalDb_ = 0.0f;                  // turns window power into sine dBFS
    float smoothCoef_ = 0.0f;
    float detOctavesUsed_ = -1.0f;
    float smoothMsUsed_ = -1.0f;
};

} // namespace earcomp
