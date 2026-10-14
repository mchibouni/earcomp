// Offline probe of the EarComp processor, same measurements as the Mimi probe
// (tones x levels per variant, level steps, pink noise). Writes JSON to stdout.
// usage: probe [--octaves X] [--ms Y] [--sr Z]   (defaults: the shipped Processor settings)
#include "../src/Processor.hpp"
#include "../src/MimiTables.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace earcomp;

static double SR = 48000.0;
static float octaves = -1.0f, ms = -1.0f;  // < 0: keep the Processor defaults

static void run(Processor& p, const std::vector<float>& xl, const std::vector<float>& xr,
                std::vector<float>& ol, std::vector<float>& orr) {
    ol.assign(xl.size(), 0.0f);
    orr.assign(xr.size(), 0.0f);
    const int chunk = 512;  // host-like block size, deliberately not the hop
    for (size_t s = 0; s < xl.size(); s += chunk) {
        int n = int(std::min<size_t>(chunk, xl.size() - s));
        p.process(&xl[s], &xr[s], &ol[s], &orr[s], n);
    }
}

static double rms(const std::vector<float>& x, size_t a, size_t b) {
    double s = 0;
    for (size_t i = a; i < b; ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(b - a));
}
static double dB(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

static Processor* make(int variant) {
    auto* p = new Processor();
    if (octaves >= 0) p->detectOctaves = octaves;
    if (ms >= 0) p->smoothingMs = ms;
    p->variant = variant;
    p->prepare(SR);
    return p;
}

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--octaves")) octaves = float(atof(argv[i + 1]));
        else if (!strcmp(argv[i], "--ms")) ms = float(atof(argv[i + 1]));
        else if (!strcmp(argv[i], "--sr")) SR = atof(argv[i + 1]);
    }
    const int L = Processor::kLatency;
    std::vector<int> levels;
    for (int l = -90; l <= 0; l += 5) levels.push_back(l);
    const size_t N = size_t(SR * 2.0);

    std::string js = "{\"sampleRate\":" + std::to_string(SR) + ",\"latency\":" + std::to_string(L) + ",\"static\":{";

    // passthrough check: intensity 0 must give the input delayed by the latency
    {
        Processor* p = make(0);
        p->intensity = 0.0f;
        std::vector<float> x(N), ol, orr;
        for (size_t i = 0; i < N; ++i) x[i] = 0.5f * std::sin(2 * M_PI * 997.0 * i / SR) + 0.2f * std::sin(2 * M_PI * 6100.0 * i / SR);
        run(*p, x, x, ol, orr);
        double err = 0;
        for (size_t i = L + 1024; i < N; ++i) err = std::max(err, double(std::fabs(ol[i] - x[i - L])));
        fprintf(stderr, "passthrough max error: %.2e\n", err);
        delete p;
    }

    const char* keys[] = {"default", "up", "down"};
    for (int v = 0; v < kTableVariants; ++v) {
        Processor* p = make(v);
        js += std::string(v ? "," : "") + "\"" + keys[v] + "\":{\"L\":[";
        for (size_t li = 0; li < levels.size(); ++li) {
            js += li ? ",[" : "[";
            for (int fi = 0; fi < kTableFreqs; ++fi) {
                double amp = std::pow(10.0, levels[li] / 20.0), f = kTableFreqHz[fi];
                std::vector<float> x(N), ol, orr;
                for (size_t i = 0; i < N; ++i) x[i] = float(amp * std::sin(2 * M_PI * f * i / SR));
                p->reset();
                run(*p, x, x, ol, orr);
                size_t a = N - size_t(SR / 2);
                char buf[32];
                snprintf(buf, sizeof buf, "%s%.2f", fi ? "," : "", dB(rms(ol, a, N)) - dB(rms(x, a, N)));
                js += buf;
            }
            js += "]";
        }
        js += "]}";
        delete p;
    }
    js += "},\"timing\":{";

    // level steps -60 -> -20 -> -60 dBFS, gain per 64-sample block (4-block window)
    {
        Processor* p = make(0);
        const double fs[] = {1000.0, 8000.0};
        for (int t = 0; t < 2; ++t) {
            size_t n = size_t(SR * 3.5);
            std::vector<float> x(n), ol, orr;
            for (size_t i = 0; i < n; ++i) {
                double lev = (i >= size_t(SR) && i < size_t(2 * SR)) ? -20.0 : -60.0;
                x[i] = float(std::pow(10.0, lev / 20.0) * std::sin(2 * M_PI * fs[t] * i / SR));
            }
            p->reset();
            run(*p, x, x, ol, orr);
            js += std::string(t ? "," : "") + "\"" + std::to_string(int(fs[t])) + "\":{\"blockSec\":" + std::to_string(64.0 / SR) + ",\"gainDb\":[";
            bool first = true;
            for (size_t s = 0; s + 256 <= n - L; s += 64) {
                double num = 0, den = 0;
                for (size_t i = s; i < s + 256; ++i) { num += double(ol[i + L]) * ol[i + L]; den += double(x[i]) * x[i]; }
                char buf[32];
                snprintf(buf, sizeof buf, "%s%.2f", first ? "" : ",", 10.0 * std::log10(num / den));
                js += buf;
                first = false;
            }
            js += "]}";
        }
        delete p;
    }
    js += "},\"noise\":{";

    // pink noise transfer per 1/3 octave at -40 and -20 dBFS RMS
    {
        size_t n = size_t(SR * 4.0);
        std::vector<float> pink(n);
        unsigned seed = 12345;
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (size_t i = 0; i < n; ++i) {
            seed = seed * 1103515245u + 12345u;
            double w = (seed / 4294967296.0) * 2 - 1;
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            pink[i] = float(b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
            b6 = w * 0.115926;
        }
        double pr = rms(pink, 0, n);
        const int nl[] = {-40, -20};
        Processor* p = make(0);
        for (int li = 0; li < 2; ++li) {
            std::vector<float> x(n), ol, orr;
            double g = std::pow(10.0, nl[li] / 20.0) / pr;
            for (size_t i = 0; i < n; ++i) x[i] = float(pink[i] * g);
            p->reset();
            run(*p, x, x, ol, orr);
            js += std::string(li ? "," : "") + "\"" + std::to_string(nl[li]) + "\":[";
            for (int fi = 0; fi < kTableFreqs; ++fi) {
                // band-pass both (RBJ, Q 4.32) and compare energy over the last 2 s
                auto bp = [&](const std::vector<float>& in, size_t off) {
                    double f0 = kTableFreqHz[fi], w0 = 2 * M_PI * f0 / SR, al = std::sin(w0) / (2 * 4.32), a0 = 1 + al;
                    double c0 = al / a0, c2 = -al / a0, a1 = -2 * std::cos(w0) / a0, a2 = (1 - al) / a0;
                    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0;
                    for (size_t i = off; i < off + n - L; ++i) {
                        double v = c0 * in[i] + c2 * x2 - a1 * y1 - a2 * y2;
                        x2 = x1; x1 = in[i]; y2 = y1; y1 = v;
                        if (i - off >= n - L - size_t(2 * SR)) e += v * v;
                    }
                    return e;
                };
                char buf[32];
                snprintf(buf, sizeof buf, "%s%.2f", fi ? "," : "", 10.0 * std::log10(bp(ol, L) / bp(x, 0)));
                js += buf;
            }
            js += "]";
        }
        delete p;
    }
    js += "}}";
    printf("%s\n", js.c_str());
    return 0;
}
