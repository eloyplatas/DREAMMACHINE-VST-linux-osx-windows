#include "Wavetables.h"
#include <algorithm>
#include <mutex>

using cd = std::complex<double>;
static constexpr double kPi = 3.14159265358979323846;

// --------------------------------------------------------------------------
// FFT radix-2 iterativa (tamaño potencia de 2)
void wavetables::fft (std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / (double) len * (inverse ? 1.0 : -1.0);
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

// --------------------------------------------------------------------------
float WaveTable::read (double phase, float pos, double maxHarm) const noexcept
{
    if (numFrames <= 0) return 0.0f;
    int mip = 0;
    if (maxHarm < (double) H)
    {
        const double m = std::ceil (std::log2 ((double) H / std::max (maxHarm, 1.0)));
        mip = std::clamp ((int) m, 0, MIPS - 1);
    }
    // la fase puede llegar a valer exactamente 1.0 por redondeo (p. ej. con FM que cruza cero)
    if (phase >= 1.0 || phase < 0.0) phase -= std::floor (phase);
    const double idx = phase * N;
    int i0 = (int) idx;
    if (i0 >= N) i0 = N - 1;
    if (i0 < 0) i0 = 0;
    const float fr = (float) (idx - i0);

    const float fp = std::clamp (pos, 0.0f, 1.0f) * (float) (numFrames - 1);
    const int f0 = (int) fp;
    const int f1 = std::min (f0 + 1, numFrames - 1);
    const float ff = fp - (float) f0;

    const float* a = frame (f0, mip);
    const float va = a[i0] + fr * (a[i0 + 1] - a[i0]);
    if (f1 == f0 || ff <= 0.0f) return va;
    const float* b = frame (f1, mip);
    const float vb = b[i0] + fr * (b[i0 + 1] - b[i0]);
    return va + ff * (vb - va);
}

std::shared_ptr<WaveTable> WaveTable::fromSpectra (const std::string& name,
                                                   const std::vector<std::vector<cd>>& spectra)
{
    auto t = std::make_shared<WaveTable>();
    t->name = name;
    t->numFrames = (int) spectra.size();
    t->data.assign ((size_t) t->numFrames * MIPS * (N + 1), 0.0f);

    std::vector<cd> buf (N);
    for (int f = 0; f < t->numFrames; ++f)
    {
        const auto& sp = spectra[(size_t) f];
        double norm = 1.0;
        for (int m = 0; m < MIPS; ++m)
        {
            const int cap = H >> m;
            std::fill (buf.begin(), buf.end(), cd (0, 0));
            for (int h = 1; h <= cap && h < (int) sp.size() && h < H; ++h)
            {
                buf[(size_t) h] = sp[(size_t) h];
                buf[(size_t) (N - h)] = std::conj (sp[(size_t) h]);
            }
            wavetables::fft (buf, true);
            if (m == 0)
            {
                double peak = 0.0;
                for (auto& x : buf) peak = std::max (peak, std::abs (x.real()));
                norm = peak > 1e-12 ? 0.95 / peak : 1.0;
            }
            float* dst = t->data.data() + ((size_t) f * MIPS + (size_t) m) * (N + 1);
            for (int i = 0; i < N; ++i) dst[i] = (float) (buf[(size_t) i].real() * norm);
            dst[N] = dst[0];
        }
    }
    return t;
}

std::shared_ptr<WaveTable> WaveTable::fromCycles (const std::string& name,
                                                  const std::vector<std::vector<float>>& cycles)
{
    std::vector<std::vector<cd>> spectra;
    std::vector<cd> buf (N);
    for (auto& c : cycles)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = cd (i < (int) c.size() ? c[(size_t) i] : 0.0f, 0.0);
        wavetables::fft (buf, false);
        std::vector<cd> sp (H + 1);
        for (int h = 1; h <= H; ++h) sp[(size_t) h] = buf[(size_t) h];
        spectra.push_back (std::move (sp));
    }
    return fromSpectra (name, spectra);
}

// --------------------------------------------------------------------------
// Generadores de espectros
namespace
{
    constexpr int FR = 16;   // frames por tabla interna

    using Spec = std::vector<cd>;
    Spec emptySpec() { return Spec (WaveTable::H + 1, cd (0, 0)); }

    // armónico en fase seno
    inline void put (Spec& s, int h, double a) { if (h >= 1 && h <= WaveTable::H) s[(size_t) h] = cd (0.0, -a); }

    bool smoothOver (int n, std::initializer_list<int> ps)
    {
        for (int p : ps) while (n % p == 0) n /= p;
        return n == 1;
    }

    double gauss (double x, double mu, double sigma) { const double d = (x - mu) / sigma; return std::exp (-0.5 * d * d); }

    template <typename F>
    std::shared_ptr<WaveTable> makeSpectral (const std::string& name, F&& fn, int frames = FR)
    {
        std::vector<Spec> specs;
        for (int f = 0; f < frames; ++f)
        {
            const double t = frames > 1 ? (double) f / (frames - 1) : 0.0;
            Spec s = emptySpec();
            fn (s, t);
            specs.push_back (std::move (s));
        }
        return WaveTable::fromSpectra (name, specs);
    }

    template <typename F>
    std::shared_ptr<WaveTable> makeTemporal (const std::string& name, F&& fn, int frames = FR)
    {
        std::vector<std::vector<float>> cyc;
        for (int f = 0; f < frames; ++f)
        {
            const double t = frames > 1 ? (double) f / (frames - 1) : 0.0;
            std::vector<float> c (WaveTable::N);
            for (int i = 0; i < WaveTable::N; ++i) c[(size_t) i] = (float) fn ((double) i / WaveTable::N, t);
            cyc.push_back (std::move (c));
        }
        return WaveTable::fromCycles (name, cyc);
    }

    // formantes de vocales (Hz) para una voz grave de referencia
    struct Vowel { double f1, f2, f3; };
    const Vowel vowels[] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 },
                             { 450, 800, 2830 }, { 325, 700, 2530 } };

    std::vector<std::shared_ptr<WaveTable>> buildAll()
    {
        std::vector<std::shared_ptr<WaveTable>> v;
        const int H = WaveTable::H;

        // 1. Armónicos 7·3·2: solo armónicos que se factorizan con 2, 3 y 7
        v.push_back (makeSpectral ("YOUNG 7-3-2", [H] (Spec& s, double t) {
            const double slope = 2.0 - 1.4 * t;
            for (int h = 1; h <= H; ++h)
                if (smoothOver (h, { 2, 3, 7 })) put (s, h, 1.0 / std::pow (h, slope));
        }));

        // 2. Primo 31: familia 2·3·7·31 con énfasis creciente en múltiplos de 31
        v.push_back (makeSpectral ("PRIME 31", [H] (Spec& s, double t) {
            for (int h = 1; h <= H; ++h)
                if (smoothOver (h, { 2, 3, 7, 31 }))
                {
                    double a = 1.0 / std::pow (h, 1.6 - 0.6 * t);
                    if (h % 31 == 0) a *= 1.0 + 6.0 * t;
                    put (s, h, a);
                }
        }));

        // 3. Sierra -> cuadrada
        v.push_back (makeSpectral ("SAW > SQUARE", [H] (Spec& s, double t) {
            for (int h = 1; h <= H; ++h) put (s, h, (h % 2 == 0 ? (1.0 - t) : 1.0) / h);
        }));

        // 4. Voz (vocales A E I O U), referencia 110 Hz: el drone vocal de Zazeela
        v.push_back (makeSpectral ("VOICE AEIOU", [H] (Spec& s, double t) {
            const double p = t * 4.0;
            const int i0 = std::min ((int) p, 3);
            const double fr = p - i0;
            const Vowel a = vowels[i0], b = vowels[i0 + 1];
            const double f1 = a.f1 + fr * (b.f1 - a.f1), f2 = a.f2 + fr * (b.f2 - a.f2), f3 = a.f3 + fr * (b.f3 - a.f3);
            for (int h = 1; h <= std::min (H, 160); ++h)
            {
                const double f = 110.0 * h;
                const double env = gauss (f, f1, 90) + 0.6 * gauss (f, f2, 120) + 0.3 * gauss (f, f3, 160) + 0.02;
                put (s, h, env / std::sqrt ((double) h));
            }
        }));

        // 5. Cuerda frotada (viola/violín de Cale y Conrad)
        v.push_back (makeSpectral ("BOWED STRING", [H] (Spec& s, double t) {
            for (int h = 1; h <= H; ++h)
            {
                const double body = 1.0 + 1.5 * gauss (h, 4, 1.5) + 1.0 * gauss (h, 9, 2.5) + 0.7 * gauss (h, 17, 4);
                put (s, h, body / std::pow (h, 1.25 - 0.45 * t));
            }
        }));

        // 6. Órgano (tiradores)
        v.push_back (makeSpectral ("ORGAN", [] (Spec& s, double t) {
            const int hs[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
            const double a0[] = { 1, .8, .2, .5, .1, .3, .0, .1 };
            const double a1[] = { 1, .3, .9, .2, .7, .2, .6, .5 };
            for (int i = 0; i < 8; ++i) put (s, hs[i], a0[i] + t * (a1[i] - a0[i]));
        }));

        // 7. Impares -> pares
        v.push_back (makeSpectral ("ODD > EVEN", [H] (Spec& s, double t) {
            for (int h = 1; h <= H; ++h)
            {
                const double w = (h == 1) ? 1.0 : (h % 2 ? 1.0 - t : t);
                put (s, h, w / std::pow (h, 1.1));
            }
        }));

        // 8. Barrido de armónico: un parcial resaltado sube del 1 al 32
        v.push_back (makeSpectral ("HARMONIC SWEEP", [] (Spec& s, double t) {
            const double c = std::pow (32.0, t);
            put (s, 1, 0.5);
            for (int h = 2; h <= 64; ++h) put (s, h, gauss (std::log2 ((double) h), std::log2 (c), 0.12));
        }));

        // 9. Saxo sopranino (caña, impares, formante ~1.6 kHz sobre 220 Hz)
        v.push_back (makeSpectral ("SOPRANINO", [H] (Spec& s, double t) {
            for (int h = 1; h <= std::min (H, 90); ++h)
            {
                const double f = 220.0 * h;
                const double odd = (h % 2) ? 1.0 : 0.35 + 0.4 * t;
                const double form = 0.3 + gauss (f, 1600 + 900 * t, 500);
                put (s, h, odd * form / std::pow (h, 0.6));
            }
        }));

        // 10. Tanpura (jawari: zona brillante que recorre el espectro)
        v.push_back (makeSpectral ("TANPURA", [H] (Spec& s, double t) {
            const double c = 6.0 + 40.0 * t;
            for (int h = 1; h <= std::min (H, 256); ++h)
                put (s, h, (1.0 + 4.0 * gauss (h, c, 3.0 + 8.0 * t)) / std::pow (h, 0.8));
        }));

        // 11. Seno plegado (wavefolder)
        v.push_back (makeTemporal ("SINE FOLD", [] (double ph, double t) {
            const double x = std::sin (2.0 * kPi * ph) * (1.0 + 5.0 * t);
            return std::sin (x * kPi * 0.5);
        }));

        // 12. Pulso PWM (ancho 50% -> 5%)
        v.push_back (makeSpectral ("PULSE WIDTH", [H] (Spec& s, double t) {
            const double w = 0.5 - 0.45 * t;
            for (int h = 1; h <= H; ++h)
                s[(size_t) h] = cd (std::sin (2 * kPi * h * w) / h, -(1 - std::cos (2 * kPi * h * w)) / h);
        }));

        return v;
    }

    std::vector<std::shared_ptr<WaveTable>> buildBasic()
    {
        const int H = WaveTable::H;
        std::vector<std::shared_ptr<WaveTable>> b;
        b.push_back (makeSpectral ("SINE", [] (Spec& s, double) { put (s, 1, 1.0); }, 1));
        b.push_back (makeSpectral ("TRIANGLE", [H] (Spec& s, double) {
            for (int h = 1; h <= H; h += 2) put (s, h, ((h / 2) % 2 ? -1.0 : 1.0) / (double) (h * h));
        }, 1));
        b.push_back (makeSpectral ("SQUARE", [H] (Spec& s, double) {
            for (int h = 1; h <= H; h += 2) put (s, h, 1.0 / h);
        }, 1));
        b.push_back (makeSpectral ("SAW", [H] (Spec& s, double) {
            for (int h = 1; h <= H; ++h) put (s, h, 1.0 / h);
        }, 1));
        return b;
    }

    std::once_flag gOnce;
    std::vector<std::shared_ptr<WaveTable>> gBuiltIn, gBasic;
    void ensure() { std::call_once (gOnce, [] { gBuiltIn = buildAll(); gBasic = buildBasic(); }); }
}

const std::vector<std::shared_ptr<WaveTable>>& wavetables::builtIn() { ensure(); return gBuiltIn; }
const std::shared_ptr<WaveTable>& wavetables::basic (int which) { ensure(); return gBasic[(size_t) std::clamp (which, 0, 3)]; }

std::vector<std::string> wavetables::builtInNames()
{
    return { "YOUNG 7-3-2", "PRIME 31", "SAW > SQUARE", "VOICE AEIOU", "BOWED STRING", "ORGAN",
             "ODD > EVEN", "HARMONIC SWEEP", "SOPRANINO", "TANPURA", "SINE FOLD", "PULSE WIDTH" };
}

std::shared_ptr<WaveTable> wavetables::loadFromSamples (const std::string& name, const float* x, int len)
{
    const int N = WaveTable::N;
    std::vector<std::vector<float>> cycles;
    if (len <= 0) return nullptr;
    if (len % N == 0 || len > 8192)
    {
        const int frames = std::min (256, std::max (1, len / N));
        for (int f = 0; f < frames; ++f)
            cycles.emplace_back (x + (size_t) f * N, x + (size_t) f * N + N);
    }
    else
    {
        // ciclo único: remuestrear a N
        std::vector<float> c (N);
        for (int i = 0; i < N; ++i)
        {
            const double p = (double) i * len / N;
            const int i0 = (int) p;
            const int i1 = (i0 + 1) % len;
            const double fr = p - i0;
            c[(size_t) i] = (float) (x[i0] + fr * (x[i1] - x[i0]));
        }
        cycles.push_back (std::move (c));
    }
    return WaveTable::fromCycles (name, cycles);
}
