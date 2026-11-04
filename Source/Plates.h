#pragma once
// Dos reverbs de topología Griesinger, implementadas a partir de la literatura publicada:
//  - Dattorro: "Effect Design, Part 1" (J. Dattorro, JAES 1997), la reverb de placa de la Lexicon 224.
//  - Rings: el esquema de 4 all-pass de difusión + bucle de dos ramas con las líneas largas
//    moduladas, como el de los módulos Rings/Clouds (y el modelo "Mutable" del Deluge).
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace plates
{
    struct Line
    {
        std::vector<float> b; int w = 0;
        void alloc (int n) { b.assign ((size_t) std::max (8, n + 4), 0.0f); w = 0; }
        void clear() { std::fill (b.begin(), b.end(), 0.0f); }
        int size() const { return (int) b.size(); }
        float at (int d) const { int r = w - d; if (r < 0) r += size(); return b[(size_t) r]; }
        float atF (float d) const
        {
            float rp = (float) w - d; while (rp < 0) rp += (float) size();
            int i0 = (int) rp; const float fr = rp - (float) i0;
            if (i0 >= size()) i0 -= size();
            const int i1 = (i0 + 1) % size();
            return b[(size_t) i0] + fr * (b[(size_t) i1] - b[(size_t) i0]);
        }
        void push (float x) { b[(size_t) w] = x; if (++w >= size()) w = 0; }
    };

    // all-pass de Schroeder con línea propia (retardo variable opcional)
    struct AP
    {
        Line l; float len = 1;
        float process (float x, float g)
        {
            const float d = l.atF (len);
            const float v = x + g * d;
            l.push (v);
            return d - g * v;
        }
    };

    inline float onePole (float& z, float x, float c) { z += c * (x - z); return z; }

    // ------------------------------------------------------------------------------------
    class Dattorro
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            const float k = sr / 29761.0f * 1.6f;   // margen para el tamaño máximo
            for (auto* a : { &ap1, &ap2, &ap3, &ap4, &dap1a, &dap1b, &dap2a, &dap2b }) a->l.alloc ((int) (2700 * k) + 64);
            for (auto* d : { &del1a, &del1b, &del2a, &del2b }) d->alloc ((int) (4500 * k) + 64);
            clear();
        }
        void clear()
        {
            for (auto* a : { &ap1, &ap2, &ap3, &ap4, &dap1a, &dap1b, &dap2a, &dap2b }) a->l.clear();
            for (auto* d : { &del1a, &del1b, &del2a, &del2b }) d->clear();
            band = lp1 = lp2 = 0; ph = 0;
        }
        // in: mono; out: estéreo húmedo
        void process (const float* in, float* outL, float* outR, int n, float rt60, float size, float damp, float modHz, float modDepth)
        {
            const float k = sr / 29761.0f * (0.55f + 0.95f * std::clamp (size, 0.01f, 1.0f));
            ap1.len = 142 * k; ap2.len = 107 * k; ap3.len = 379 * k; ap4.len = 277 * k;
            dap1b.len = 1800 * k; dap2b.len = 2656 * k;
            const float d1a = 4453 * k, d1b = 3720 * k, d2a = 4217 * k, d2b = 3163 * k;
            const float loopT = (672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163) * k / sr;
            const float decay = std::clamp (std::pow (10.0f, -3.0f * loopT / (4.0f * std::max (0.1f, rt60))), 0.05f, 0.97f);
            const float kdd2 = std::clamp (decay + 0.15f, 0.25f, 0.5f);
            const float dampC = 1.0f - 0.85f * std::clamp (damp, 0.0f, 1.0f);   // coeficiente del paso bajo
            const float exc = 16.0f * k * std::clamp (modDepth, 0.0f, 1.0f) * 2.0f;
            const float inc = modHz * 0.4f / sr;
            for (int i = 0; i < n; ++i)
            {
                ph += inc; if (ph >= 1.0f) ph -= 1.0f;
                const float m1 = std::sin (6.2831853f * ph), m2 = std::cos (6.2831853f * ph);
                float x = onePole (band, in[i], 0.9995f);
                x = ap1.process (x, 0.75f); x = ap2.process (x, 0.75f);
                x = ap3.process (x, 0.625f); x = ap4.process (x, 0.625f);

                // rama 1
                dap1a.len = 672 * k + exc * (1.0f + m1) + 1.0f;
                float a = x + decay * del2b.atF (d2b);
                a = dap1a.process (a, -0.7f);
                del1a.push (a);
                a = del1a.atF (d1a);
                a = onePole (lp1, a, dampC) * decay;
                a = dap1b.process (a, kdd2);
                del1b.push (a);
                // rama 2
                dap2a.len = 908 * k + exc * (1.0f + m2) + 1.0f;
                float b = x + decay * del1b.atF (d1b);
                b = dap2a.process (b, -0.7f);
                del2a.push (b);
                b = del2a.atF (d2a);
                b = onePole (lp2, b, dampC) * decay;
                b = dap2b.process (b, kdd2);
                del2b.push (b);

                // tomas de salida (tabla 2 del artículo)
                float l = 0.6f * (del2a.atF (266 * k) + del2a.atF (2974 * k) - dap2b.l.atF (1913 * k) + del2b.atF (1996 * k)
                                  - del1a.atF (1990 * k) - dap1b.l.atF (187 * k) - del1b.atF (1066 * k));
                float r = 0.6f * (del1a.atF (353 * k) + del1a.atF (3627 * k) - dap1b.l.atF (1228 * k) + del1b.atF (2673 * k)
                                  - del2a.atF (2111 * k) - dap2b.l.atF (335 * k) - del2b.atF (121 * k));
                outL[i] = l; outR[i] = r;
            }
        }
    private:
        float sr = 48000;
        AP ap1, ap2, ap3, ap4, dap1a, dap1b, dap2a, dap2b;
        Line del1a, del1b, del2a, del2b;
        float band = 0, lp1 = 0, lp2 = 0, ph = 0;
    };

    // ------------------------------------------------------------------------------------
    class Rings
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            const float k = sr / 32000.0f * 1.6f;
            for (auto* a : { &ap1, &ap2, &ap3, &ap4, &dap1a, &dap1b, &dap2a, &dap2b }) a->l.alloc ((int) (2700 * k) + 64);
            del1.alloc ((int) (4600 * k) + 256);
            del2.alloc ((int) (6400 * k) + 256);
            clear();
        }
        void clear()
        {
            for (auto* a : { &ap1, &ap2, &ap3, &ap4, &dap1a, &dap1b, &dap2a, &dap2b }) a->l.clear();
            del1.clear(); del2.clear(); lp1 = lp2 = 0; ph1 = 0; ph2 = 0.37f;
        }
        void process (const float* in, float* outL, float* outR, int n, float rt60, float size, float damp, float modHz, float modDepth)
        {
            const float k = sr / 32000.0f * (0.55f + 0.95f * std::clamp (size, 0.01f, 1.0f));
            ap1.len = 150 * k; ap2.len = 214 * k; ap3.len = 319 * k; ap4.len = 527 * k;
            dap1a.len = 2182 * k; dap1b.len = 2690 * k; dap2a.len = 2525 * k; dap2b.len = 2197 * k;
            const float l1 = 4501 * k, l2 = 6312 * k;
            const float loopT = (2182 + 2690 + 4501 + 2525 + 2197 + 6312) * k / sr;
            const float krt = std::clamp (std::pow (10.0f, -3.0f * loopT / (2.0f * std::max (0.1f, rt60))), 0.05f, 0.98f);
            const float kap = 0.625f;
            const float lpC = 1.0f - 0.85f * std::clamp (damp, 0.0f, 1.0f);
            const float dep = std::clamp (modDepth, 0.0f, 1.0f) * 2.0f;
            const float inc1 = modHz * 0.2f / sr, inc2 = modHz * 0.27f / sr;
            for (int i = 0; i < n; ++i)
            {
                ph1 += inc1; if (ph1 >= 1.0f) ph1 -= 1.0f;
                ph2 += inc2; if (ph2 >= 1.0f) ph2 -= 1.0f;
                float x = in[i] * 0.5f;
                x = ap1.process (x, kap); x = ap2.process (x, kap); x = ap3.process (x, kap); x = ap4.process (x, kap);
                const float apout = x;

                // rama 1: lee la línea 2 (modulada), paso bajo, dos all-pass, escribe la línea 1
                float a = apout + krt * del2.atF (l2 - 100.0f * k * dep * (1.0f + std::sin (6.2831853f * ph2)) * 0.5f - 2.0f);
                a = onePole (lp1, a, lpC);
                a = dap1a.process (a, -kap);
                a = dap1b.process (a, kap);
                del1.push (a);
                // rama 2
                float b = apout + krt * del1.atF (l1 - 50.0f * k * dep * (1.0f + std::sin (6.2831853f * ph1)) * 0.5f - 2.0f);
                b = onePole (lp2, b, lpC);
                b = dap2a.process (b, kap);
                b = dap2b.process (b, -kap);
                del2.push (b);
                outL[i] = a * 1.2f;
                outR[i] = b * 1.2f;
            }
        }
    private:
        float sr = 48000;
        AP ap1, ap2, ap3, ap4, dap1a, dap1b, dap2a, dap2b;
        Line del1, del2;
        float lp1 = 0, lp2 = 0, ph1 = 0, ph2 = 0;
    };
}
