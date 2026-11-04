#pragma once
// Efecto seleccionable antes de la reverb:
// chorus, phaser, delay, tape delay, granulador, sub bass, resonador y (1.2, a partir de las ideas
// del firmware del Deluge) flanger armónico, dimension y warbler.
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <random>
#include "Dsp2.h"

class DreamFx
{
public:
    struct Context
    {
        double base = 55.0;                 // frecuencia de la fundamental
        std::array<double, 5> freqs {};     // frecuencias de las voces
        std::array<bool, 5> active {};
        float fundEnv = 0.0f;               // envolvente de la fundamental
    };

    void prepare (double rate)
    {
        sr = rate;
        const size_t big = (size_t) (sr * 4.2) + 8;
        for (auto& b : dline) { b.assign (big, 0.0f); }
        dw = 0;
        gbuf.assign ((size_t) (sr * 4.0) + 8, 0.0f); gw = 0;
        for (auto& c : combs) for (auto& b : c) b.assign ((size_t) (sr / 15.0) + 8, 0.0f);
        cw = 0;
        for (auto& g : grains) g.active = false;
        for (auto& a : ap) a = 0.0f;
        for (auto& z : combLp) z = 0.0f;
        lfo = 0; wow = 0; flut = 0; subPh = 0; fbL = fbR = 0; lpL = lpR = 0; phFb[0] = phFb[1] = 0;
        grainTimer = 0;
    }

    // limpia sin reservar memoria (se llama desde el hilo de audio al cambiar de tipo)
    void reset()
    {
        for (auto& b : dline) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (gbuf.begin(), gbuf.end(), 0.0f);
        for (auto& c : combs) for (auto& b : c) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& g : grains) g.active = false;
        ap.fill (0.0f); combLp.fill (0.0f);
        lfo = wow = flut = 0; fbL = fbR = lpL = lpR = 0; phFb[0] = phFb[1] = 0; subEnv = 0;
    }

    // type: 0 off, 1 chorus, 2 phaser, 3 delay, 4 tape, 5 granulator, 6 sub, 7 resonator,
    //       8 harmonic flanger, 9 dimension, 10 warbler
    // sendL/sendR: el granulador puede mandar parte de sus granos directamente a la reverb
    void process (float* L, float* R, int n, int type, float mix, float p1, float p2, float p3, const Context& ctx,
                  float p4 = 0.0f, float p5 = 0.0f, float* sendL = nullptr, float* sendR = nullptr)
    {
        if (type != lastType) { reset(); lastType = type; }
        if (type == 0 || mix <= 0.0f) return;
        gRev = p4; gSend = p5; gSendL = sendL; gSendR = sendR;
        switch (type)
        {
            case 1: chorus (L, R, n, mix, p1, p2, p3); break;
            case 2: phaser (L, R, n, mix, p1, p2, p3); break;
            case 3: delay (L, R, n, mix, p1, p2, p3, false); break;
            case 4: delay (L, R, n, mix, p1, p2, p3, true); break;
            case 5: granulator (L, R, n, mix, p1, p2, p3); break;
            case 6: sub (L, R, n, mix, p1, p2, p3, ctx); break;
            case 7: resonator (L, R, n, mix, p1, p2, p3, ctx); break;
            case 8: harmFlanger (L, R, n, mix, p1, p2, p3, ctx); break;
            case 9: dimension (L, R, n, mix, p1, p2, p3); break;
            case 10: warbler (L, R, n, mix, p1, p2, p3); break;
            default: break;
        }
    }

private:
    static constexpr float twoPi = 6.2831853f;
    static float expMap (float x, float lo, float hi) { return lo * std::pow (hi / lo, std::clamp (x, 0.0f, 1.0f)); }

    float readD (int ch, float d) const
    {
        const auto& b = dline[(size_t) ch];
        const int sz = (int) b.size();
        float rp = (float) dw - d; while (rp < 0) rp += (float) sz;
        int i0 = (int) rp; const float fr = rp - (float) i0;
        if (i0 >= sz) i0 -= sz;                // el redondeo puede dar exactamente sz
        return b[(size_t) i0] + fr * (b[(size_t) ((i0 + 1) % sz)] - b[(size_t) i0]);
    }
    void advanceD() { dw = (dw + 1) % (int) dline[0].size(); }

    // ---------------------------------------------------------------- chorus
    void chorus (float* L, float* R, int n, float mix, float p1, float p2, float p3)
    {
        const float rate = expMap (p1, 0.03f, 5.0f);
        const float depth = (float) sr * 0.008f * p2;
        const float spread = p3;
        for (int i = 0; i < n; ++i)
        {
            dline[0][(size_t) dw] = L[i]; dline[1][(size_t) dw] = R[i];
            lfo += rate / (float) sr; if (lfo >= 1.0f) lfo -= 1.0f;
            float wl = 0, wr = 0;
            for (int v = 0; v < 3; ++v)
            {
                const float ph = lfo + v / 3.0f;
                const float m = 0.5f + 0.5f * std::sin (twoPi * ph);
                const float m2 = 0.5f + 0.5f * std::sin (twoPi * (ph + 0.25f * spread));
                wl += readD (0, (float) sr * 0.012f + depth * m);
                wr += readD (1, (float) sr * 0.012f + depth * m2);
            }
            advanceD();
            L[i] = L[i] * (1 - mix) + wl * 0.4f * mix;
            R[i] = R[i] * (1 - mix) + wr * 0.4f * mix;
        }
    }

    // ---------------------------------------------------------------- phaser
    void phaser (float* L, float* R, int n, float mix, float p1, float p2, float p3)
    {
        const float rate = expMap (p1, 0.02f, 5.0f);
        const float fb = p3 * 0.85f;
        for (int i = 0; i < n; ++i)
        {
            lfo += rate / (float) sr; if (lfo >= 1.0f) lfo -= 1.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float m = 0.5f + 0.5f * std::sin (twoPi * (lfo + c * 0.25f));
                const float f = expMap (m * p2 + (1 - p2) * 0.3f, 120.0f, 4000.0f);
                const float t = std::tan (3.14159265f * f / (float) sr);
                const float a = (t - 1.0f) / (t + 1.0f);
                float x = (c ? R[i] : L[i]) + phFb[c] * fb;
                for (int s = 0; s < 6; ++s)
                {
                    float& z = ap[(size_t) (c * 6 + s)];
                    const float y = a * x + z;
                    z = x - a * y;
                    x = y;
                }
                phFb[c] = x;
                float& o = c ? R[i] : L[i];
                o = o * (1 - mix * 0.5f) + x * mix * 0.5f;
            }
        }
    }

    // ---------------------------------------------------------------- delay / tape
    void delay (float* L, float* R, int n, float mix, float p1, float p2, float p3, bool tape)
    {
        const float time = expMap (p1, tape ? 0.05f : 0.02f, 2.0f) * (float) sr;
        const float fb = tape ? p2 * 1.05f : p2 * 0.95f;
        const float tone = tape ? 5000.0f : expMap (p3, 500.0f, 18000.0f);
        const float lpc = 1.0f - std::exp (-twoPi * tone / (float) sr);
        const float wowDepth = tape ? p3 * 0.004f * (float) sr : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float d = time;
            if (tape)
            {
                wow += 0.5f / (float) sr; if (wow >= 1) wow -= 1;
                flut += 6.3f / (float) sr; if (flut >= 1) flut -= 1;
                d += wowDepth * (0.5f + 0.5f * std::sin (twoPi * wow)) + wowDepth * 0.15f * std::sin (twoPi * flut);
            }
            float yl = readD (0, d), yr = readD (1, d);
            lpL += lpc * (yl - lpL); lpR += lpc * (yr - lpR);
            yl = lpL; yr = lpR;
            float inL = L[i] + (tape ? yl : yr) * fb;   // ping-pong en el digital
            float inR = R[i] + (tape ? yr : yl) * fb;
            if (tape) { inL = std::tanh (inL); inR = std::tanh (inR); }
            dline[0][(size_t) dw] = inL; dline[1][(size_t) dw] = inR;
            advanceD();
            L[i] = L[i] * (1 - mix * 0.5f) + yl * mix;
            R[i] = R[i] * (1 - mix * 0.5f) + yr * mix;
        }
    }

    // ---------------------------------------------------------------- granulador
    struct Grain { bool active = false; double pos = 0, inc = 1; int len = 0, age = 0; float panL = 1, panR = 1; };
    void granulator (float* L, float* R, int n, float mix, float p1, float p2, float p3)
    {
        static const double ratios[] = { 1.0, 2.0, 0.5, 1.5, 4.0 / 3.0, 7.0 / 4.0, 3.0, 0.75 };
        const int size = std::max (64, (int) (expMap (p1, 0.02f, 0.8f) * (float) sr));
        const float density = expMap (p2, 1.0f, 60.0f);
        const int gsz = (int) gbuf.size();
        for (int i = 0; i < n; ++i)
        {
            gbuf[(size_t) gw] = 0.5f * (L[i] + R[i]);
            gw = (gw + 1) % gsz;
            grainTimer -= 1.0;
            if (grainTimer <= 0.0)
            {
                grainTimer = (double) sr / density * (0.5 + uni (rng));
                for (auto& g : grains)
                    if (! g.active)
                    {
                        g.active = true; g.len = size; g.age = 0;
                        const int k = uni (rng) < p3 ? 1 + (int) (uni (rng) * 7.0f) % 7 : 0;
                        g.inc = ratios[k];
                        if (uni (rng) < gRev)
                        {
                            // grano invertido: empieza cerca del presente y lee hacia atrás
                            g.inc = -g.inc;
                            g.pos = (double) gw - 4.0 - uni (rng) * sr * 2.5;
                        }
                        else
                        {
                            const double back = (double) size * std::max (1.0, g.inc) + uni (rng) * sr * 2.5;
                            g.pos = (double) gw - back;
                        }
                        while (g.pos < 0) g.pos += gsz;
                        const float pan = uni (rng);
                        g.panL = std::cos (pan * 1.5708f); g.panR = std::sin (pan * 1.5708f);
                        break;
                    }
            }
            float wl = 0, wr = 0;
            for (auto& g : grains)
                if (g.active)
                {
                    const float w = 0.5f - 0.5f * std::cos (twoPi * (float) g.age / (float) g.len);
                    int i0 = (int) g.pos; const float fr = (float) (g.pos - i0);
                    if (i0 >= gsz) i0 -= gsz;
                    const float v = gbuf[(size_t) i0] + fr * (gbuf[(size_t) ((i0 + 1) % gsz)] - gbuf[(size_t) i0]);
                    wl += v * w * g.panL; wr += v * w * g.panR;
                    g.pos += g.inc;
                    if (g.pos >= gsz) g.pos -= gsz;
                    if (g.pos < 0) g.pos += gsz;
                    if (++g.age >= g.len) g.active = false;
                }
            L[i] = L[i] * (1 - mix) + wl * 0.6f * mix;
            R[i] = R[i] * (1 - mix) + wr * 0.6f * mix;
            // envío a la reverb: los granos suenan más lisos dentro de la cola
            if (gSendL) { gSendL[i] += wl * 0.6f * gSend; gSendR[i] += wr * 0.6f * gSend; }
        }
    }

    // ---------------------------------------------------------------- sub bass
    void sub (float* L, float* R, int n, float mix, float p1, float p2, float p3, const Context& ctx)
    {
        const int oct = p1 < 0.34f ? 1 : (p1 < 0.67f ? 2 : 3);
        double f = ctx.base / std::pow (2.0, oct);
        while (f < 16.0) f *= 2.0;
        const float level = p2 * mix;
        const float drive = 1.0f + p3 * 8.0f;
        for (int i = 0; i < n; ++i)
        {
            subPh += f / sr; subPh -= std::floor (subPh);
            subEnv += (ctx.fundEnv - subEnv) * 0.001f;
            const float s = std::tanh (std::sin (6.283185307 * subPh) * drive) / std::tanh (drive);
            L[i] += s * level * subEnv * 0.5f;
            R[i] += s * level * subEnv * 0.5f;
        }
    }

    // ---------------------------------------------------------------- flanger armónico
    // El retardo es un número entero de periodos de la fundamental: los picos del peine
    // caen exactamente en sus armónicos, así el flanger refuerza la serie en lugar de desafinarla.
    void harmFlanger (float* L, float* R, int n, float mix, float p1, float p2, float p3, const Context& ctx)
    {
        const int periods = 1 + (int) std::lround (std::clamp (p1, 0.0f, 1.0f) * 7.0f);
        const float maxD = (float) dline[0].size() - 8.0f;
        const float base = std::min (maxD * 0.5f, (float) (sr / std::max (1.0, ctx.base)) * (float) periods);
        const float fb = (std::clamp (p2, 0.0f, 1.0f) * 2.0f - 1.0f) * 0.95f;
        const float sweep = std::clamp (p3, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            lfo += 0.05f / (float) sr; if (lfo >= 1.0f) lfo -= 1.0f;
            // el barrido va de 'periods' a 'periods + 1' periodos (también armónico en los extremos)
            const float d = std::clamp (base * (1.0f + sweep * (0.5f - 0.5f * std::cos (twoPi * lfo)) / (float) periods), 2.0f, maxD);
            const float yl = readD (0, d), yr = readD (1, d);
            dline[0][(size_t) dw] = L[i] + yl * fb;
            dline[1][(size_t) dw] = R[i] + yr * fb;
            advanceD();
            L[i] = L[i] * (1 - 0.5f * mix) + yl * mix * 0.7f;
            R[i] = R[i] * (1 - 0.5f * mix) + yr * mix * 0.7f;
        }
    }

    // ---------------------------------------------------------------- dimension
    // Dos líneas cortas con LFO triangular en contrafase sobre la suma mono, como el
    // Roland Dimension D: ensancha sin el vaivén evidente de un chorus.
    void dimension (float* L, float* R, int n, float mix, float p1, float p2, float p3)
    {
        static const float rates[4]  = { 0.25f, 0.33f, 0.50f, 0.50f };
        static const float depths[4] = { 0.35f, 0.55f, 0.75f, 1.10f };   // ms
        const int mode = std::clamp ((int) (p1 * 3.999f), 0, 3);
        const float base = 0.0025f * (float) sr, dep = depths[mode] * 0.001f * (float) sr;
        const float width = std::clamp (p2, 0.0f, 1.0f);
        const float lpc = 1.0f - std::exp (-twoPi * expMap (p3, 2000.0f, 16000.0f) / (float) sr);
        for (int i = 0; i < n; ++i)
        {
            lfo += rates[mode] / (float) sr; if (lfo >= 1.0f) lfo -= 1.0f;
            const float tri = 1.0f - 4.0f * std::abs (lfo - 0.5f);   // -1..1
            const float m = 0.5f * (L[i] + R[i]);
            dline[0][(size_t) dw] = m;
            float a = readD (0, base + dep * tri), b = readD (0, base - dep * tri);
            advanceD();
            lpL += lpc * (a - lpL); lpR += lpc * (b - lpR);
            a = lpL; b = lpR;
            const float wl = a * (0.5f + 0.5f * width) + b * (0.5f - 0.5f * width);
            const float wr = b * (0.5f + 0.5f * width) + a * (0.5f - 0.5f * width);
            L[i] = L[i] * (1 - 0.5f * mix) + wl * mix;
            R[i] = R[i] * (1 - 0.5f * mix) + wr * mix;
        }
    }

    // ---------------------------------------------------------------- warbler
    // Retardo corto realimentado movido por dos LFO aleatorios suaves e independientes
    // (uno por canal): oscilación irregular de cinta. Salida solo húmeda, como en el Deluge.
    void warbler (float* L, float* R, int n, float mix, float p1, float p2, float p3)
    {
        const float rate = expMap (p1, 0.05f, 8.0f);
        const float base = 0.006f * (float) sr, dep = std::clamp (p2, 0.0f, 1.0f) * 0.005f * (float) sr;
        const float fb = std::clamp (p3, 0.0f, 1.0f) * 0.85f;
        for (int i = 0; i < n; ++i)
        {
            const float m1 = warbleA.tick (rate, (float) sr), m2 = warbleB.tick (rate * 0.97f, (float) sr);
            const float yl = readD (0, std::max (2.0f, base + dep * m1)), yr = readD (1, std::max (2.0f, base + dep * m2));
            dline[0][(size_t) dw] = L[i] + yl * fb;
            dline[1][(size_t) dw] = R[i] + yr * fb;
            advanceD();
            L[i] = L[i] * (1 - mix) + yl * mix;
            R[i] = R[i] * (1 - mix) + yr * mix;
        }
    }

    // ---------------------------------------------------------------- resonador (peines afinados a las razones)
    void resonator (float* L, float* R, int n, float mix, float p1, float p2, float p3, const Context& ctx)
    {
        const float fb = 0.6f + 0.399f * p1;
        const float lpc = 1.0f - std::exp (-twoPi * expMap (p2, 300.0f, 16000.0f) / (float) sr);
        const int oct = p3 < 0.34f ? 0 : (p3 < 0.67f ? 1 : 2);
        const int sz = (int) combs[0][0].size();
        std::array<float, 5> dl {};
        for (int v = 0; v < 5; ++v)
        {
            double f = ctx.freqs[(size_t) v] / std::pow (2.0, oct);
            while (f < 15.0) f *= 2.0;
            dl[(size_t) v] = std::min ((float) (sr / f), (float) sz - 2.0f);
        }
        for (int i = 0; i < n; ++i)
        {
            float wl = 0, wr = 0;
            for (int v = 0; v < 5; ++v)
            {
                if (! ctx.active[(size_t) v]) continue;
                for (int c = 0; c < 2; ++c)
                {
                    auto& b = combs[(size_t) c][(size_t) v];
                    float rp = (float) cw - dl[(size_t) v]; while (rp < 0) rp += (float) sz;
                    int i0 = (int) rp; const float fr = rp - (float) i0;
                    if (i0 >= sz) i0 -= sz;
                    const float y = b[(size_t) i0] + fr * (b[(size_t) ((i0 + 1) % sz)] - b[(size_t) i0]);
                    float& z = combLp[(size_t) (c * 5 + v)];
                    z += lpc * (y - z);
                    b[(size_t) cw] = (c ? R[i] : L[i]) * 0.2f + z * fb;
                    (c ? wr : wl) += y;
                }
            }
            for (int v = 0; v < 5; ++v)
                if (! ctx.active[(size_t) v]) { combs[0][(size_t) v][(size_t) cw] = 0; combs[1][(size_t) v][(size_t) cw] = 0; }
            cw = (cw + 1) % sz;
            L[i] = L[i] * (1 - mix) + std::tanh (wl * 0.5f) * mix;
            R[i] = R[i] * (1 - mix) + std::tanh (wr * 0.5f) * mix;
        }
    }

    double sr = 48000;
    int lastType = -1;
    std::array<std::vector<float>, 2> dline; int dw = 0;
    std::vector<float> gbuf; int gw = 0;
    std::array<Grain, 24> grains; double grainTimer = 0;
    std::array<std::array<std::vector<float>, 5>, 2> combs; int cw = 0;
    std::array<float, 10> combLp {};
    std::array<float, 12> ap {};
    float phFb[2] = { 0, 0 };
    float lfo = 0, wow = 0, flut = 0, fbL = 0, fbR = 0, lpL = 0, lpR = 0, subEnv = 0;
    double subPh = 0;
    dsp2::WarbleLfo warbleA { 31u }, warbleB { 57u };
    float gRev = 0, gSend = 0; float* gSendL = nullptr; float* gSendR = nullptr;
    std::mt19937 rng { 777u };
    std::uniform_real_distribution<float> uni { 0.0f, 1.0f };
};
