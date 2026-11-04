#pragma once
// Procesadores de la versión 1.2, inspirados en el firmware del Synthstrom Deluge
// (reimplementación propia, no código copiado): LFO "warble", filtro ladder,
// SVF con morph, wavefold, decimador, EQ de estantes y compresor RMS con mezcla.
#include <cmath>
#include <algorithm>
#include <random>

namespace dsp2
{
    constexpr float kPi = 3.14159265358979f;

    // -------------------------------------------------------------------------
    // LFO aleatorio suave: en cada ciclo elige un destino al azar y llega a él con
    // un filtro de segundo orden (derivada cero al principio y al final).
    struct WarbleLfo
    {
        double phase = 0; float target = 0, value = 0, speed = 0;
        std::mt19937 rng;
        explicit WarbleLfo (unsigned seed = 99u) : rng (seed) {}
        float tick (float rateHz, float sr)
        {
            const double inc = rateHz / sr;
            phase += inc;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                target = std::uniform_real_distribution<float> (-1.0f, 1.0f) (rng);
            }
            const float w = std::min (0.25f, 2.0f * kPi * rateHz * 1.5f / sr);
            speed += w * w * (target - value) - 2.0f * w * speed;   // amortiguamiento crítico
            value = std::clamp (value + speed, -1.5f, 1.5f);
            return value;
        }
    };

    // -------------------------------------------------------------------------
    // Ladder paso bajo de 4 polos con saturación (drive) y resonancia
    struct Ladder
    {
        float s[4] = { 0, 0, 0, 0 };
        void reset() { s[0] = s[1] = s[2] = s[3] = 0; }
        float process (float x, float g, float k, float drive)
        {
            // drive >= 1: ganancia unidad en señales pequeñas, saturación en las grandes
            const float in = std::tanh (x * drive) / std::sqrt (drive);
            float u = std::tanh (in - k * s[3]);
            for (int i = 0; i < 4; ++i)
            {
                s[i] += g * (u - s[i]);
                u = s[i];
            }
            return s[3] * (1.0f + 0.35f * k);
        }
    };

    // -------------------------------------------------------------------------
    // SVF de topología TPT: salidas LP, BP, HP
    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        void process (float x, float g, float k, float& lp, float& bp, float& hp)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
    };

    // -------------------------------------------------------------------------
    // Biquad (RBJ) para los estantes del EQ
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        void shelf (bool high, float f, float gainDb, float sr)
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float w0 = 2.0f * kPi * std::clamp (f, 10.0f, sr * 0.45f) / sr;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float alpha = sw / 2.0f * std::sqrt (2.0f);   // S = 1
            const float sq = 2.0f * std::sqrt (A) * alpha;
            float B0, B1, B2, A0, A1, A2;
            if (! high)
            {
                B0 = A * ((A + 1) - (A - 1) * cw + sq);
                B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                B2 = A * ((A + 1) - (A - 1) * cw - sq);
                A0 = (A + 1) + (A - 1) * cw + sq;
                A1 = -2 * ((A - 1) + (A + 1) * cw);
                A2 = (A + 1) + (A - 1) * cw - sq;
            }
            else
            {
                B0 = A * ((A + 1) + (A - 1) * cw + sq);
                B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                B2 = A * ((A + 1) + (A - 1) * cw - sq);
                A0 = (A + 1) - (A - 1) * cw + sq;
                A1 = 2 * ((A - 1) - (A + 1) * cw);
                A2 = (A + 1) - (A - 1) * cw - sq;
            }
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // -------------------------------------------------------------------------
    // Compresor RMS de alimentación directa, con filtro paso alto en el sidechain
    // y control de mezcla (compresión en paralelo)
    struct Compressor
    {
        float hpX[2] = { 0, 0 }, hpY[2] = { 0, 0 }, env = 0, grDb = 0;
        void reset() { hpX[0] = hpX[1] = hpY[0] = hpY[1] = 0; env = 0; grDb = 0; }
        void process (float* L, float* R, int n, float sr, float threshDb, float ratio, float attMs, float relMs,
                      float hpfHz, float blend, float makeupDb)
        {
            const float hpC = std::exp (-2.0f * kPi * hpfHz / sr);
            const float envC = 1.0f - std::exp (-1.0f / (0.005f * sr));
            const float aC = 1.0f - std::exp (-1.0f / (std::max (0.05f, attMs) * 0.001f * sr));
            const float rC = 1.0f - std::exp (-1.0f / (std::max (1.0f, relMs) * 0.001f * sr));
            const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
            for (int i = 0; i < n; ++i)
            {
                float d = 0;
                float* ch[2] = { L + i, R + i };
                for (int c = 0; c < 2; ++c)
                {
                    const float x = *ch[c];
                    const float y = hpC * (hpY[c] + x - hpX[c]);
                    hpX[c] = x; hpY[c] = y;
                    d = std::max (d, y * y);
                }
                env += envC * (d - env);
                const float lev = 10.0f * std::log10 (env + 1e-12f);
                const float over = lev - threshDb;
                const float target = over > 0.0f ? over * slope : 0.0f;
                grDb += (target > grDb ? aC : rC) * (target - grDb);
                const float g = std::pow (10.0f, (makeupDb - grDb) / 20.0f);
                const float mix = (1.0f - blend) + blend * g;
                L[i] *= mix; R[i] *= mix;
            }
        }
    };
}
