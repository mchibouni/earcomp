#include "Status.hpp"
#include "Processor.hpp"
#include <Accelerate/Accelerate.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace earcomp {

void StatusReporter::start(const Processor* processor, double sampleRate, ModeHandler onMode) {
    stop();
    processor_ = processor;
    sampleRate_ = sampleRate;
    onMode_ = std::move(onMode);
    const char* home = std::getenv("HOME");
    if (!home) return;
    dir_ = std::string(home) + "/Library/Application Support/EarComp";
    mkdir(dir_.c_str(), 0755);
    controlTime_ = 0;
    controlNsec_ = -1;
    running_ = true;
    thread_ = std::thread([this] { run(); });
}

void StatusReporter::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void StatusReporter::measure(const float* left, const float* right, int frames) noexcept {
    if (frames <= 0) return;
    const float a = 1.0f - std::exp(-float(frames) / (0.3f * float(sampleRate_)));
    float ml = 0.0f, mr = 0.0f;
    vDSP_measqv(left, 1, &ml, vDSP_Length(frames));
    vDSP_measqv(right, 1, &mr, vDSP_Length(frames));
    meanSquareL_ += a * (ml - meanSquareL_);
    meanSquareR_ += a * (mr - meanSquareR_);
    rmsDbL_.store(meanSquareL_ > 1e-14f ? 10.0f * std::log10(meanSquareL_) : -140.0f, std::memory_order_relaxed);
    rmsDbR_.store(meanSquareR_ > 1e-14f ? 10.0f * std::log10(meanSquareR_) : -140.0f, std::memory_order_relaxed);
}

void StatusReporter::run() {
    while (running_) {
        checkControl();
        writeStatus();
        for (int i = 0; i < 5 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Reads control.json when its modification time changes; calls onMode for a new mode.
void StatusReporter::checkControl() {
    const std::string path = dir_ + "/control.json";
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return;
    if (st.st_mtimespec.tv_sec == controlTime_ && st.st_mtimespec.tv_nsec == controlNsec_) return;
    controlTime_ = st.st_mtimespec.tv_sec;
    controlNsec_ = st.st_mtimespec.tv_nsec;
    std::ifstream f(path);
    std::stringstream text;
    text << f.rdbuf();
    const std::string s = text.str();
    std::string mode;
    if (s.find("\"headphones\"") != std::string::npos) mode = "headphones";
    else if (s.find("\"speakers\"") != std::string::npos) mode = "speakers";
    if (mode.empty() || mode == mode_) return;
    mode_ = mode;
    if (onMode_) onMode_(mode_);
}

void StatusReporter::writeStatus() {
    const std::string path = dir_ + "/status.json", tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return;
    const auto& p = *processor_;
    std::fprintf(f,
                 "{\"time\": %ld, \"sampleRate\": %.0f, \"mode\": \"%s\", \"inputRmsDbfs\": [%.1f, %.1f], "
                 "\"intensity\": %.2f, \"variant\": %d, \"rightTrebleDb\": %.1f, \"hissGuardDb\": %.1f}\n",
                 long(std::time(nullptr)), sampleRate_, mode_.c_str(), rmsDbL_.load(), rmsDbR_.load(),
                 p.intensity.load(), p.variant.load(), p.rightTrebleDb.load(), p.hissGuardDb.load());
    std::fclose(f);
    std::rename(tmp.c_str(), path.c_str());
}

} // namespace earcomp
