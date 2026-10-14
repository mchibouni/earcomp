#include "Processor.hpp"
#include "MimiTables.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace earcomp {

namespace {
constexpr float kFloorDb = -140.0f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Weight of the right-ear offset: 0 below 3 kHz, 1 from 5 kHz to 12.5 kHz,
// back to 0 at 16 kHz (Mimi leaves 16 kHz untouched).
float rightRampAt(float hz) {
    if (hz <= 3000.0f || hz >= 16000.0f) return 0.0f;
    if (hz < 5000.0f) return std::log2(hz / 3000.0f) / std::log2(5000.0f / 3000.0f);
    if (hz <= 12500.0f) return 1.0f;
    return std::log2(16000.0f / hz) / std::log2(16000.0f / 12500.0f);
}
} // namespace

Processor::Processor() {
    fft_ = vDSP_create_fftsetup(kLog2FFT, kFFTRadix2);
    for (int n = 0; n < kFFT; ++n)  // periodic Hann, square-rooted
        window_[n] = std::sqrt(0.5f * (1.0f - std::cos(2.0f * float(M_PI) * n / kFFT)));
    // Low-delay synthesis: analysis x synthesis = a 192-sample Hann placed over the last three
    // hops of the frame. The newest frame then adds nothing to the oldest hop still in the
    // accumulator, so that hop can be output one hop earlier (latency 192 instead of 256).
    constexpr int kTail = kFFT - kHop;
    for (int n = 0; n < kFFT; ++n) {
        if (n < kHop) { synthWindow_[n] = 0.0f; continue; }
        float h = 0.5f * (1.0f - std::cos(2.0f * float(M_PI) * (n - kHop) / kTail));
        synthWindow_[n] = h / window_[n];
    }
    // window power for a full-scale sine: one-sided bin power sum = (N/2) * (A^2/2) * sum(w^2)
    float w2 = 0.0f;
    for (float w : window_) w2 += w * w;
    levelCalDb_ = 10.0f * std::log10((kFFT / 2.0f) * 0.5f * w2);
    prepare(48000.0);
}

Processor::~Processor() {
    if (fft_) vDSP_destroy_fftsetup(fft_);
}

void Processor::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (int k = 0; k < kBins; ++k) {
        float hz = float(k * sampleRate / kFFT);
        binHz_[k] = hz;
        // table column interpolation in log-frequency
        float f = std::max(hz, kTableFreqHz[0]);
        int i = 0;
        while (i < kTableFreqs - 2 && f > kTableFreqHz[i + 1]) ++i;
        float t = std::log2(f / kTableFreqHz[i]) / std::log2(kTableFreqHz[i + 1] / kTableFreqHz[i]);
        tableIdx_[k] = i;
        tableFrac_[k] = std::clamp(t, 0.0f, 1.0f);
        rightRamp_[k] = rightRampAt(hz);
    }
    detOctavesUsed_ = -1.0f;
    smoothMsUsed_ = -1.0f;
    updateDetector(detectOctaves.load(), smoothingMs.load());
    reset();
}

void Processor::reset() {
    std::memset(inFifo_, 0, sizeof inFifo_);
    std::memset(outFifo_, 0, sizeof outFifo_);
    std::memset(accum_, 0, sizeof accum_);
    for (float& l : levelDb_) l = kFloorDb;
    rover_ = kFifoLatency;
}

void Processor::updateDetector(float octaves, float ms) {
    if (octaves != detOctavesUsed_) {
        const float binHz = float(sampleRate_ / kFFT);
        const float half = std::pow(2.0f, octaves / 2.0f);
        for (int k = 0; k < kBins; ++k) {
            // at least +-2 bins so a sine's whole main lobe is measured
            int lo = int(std::floor(binHz_[k] / half / binHz));
            int hi = int(std::ceil(binHz_[k] * half / binHz));
            detLo_[k] = std::clamp(std::min(lo, k - 2), 0, kBins - 1);
            detHi_[k] = std::clamp(std::max(hi, k + 2), 0, kBins - 1);
        }
        detOctavesUsed_ = octaves;
    }
    if (ms != smoothMsUsed_) {
        smoothCoef_ = std::exp(-float(kHop) / (std::max(ms, 0.1f) * 0.001f * float(sampleRate_)));
        smoothMsUsed_ = ms;
    }
}

void Processor::process(const float* inL, const float* inR, float* outL, float* outR, int frames) {
    for (int i = 0; i < frames; ++i) {
        const float l = inL[i], r = inR[i];
        inFifo_[0][rover_] = l;
        inFifo_[1][rover_] = r;
        outL[i] = outFifo_[0][rover_ - kFifoLatency];
        outR[i] = outFifo_[1][rover_ - kFifoLatency];
        if (++rover_ >= kFFT) {
            rover_ = kFifoLatency;
            processFrame();
        }
    }
}

void Processor::processFrame() {
    updateDetector(detectOctaves.load(std::memory_order_relaxed), smoothingMs.load(std::memory_order_relaxed));
    const int var = std::clamp(variant.load(std::memory_order_relaxed), 0, kTableVariants - 1);
    const float mix = std::clamp(intensity.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float rightDb = rightTrebleDb.load(std::memory_order_relaxed);

    // analysis
    for (int c = 0; c < 2; ++c) {
        vDSP_vmul(inFifo_[c], 1, window_, 1, frame_, 1, kFFT);
        DSPSplitComplex z{re_[c], im_[c]};
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(frame_), 2, &z, 1, kFFT / 2);
        vDSP_fft_zrip(fft_, &z, 1, kLog2FFT, kFFTDirection_Forward);
        // zrip output is 2x the DFT, so divide powers by 4; DC and Nyquist are packed in [0]
        float* p = power_[c];
        p[0] = 0.25f * re_[c][0] * re_[c][0];
        p[kBins - 1] = 0.25f * im_[c][0] * im_[c][0];
        for (int k = 1; k < kFFT / 2; ++k)
            p[k] = 0.25f * (re_[c][k] * re_[c][k] + im_[c][k] * im_[c][k]);
    }

    // level per bin: power in a window around it, each channel in dB, then averaged (linked)
    float levelCh[2][kBins];
    for (int c = 0; c < 2; ++c) {
        prefix_[0] = 0.0f;
        for (int k = 0; k < kBins; ++k) prefix_[k + 1] = prefix_[k] + power_[c][k];
        for (int k = 0; k < kBins; ++k) {
            float pw = prefix_[detHi_[k] + 1] - prefix_[detLo_[k]];
            levelCh[c][k] = pw > 1e-20f ? 10.0f * std::log10(pw) - levelCalDb_ : kFloorDb;
        }
    }
    for (int k = 0; k < kBins; ++k) {
        float lev = std::max(0.5f * (levelCh[0][k] + levelCh[1][k]), kFloorDb);
        levelDb_[k] = lev + smoothCoef_ * (levelDb_[k] - lev);

        // table lookup: bilinear in (level, log-frequency)
        float li = std::clamp((levelDb_[k] - kTableLevelMin) / kTableLevelStep, 0.0f, float(kTableLevels - 1));
        int l0 = std::min(int(li), kTableLevels - 2);
        float lt = li - l0;
        int f0 = tableIdx_[k];
        float ft = tableFrac_[k];
        const auto& T = kGainDb[var];
        float g = lerp(lerp(T[l0][f0], T[l0][f0 + 1], ft), lerp(T[l0 + 1][f0], T[l0 + 1][f0 + 1], ft), lt);

        float wetL = std::pow(10.0f, g / 20.0f);
        float wetR = std::pow(10.0f, (g + rightDb * rightRamp_[k]) / 20.0f);
        gainL_[k] = (1.0f - mix) + mix * wetL;
        gainR_[k] = (1.0f - mix) + mix * wetR;
    }

    // synthesis: apply gains, inverse FFT, window, overlap-add
    // zrip forward+inverse scales by 2N; the 192-sample Hann products overlap 3x and sum to 1.5
    const float scale = 1.0f / (2.0f * kFFT * 1.5f);
    for (int c = 0; c < 2; ++c) {
        const float* g = c == 0 ? gainL_ : gainR_;
        re_[c][0] *= g[0];
        im_[c][0] *= g[kBins - 1];
        for (int k = 1; k < kFFT / 2; ++k) {
            re_[c][k] *= g[k];
            im_[c][k] *= g[k];
        }
        DSPSplitComplex z{re_[c], im_[c]};
        vDSP_fft_zrip(fft_, &z, 1, kLog2FFT, kFFTDirection_Inverse);
        vDSP_ztoc(&z, 1, reinterpret_cast<DSPComplex*>(frame_), 2, kFFT / 2);
        for (int n = kHop; n < kFFT; ++n) accum_[c][n] += frame_[n] * synthWindow_[n] * scale;

        // accum[kHop, 2*kHop) is now complete: later frames add nothing to it
        std::memcpy(outFifo_[c], accum_[c] + kHop, kHop * sizeof(float));
        std::memmove(accum_[c], accum_[c] + kHop, (kFFT - kHop) * sizeof(float));
        std::memset(accum_[c] + kFFT - kHop, 0, kHop * sizeof(float));
        std::memmove(inFifo_[c], inFifo_[c] + kHop, kFifoLatency * sizeof(float));
    }
}

} // namespace earcomp
