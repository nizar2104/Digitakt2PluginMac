// Port of the web panel's factory sound synthesis (src/lib/dt-samples.ts).
#include "Samples.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <functional>

namespace dt2
{
namespace
{
    constexpr double SR = SAMPLE_RATE;
    constexpr double PI = 3.14159265358979323846;

    struct Rng
    {
        uint32_t s;
        explicit Rng (uint32_t seed) : s (seed) {}
        double operator()() { s = s * 1664525u + 1013904223u; return ((double) s / 4294967296.0) * 2.0 - 1.0; }
    };

    struct Svf
    {
        double lp = 0, bp = 0, hp = 0;
        Svf& operator() (double x, double f, double q)
        {
            const double F = 2.0 * std::sin ((PI * std::min (f, SR / 6.0)) / SR);
            hp = x - lp - q * bp;
            bp += F * hp;
            lp += F * bp;
            return *this;
        }
    };

    double sgn (double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }
    double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
    double saw (double ph) { return 2.0 * (ph - std::floor (ph + 0.5)); }
    double fmod1 (double x) { return x - std::floor (x); }

    double metal (double t)
    {
        static const double fr[] = { 205.3, 304.4, 369.6, 522.7, 540, 800 };
        double s = 0;
        for (double f : fr) s += sgn (std::sin (2 * PI * f * 1.7 * t));
        return s / 6.0;
    }

    using Gen = std::function<void (std::vector<float>&, std::vector<float>&)>;

    SamplePtr mk (const char* name, double seconds, const Gen& gen)
    {
        auto s = std::make_shared<Sample>();
        s->name = name;
        const int n = (int) std::floor (seconds * SR);
        s->L.assign ((size_t) n, 0.0f);
        s->R.assign ((size_t) n, 0.0f);
        gen (s->L, s->R);
        float peak = 0;
        for (int i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (s->L[(size_t) i]), std::abs (s->R[(size_t) i]) });
        const float g = peak > 0 ? 0.95f / peak : 1.0f;
        const int fade = std::min (480, n);
        for (int i = 0; i < n; ++i)
        {
            const float f = i > n - fade ? (float) (n - i) / (float) fade : 1.0f;
            s->L[(size_t) i] *= g * f;
            s->R[(size_t) i] *= g * f;
        }
        return s;
    }

    Gen mono (std::function<double (int, double)> fn)
    {
        return [fn] (std::vector<float>& L, std::vector<float>& R) {
            for (size_t i = 0; i < L.size(); ++i)
            {
                const auto v = (float) fn ((int) i, (double) i / SR);
                L[i] = v;
                R[i] = v;
            }
        };
    }

    SamplePtr kick (const char* name, double len, double f0, double f1, double drop, double dec, double click, double drive)
    {
        auto r = std::make_shared<Rng> (7);
        auto ph = std::make_shared<double> (0.0);
        return mk (name, len, mono ([=] (int, double t) {
            const double f = f1 + (f0 - f1) * std::exp (-t * drop);
            *ph += f / SR;
            const double body = std::sin (2 * PI * *ph) * std::exp (-t * dec);
            const double cl = (*r)() * std::exp (-t * 900) * click;
            return std::tanh ((body + cl) * drive);
        }));
    }

    SamplePtr snare (const char* name, double tone, double noiseAmt, double dec, uint32_t seed)
    {
        auto r = std::make_shared<Rng> (seed);
        auto f = std::make_shared<Svf>();
        auto p1 = std::make_shared<double> (0.0), p2 = std::make_shared<double> (0.0);
        return mk (name, 0.45, mono ([=] (int, double t) {
            const double fr = tone * (1 + 0.5 * std::exp (-t * 60));
            *p1 += fr / SR;
            *p2 += (fr * 1.62) / SR;
            const double body = (std::sin (2 * PI * *p1) * 0.7 + std::sin (2 * PI * *p2) * 0.3) * std::exp (-t * 28);
            const double n = (*f) ((*r)(), 4200, 0.9).bp * 2.2 * std::exp (-t * dec) * noiseAmt;
            return std::tanh (body + n);
        }));
    }

    SamplePtr hat (const char* name, double len, double dec, uint32_t seed)
    {
        auto r = std::make_shared<Rng> (seed);
        auto f = std::make_shared<Svf>();
        return mk (name, len, mono ([=] (int, double t) {
            const double x = metal (t) * 0.6 + (*r)() * 0.5;
            return (*f) (x, 9000, 0.5).hp * std::exp (-t * dec);
        }));
    }

    SamplePtr chordStab (const char* name, std::vector<double> notes, double len, double cutoff, double dec, double attack = 0.002)
    {
        return mk (name, len, [=] (std::vector<float>& L, std::vector<float>& R) {
            Svf fl, fr;
            std::vector<std::array<double, 2>> phs (notes.size(), std::array<double, 2> { 0.0, 0.37 });
            for (size_t i = 0; i < L.size(); ++i)
            {
                const double t = (double) i / SR;
                double l = 0, rr = 0;
                for (size_t k = 0; k < notes.size(); ++k)
                {
                    const double f = midiHz (notes[k]);
                    phs[k][0] = fmod1 (phs[k][0] + (f * 0.996) / SR);
                    phs[k][1] = fmod1 (phs[k][1] + (f * 1.004) / SR);
                    l += saw (phs[k][0]);
                    rr += saw (phs[k][1]);
                }
                const double env = std::min (1.0, t / attack) * std::exp (-t * dec);
                const double c = cutoff * (0.35 + 0.65 * std::exp (-t * 6));
                L[i] = (float) (fl (l / (double) notes.size(), c, 0.6).lp * env);
                R[i] = (float) (fr (rr / (double) notes.size(), c, 0.6).lp * env);
            }
        });
    }
} // namespace

Pool makeFactoryPool()
{
    Pool list;
    list.push_back (kick ("BD DEEP", 0.9, 165, 46, 34, 5.5, 0.35, 1.6));
    list.push_back (snare ("SD CRISP", 190, 1, 18, 3));
    {
        auto r = std::make_shared<Rng> (11);
        auto f = std::make_shared<Svf>();
        list.push_back (mk ("CP CLASSIC", 0.6, mono ([=] (int, double t) {
            static const double bursts[] = { 0, 0.011, 0.023, 0.034 };
            double env = 0;
            for (double b : bursts)
                if (t >= b) env = std::max (env, std::exp (-(t - b) * (b == 0.034 ? 14.0 : 180.0)));
            return (*f) ((*r)(), 1150, 0.55).bp * env * 2.4;
        })));
    }
    {
        auto r = std::make_shared<Rng> (5);
        auto p = std::make_shared<double> (0.0);
        list.push_back (mk ("RS RIM", 0.12, mono ([=] (int, double t) {
            *p += 1720 / SR;
            return (std::sin (2 * PI * *p) * 0.8 + (*r)() * 0.4 * std::exp (-t * 400)) * std::exp (-t * 55);
        })));
    }
    list.push_back (hat ("CH TIGHT", 0.18, 38, 21));
    list.push_back (hat ("OH AIRY", 0.9, 5.2, 22));
    {
        auto r = std::make_shared<Rng> (23);
        auto f = std::make_shared<Svf>();
        list.push_back (mk ("CY RIDE", 2.2, mono ([=] (int, double t) {
            const double x = metal (t * 1.13) * 0.5 + (*r)() * 0.3 + std::sin (2 * PI * 3100 * t) * 0.2;
            return (*f) (x, 7000, 0.7).hp * std::exp (-t * 1.9);
        })));
    }
    {
        auto f = std::make_shared<Svf>();
        list.push_back (mk ("CB BELL", 0.5, mono ([=] (int, double t) {
            const double x = sgn (std::sin (2 * PI * 540 * t)) + sgn (std::sin (2 * PI * 800 * t));
            return (*f) (x * 0.5, 2600, 0.4).bp * (0.6 * std::exp (-t * 60) + 0.4 * std::exp (-t * 9));
        })));
    }
    auto tom = [] (const char* name, double f0, double f1) {
        auto p = std::make_shared<double> (0.0);
        auto r = std::make_shared<Rng> (9);
        return mk (name, 0.7, mono ([=] (int, double t) {
            const double f = f1 + (f0 - f1) * std::exp (-t * 18);
            *p += f / SR;
            return std::sin (2 * PI * *p) * std::exp (-t * 6.5) + (*r)() * 0.08 * std::exp (-t * 200);
        }));
    };
    list.push_back (tom ("LT FLOOR", 120, 82));
    list.push_back (tom ("HT RACK", 230, 165));
    {
        auto r = std::make_shared<Rng> (31);
        auto f = std::make_shared<Svf>();
        list.push_back (mk ("SH SHAKER", 0.16, mono ([=] (int, double t) {
            return (*f) ((*r)(), 8000, 0.7).hp * std::min (1.0, t / 0.018) * std::exp (-t * 30);
        })));
    }
    {
        auto p = std::make_shared<double> (0.0);
        list.push_back (mk ("CL CLAVE", 0.12, mono ([=] (int, double t) {
            *p += 2480 / SR;
            return std::sin (2 * PI * *p) * std::exp (-t * 40);
        })));
    }
    {
        auto f = std::make_shared<Svf>();
        auto p = std::make_shared<double> (0.0);
        const double fr = midiHz (36);
        list.push_back (mk ("BS ACID C2", 0.8, mono ([=] (int, double t) {
            *p = fmod1 (*p + fr / SR);
            const double x = saw (*p) * 0.7 + std::sin (2 * PI * *p) * 0.5;
            return (*f) (x, 2200, 0.35).lp * std::exp (-t * 2.2);
        })));
    }
    list.push_back (chordStab ("ST MIN7 C3", { 48, 51, 55, 58 }, 0.9, 3800, 3.4));
    {
        const int N = (int) std::round (SR / midiHz (48));
        auto buf = std::make_shared<std::vector<double>> ((size_t) N);
        Rng r (41);
        for (auto& v : *buf) v = r();
        auto idx = std::make_shared<int> (0);
        list.push_back (mk ("PL PLUCK C3", 1.4, mono ([=] (int, double) {
            const double a = (*buf)[(size_t) *idx];
            const double b = (*buf)[(size_t) ((*idx + 1) % N)];
            (*buf)[(size_t) *idx] = (a + b) * 0.4985;
            *idx = (*idx + 1) % N;
            return a;
        })));
    }
    list.push_back (chordStab ("PD WARM C3", { 48, 55, 58, 63, 67 }, 2.6, 2400, 0.7, 0.35));
    list.push_back (kick ("BD BOOM", 1.6, 120, 43, 22, 1.8, 0.15, 1.15));
    {
        auto p = std::make_shared<double> (0.0);
        list.push_back (mk ("ZP ZAP", 0.25, mono ([=] (int, double t) {
            *p += (90 + 2600 * std::exp (-t * 34)) / SR;
            return std::sin (2 * PI * *p) * std::exp (-t * 14);
        })));
    }
    {
        const double fr = midiHz (60);
        list.push_back (mk ("BL FM C4", 1.8, mono ([=] (int, double t) {
            const double mod = std::sin (2 * PI * fr * 3.5 * t) * 3.2 * std::exp (-t * 4);
            return std::sin (2 * PI * fr * t + mod) * std::exp (-t * 2.4);
        })));
    }
    {
        auto r = std::make_shared<Rng> (51);
        auto f = std::make_shared<Svf>();
        list.push_back (mk ("NS RISER", 1.2, mono ([=] (int, double t) {
            return (*f) ((*r)(), 300 + 7000 * std::pow (t / 1.2, 2.0), 0.25).bp * std::min (1.0, t * 3) * (t > 1.1 ? (1.2 - t) * 10 : 1.0);
        })));
    }
    {
        const double fr = midiHz (48);
        auto f1 = std::make_shared<Svf>(), f2 = std::make_shared<Svf>();
        auto p = std::make_shared<double> (0.0);
        list.push_back (mk ("VX AH C3", 0.9, mono ([=] (int, double t) {
            *p = fmod1 (*p + (fr * (1 + 0.006 * std::sin (2 * PI * 5.5 * t))) / SR);
            const double x = saw (*p);
            return ((*f1) (x, 750, 0.12).bp + 0.6 * (*f2) (x, 1150, 0.12).bp) * std::min (1.0, t * 25) * std::exp (-t * 2.5);
        })));
    }
    list.push_back (snare ("SD LOFI", 160, 0.8, 11, 4));
    {
        auto r = std::make_shared<Rng> (61);
        auto p = std::make_shared<double> (0.0);
        list.push_back (mk ("TK TICK", 0.08, mono ([=] (int, double t) {
            *p += 3400 / SR;
            return (std::sin (2 * PI * *p + std::sin (2 * PI * 900 * t) * 2) + (*r)() * 0.2) * std::exp (-t * 90);
        })));
    }
    {
        auto f = std::make_shared<Svf>();
        auto p = std::make_shared<double> (0.0);
        const double fr = midiHz (36);
        list.push_back (mk ("BS SUB C2", 1.0, mono ([=] (int, double t) {
            *p = fmod1 (*p + fr / SR);
            return (*f) (std::tanh (std::sin (2 * PI * *p) * 1.8), 500, 0.6).lp * std::exp (-t * 1.6);
        })));
    }
    for (size_t i = 0; i < list.size(); ++i)
        const_cast<Sample&> (*list[i]).id = "factory-" + juce::String ((int) i);
    return list;
}
} // namespace dt2
