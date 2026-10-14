// Links the plugin with tools outside the host, through two small JSON files in
// ~/Library/Application Support/EarComp/:
//   status.json   written four times a second: input level, parameters, current mode
//   control.json  read when it changes: {"mode": "speakers"} or {"mode": "headphones"}
// The render thread only updates atomics; a background thread does the file work and calls
// onMode (on that thread) when control.json asks for a different mode, and once at start.
#pragma once
#include <atomic>
#include <ctime>
#include <functional>
#include <string>
#include <thread>

namespace earcomp {

class Processor;

class StatusReporter {
public:
    using ModeHandler = std::function<void(const std::string& mode)>;

    ~StatusReporter() { stop(); }
    void start(const Processor* processor, double sampleRate, ModeHandler onMode);   // not real-time safe
    void stop();                                                                      // not real-time safe
    void measure(const float* left, const float* right, int frames) noexcept;        // real-time safe

private:
    void run();
    void checkControl();
    void writeStatus();

    const Processor* processor_ = nullptr;
    double sampleRate_ = 48000.0;
    ModeHandler onMode_;
    float meanSquareL_ = 0.0f;                 // render thread only
    float meanSquareR_ = 0.0f;
    std::atomic<float> rmsDbL_{-140.0f};       // input RMS, dBFS, ~300 ms average
    std::atomic<float> rmsDbR_{-140.0f};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::string dir_;
    std::string mode_;                         // background thread only
    std::time_t controlTime_ = 0;
    long controlNsec_ = -1;
};

} // namespace earcomp
