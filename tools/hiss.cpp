// Measures how much energy above 6 kHz EarComp outputs for signals with little or none there:
// silence, a faint pink-noise floor, low tones, low tones + noise floor. Prints dBFS per channel.
// usage: hiss [rightTrebleDb] [hissGuardDb]
#include "Processor.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static const double SR = 48000.0;

static double hfDb(const std::vector<float>& x, size_t from) {
    // two cascaded RBJ high-passes at 6 kHz (Q 0.707), RMS of the result
    double w0 = 2 * M_PI * 6000.0 / SR, al = std::sin(w0) / (2 * 0.7071), c = std::cos(w0), a0 = 1 + al;
    double b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, b2 = b0, a1 = -2 * c / a0, a2 = (1 - al) / a0;
    double s[2][4] = {};
    double e = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        double v = x[i];
        for (auto& st : s) {
            double y = b0 * v + b1 * st[0] + b2 * st[1] - a1 * st[2] - a2 * st[3];
            st[1] = st[0]; st[0] = v; st[3] = st[2]; st[2] = y; v = y;
        }
        if (i >= from) e += v * v;
    }
    return 10 * std::log10(std::max(e / double(x.size() - from), 1e-30));
}

int main(int argc, char** argv) {
    float rt = argc > 1 ? float(atof(argv[1])) : 0.0f;
    float guard = argc > 2 ? float(atof(argv[2])) : -100.0f;
    const size_t N = size_t(SR * 3);
    std::vector<float> pink(N);
    unsigned seed = 4242;
    double b[7] = {};
    for (size_t i = 0; i < N; ++i) {
        seed = seed * 1103515245u + 12345u;
        double w = (seed / 4294967296.0) * 2 - 1;
        b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759; b[2] = 0.96900 * b[2] + w * 0.1538520;
        b[3] = 0.86650 * b[3] + w * 0.3104856; b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980;
        pink[i] = float(b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362);
        b[6] = w * 0.115926;
    }
    double pr = 0; for (float v : pink) pr += v * v; pr = std::sqrt(pr / N);
    auto noise = [&](double db) { std::vector<float> x(N); double g = std::pow(10, db / 20) / pr; for (size_t i = 0; i < N; ++i) x[i] = float(pink[i] * g); return x; };
    auto tones = [&]() { std::vector<float> x(N); double a = std::pow(10, -24 / 20.0);
        for (size_t i = 0; i < N; ++i) x[i] = float(a * (std::sin(2 * M_PI * 220 * i / SR) + std::sin(2 * M_PI * 440 * i / SR) + std::sin(2 * M_PI * 880 * i / SR))); return x; };
    auto add = [](std::vector<float> a, const std::vector<float>& b) { for (size_t i = 0; i < a.size(); ++i) a[i] += b[i]; return a; };
    struct Case { const char* name; std::vector<float> x; };
    std::vector<Case> cases = {{"silence", std::vector<float>(N)}, {"pink -70", noise(-70)}, {"pink -50", noise(-50)},
                               {"tones", tones()}, {"tones+pink-70", add(tones(), noise(-70))}};
    for (auto& c : cases) {
        earcomp::Processor p;
        p.rightTrebleDb = rt;
        p.hissGuardDb = guard;
        p.prepare(SR);
        std::vector<float> l(N), r(N);
        for (size_t s = 0; s < N; s += 512) { int n = int(std::min<size_t>(512, N - s)); p.process(&c.x[s], &c.x[s], &l[s], &r[s], n); }
        size_t from = size_t(SR);
        printf("%-14s in %7.1f   out L %7.1f  R %7.1f  dBFS above 6 kHz\n", c.name, hfDb(c.x, from), hfDb(l, from), hfDb(r, from));
    }
}
