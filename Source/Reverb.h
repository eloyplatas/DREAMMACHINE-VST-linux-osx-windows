#pragma once
// Reverb FDN de 8 líneas con reflexiones tempranas, difusión, amortiguación y modulación.
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include "Plates.h"

class DreamReverb
{
public:
    struct Settings
    {
        float mix = 0.0f;        // 0..1
        float timeSec = 1.7f;    // RT60
        int   model = 1;         // 0 Room, 1 Hall, 2 Plate, 3 Dream House, 4 Shimmer, 5 Dattorro, 6 Rings
        float size = 1.0f;       // 0.01..1
        float damp = 0.6f;       // 0..1
        float modHz = 2.5f;
        float modDepth = 0.25f;  // 0..1
        float earlyGain = 0.25f; // lineal
        float diffGain = 0.5f;   // lineal
        float shimmer = 0.0f;    // 0..1 (solo modelo 4)
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int maxLen = (int) (sr * 0.8) + 8;
        for (auto& d : lines) { d.buf.assign ((size_t) maxLen, 0.0f); d.w = 0; d.lp = 0.0f; }
        for (auto& c : ap) for (auto& a : c) { a.buf.assign ((size_t) (sr * 0.06) + 8, 0.0f); a.w = 0; }
        early.assign ((size_t) (sr * 0.25) + 8, 0.0f);
        ew = 0;
        for (auto& ps : shift) ps.prepare (sr);
        shimL = shimR = 0.0f;
        for (int i = 0; i < 8; ++i) lfoPh[(size_t) i] = i / 8.0;
        dattorro.prepare (sr);
        rings.prepare (sr);
    }

    // vacía todos los buffers (se usa si aparece un valor no finito)
    void reset()
    {
        for (auto& d : lines) { std::fill (d.buf.begin(), d.buf.end(), 0.0f); d.lp = 0.0f; }
        for (auto& c : ap) for (auto& a : c) std::fill (a.buf.begin(), a.buf.end(), 0.0f);
        std::fill (early.begin(), early.end(), 0.0f);
        for (auto& ps : shift) std::fill (ps.buf.begin(), ps.buf.end(), 0.0f);
        shimL = shimR = 0.0f;
        envIn[0] = envIn[1] = envOut[0] = envOut[1] = 0.0f;
        envOrig = envMix = 0.0f; shimGain = 1.0f;
        hpXL = hpYL = hpXR = hpYR = 0.0f;
        dattorro.clear();
        rings.clear();
    }

    // sendL/sendR (opcionales): señal que entra en la reverb pero no en la parte seca
    void process (float* L, float* R, int n, const Settings& s, const float* sendL = nullptr, const float* sendR = nullptr)
    {
        if (s.model >= 5) { processPlate (L, R, n, s, sendL, sendR); return; }
        processFdn (L, R, n, s, sendL, sendR);
    }

private:
    void processPlate (float* L, float* R, int n, const Settings& s, const float* sendL, const float* sendR)
    {
        constexpr int CH = 256;
        std::array<float, CH> in {}, wl {}, wr {};
        const float wet = std::clamp (s.mix, 0.0f, 1.0f), dry = 1.0f - wet;
        const float g = s.diffGain * 1.4f + s.earlyGain * 0.3f;
        for (int off = 0; off < n; off += CH)
        {
            const int m = std::min (CH, n - off);
            for (int i = 0; i < m; ++i)
                in[(size_t) i] = 0.5f * (L[off + i] + R[off + i] + (sendL ? sendL[off + i] : 0.0f) + (sendR ? sendR[off + i] : 0.0f));
            if (s.model == 5) dattorro.process (in.data(), wl.data(), wr.data(), m, s.timeSec, s.size, s.damp, s.modHz, s.modDepth);
            else              rings.process (in.data(), wl.data(), wr.data(), m, s.timeSec, s.size, s.damp, s.modHz, s.modDepth);
            for (int i = 0; i < m; ++i)
            {
                L[off + i] = L[off + i] * dry + wl[(size_t) i] * g * wet;
                R[off + i] = R[off + i] * dry + wr[(size_t) i] * g * wet;
            }
        }
    }

    void processFdn (float* L, float* R, int n, const Settings& s, const float* sendL, const float* sendR)
    {
        static constexpr float baseMs[8] = { 37.1f, 41.9f, 47.3f, 53.7f, 59.9f, 67.1f, 73.9f, 81.7f };
        static constexpr float apMs[4]   = { 4.77f, 3.59f, 12.73f, 9.31f };
        static constexpr float erMs[8]   = { 7.f, 11.f, 17.f, 23.f, 31.f, 41.f, 53.f, 67.f };
        static constexpr float modelScale[5] = { 0.45f, 1.0f, 0.7f, 1.6f, 1.25f };
        static constexpr float modelDiff[5]  = { 0.55f, 0.6f, 0.75f, 0.7f, 0.7f };

        const int m = std::clamp (s.model, 0, 4);
        // shimmer: la copia transpuesta se reinyecta con ganancia limitada y saturada,
        // y el tiempo de caída se acota para que el bucle nunca supere ganancia 1
        const float shimAmt = m == 4 ? std::clamp (s.shimmer, 0.0f, 1.0f) : 0.0f;
        const float shimA = shimAmt * 0.5f;   // fracción de la realimentación que pasa por el transpositor
        const float scale = modelScale[m] * (0.3f + 1.3f * std::clamp (s.size, 0.01f, 1.0f));
        const float diff = modelDiff[m];
        const float rt = std::max (0.1f, s.timeSec);
        const float dampCut = 20000.0f * std::pow (0.07f, std::clamp (s.damp, 0.0f, 1.0f));
        const float lpc = 1.0f - std::exp (-2.0f * 3.14159265f * dampCut / (float) sr);
        const float modSamps = (float) (sr * 0.0025) * std::clamp (s.modDepth, 0.0f, 1.0f) * (m >= 3 ? 2.0f : 1.0f);
        const float lfoInc = s.modHz / (float) sr;

        std::array<float, 8> len {}, g {};
        for (int i = 0; i < 8; ++i)
        {
            len[(size_t) i] = std::min ((float) lines[(size_t) i].buf.size() - modSamps - 4.0f, baseMs[i] * 0.001f * (float) sr * scale);
            g[(size_t) i] = std::pow (10.0f, -3.0f * len[(size_t) i] / (rt * (float) sr));
        }
        std::array<int, 4> apLen {};
        for (int i = 0; i < 4; ++i) apLen[(size_t) i] = std::clamp ((int) (apMs[i] * 0.001f * (float) sr * scale), 1, (int) ap[0][(size_t) i].buf.size() - 9);
        std::array<int, 8> erLen {};
        for (int i = 0; i < 8; ++i) erLen[(size_t) i] = std::clamp ((int) (erMs[i] * 0.001f * (float) sr * (0.4f + s.size)), 1, (int) early.size() - 1);

        const float wet = std::clamp (s.mix, 0.0f, 1.0f), dry = 1.0f - wet;
        const float envC = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
        // mezcla de potencia constante (las dos señales están poco correladas)
        const float cDry = std::sqrt (1.0f - shimA), cShim = std::sqrt (shimA);
        const float agcC = 1.0f - std::exp (-1.0f / (0.25f * (float) sr));
        const float hpC = std::exp (-2.0f * 3.14159265f * 60.0f / (float) sr);   // paso alto 60 Hz
        const int esz = (int) early.size();

        for (int k = 0; k < n; ++k)
        {
            float inL = L[k], inR = R[k];
            const float fL = inL + (sendL ? sendL[k] : 0.0f), fR = inR + (sendR ? sendR[k] : 0.0f);
            // difusión de entrada
            float dl = fL, dr = fR;
            for (int a = 0; a < 4; ++a)
            {
                dl = ap[0][(size_t) a].tick (dl, apLen[(size_t) a], diff);
                dr = ap[1][(size_t) a].tick (dr, apLen[(size_t) a] + 7, diff);
            }
            // reflexiones tempranas
            early[(size_t) ew] = 0.5f * (fL + fR);
            float eL = 0, eR = 0;
            for (int i = 0; i < 8; ++i)
            {
                int ri = ew - erLen[(size_t) i]; if (ri < 0) ri += esz;
                const float v = early[(size_t) ri] * (1.0f - i * 0.1f);
                (i & 1 ? eR : eL) += v;
            }
            ew = (ew + 1) % esz;

            // FDN
            std::array<float, 8> o {};
            float sum = 0;
            for (int i = 0; i < 8; ++i)
            {
                auto& d = lines[(size_t) i];
                lfoPh[(size_t) i] += lfoInc * (1.0f + 0.13f * i);
                if (lfoPh[(size_t) i] >= 1.0f) lfoPh[(size_t) i] -= 1.0f;
                const float mod = modSamps * (0.5f + 0.5f * std::sin (6.2831853f * lfoPh[(size_t) i]));
                const float rp = (float) d.w - (len[(size_t) i] + mod);
                const int sz = (int) d.buf.size();
                float fp = rp; while (fp < 0) fp += (float) sz;
                int i0 = (int) fp; const float fr = fp - (float) i0;
                if (i0 >= sz) i0 -= sz;            // el redondeo puede dar exactamente sz
                const int i1 = (i0 + 1) % sz;
                float v = d.buf[(size_t) i0] + fr * (d.buf[(size_t) i1] - d.buf[(size_t) i0]);
                d.lp += lpc * (v - d.lp);
                o[(size_t) i] = d.lp * g[(size_t) i];
                sum += o[(size_t) i];
            }
            const float hh = sum * 0.25f; // Householder 2/N
            float wl = 0, wr = 0;
            for (int i = 0; i < 8; ++i) (i & 1 ? wr : wl) += o[(size_t) i];
            // shimmer: una parte de la realimentación se sustituye (no se suma) por la copia
            // una octava arriba, así la ganancia total del bucle sigue siendo < 1 y la cola decae
            if (shimA > 0.0f)
            {
                // control de ganancia: la copia transpuesta nunca lleva más energía que
                // una línea media (x 0.9), así el bucle siempre pierde energía
                const float srcL = wl * 0.25f, srcR = wr * 0.25f;
                float rawL = shift[0].process (srcL), rawR = shift[1].process (srcR);
                // paso alto: la rama de shimmer no debe acumular graves
                const float hl = hpC * (hpYL + rawL - hpXL); hpXL = rawL; hpYL = hl; rawL = hl;
                const float hr = hpC * (hpYR + rawR - hpXR); hpXR = rawR; hpYR = hr; rawR = hr;
                float eLn = 0, eRn = 0;   // energía media por línea en cada canal
                for (int i = 0; i < 8; ++i) (i & 1 ? eRn : eLn) += o[(size_t) i] * o[(size_t) i];
                envIn[0]  += envC * (eLn * 0.25f - envIn[0]);  envIn[1]  += envC * (eRn * 0.25f - envIn[1]);
                envOut[0] += envC * (rawL * rawL - envOut[0]); envOut[1] += envC * (rawR * rawR - envOut[1]);
                const float gL = std::min (3.0f, 0.9f * std::sqrt ((envIn[0] + 1e-12f) / (envOut[0] + 1e-12f)));
                const float gR = std::min (3.0f, 0.9f * std::sqrt ((envIn[1] + 1e-12f) / (envOut[1] + 1e-12f)));
                shimL = rawL * gL;
                shimR = rawR * gR;
            }
            // realimentación de la FDN (Householder), con la parte de shimmer mezclada
            std::array<float, 8> fbv {};
            float eOrig = 0, eMix = 0;
            for (int i = 0; i < 8; ++i)
            {
                const float f0 = o[(size_t) i] - hh;
                float f = f0;
                if (shimA > 0.0f) f = f0 * cDry + cShim * g[(size_t) i] * ((i & 1) ? shimR : shimL);
                fbv[(size_t) i] = f;
                eOrig += f0 * f0; eMix += f * f;
            }
            if (shimA > 0.0f)
            {
                // la energía del bucle con shimmer nunca supera la del bucle normal (que decae con RT)
                envOrig += agcC * (eOrig - envOrig);
                envMix  += agcC * (eMix - envMix);
                const float target = std::min (1.0f, std::sqrt ((envOrig + 1e-20f) / (envMix + 1e-20f)));
                shimGain += agcC * (target - shimGain);
                for (auto& f : fbv) f *= shimGain;
            }
            for (int i = 0; i < 8; ++i)
            {
                auto& d = lines[(size_t) i];
                const float in = (i & 1) ? dr : dl;
                float wv = fbv[(size_t) i] + in * 0.35f;
                if (wv > 8.0f || wv < -8.0f) wv = 8.0f * std::tanh (wv * 0.125f);   // límite de seguridad
                d.buf[(size_t) d.w] = wv;
                d.w = (d.w + 1) % (int) d.buf.size();
            }
            const float outL = wl * 0.5f * s.diffGain + eL * 0.35f * s.earlyGain;
            const float outR = wr * 0.5f * s.diffGain + eR * 0.35f * s.earlyGain;
            L[k] = inL * dry + outL * wet;
            R[k] = inR * dry + outR * wet;
        }
    }

    struct Line { std::vector<float> buf; int w = 0; float lp = 0; };
    struct AP
    {
        std::vector<float> buf; int w = 0;
        float tick (float x, int d, float gg)
        {
            const int sz = (int) buf.size();
            int r = w - d; if (r < 0) r += sz;
            const float y = buf[(size_t) r];
            const float v = x + gg * y;
            buf[(size_t) w] = v;
            w = (w + 1) % sz;
            return y - gg * v;
        }
    };
    // transpositor de octava (+12) por dos cabezas de lectura con fundido
    struct Shifter
    {
        std::vector<float> buf; int w = 0; float ph = 0; int win = 2048;
        void prepare (double rate) { win = (int) (rate * 0.06); buf.assign ((size_t) win * 2 + 8, 0.0f); w = 0; ph = 0; }
        float tap (float d)
        {
            const int sz = (int) buf.size();
            float rp = (float) w - d; while (rp < 0) rp += (float) sz;
            int i0 = (int) rp; const float fr = rp - (float) i0;
            if (i0 >= sz) i0 -= sz;
            return buf[(size_t) i0] + fr * (buf[(size_t) ((i0 + 1) % sz)] - buf[(size_t) i0]);
        }
        float process (float x)
        {
            buf[(size_t) w] = x;
            // ratio 2: la distancia de lectura decrece 1 muestra por muestra
            ph += 1.0f / (float) win; if (ph >= 1.0f) ph -= 1.0f;
            float ph2 = ph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
            const float d1 = (1.0f - ph) * (float) win + 2.0f, d2 = (1.0f - ph2) * (float) win + 2.0f;
            const float g1 = std::sin (3.14159265f * ph), g2 = std::sin (3.14159265f * ph2);
            const float y = tap (d1) * g1 + tap (d2) * g2;
            w = (w + 1) % (int) buf.size();
            return y * 0.7f;
        }
    };
    std::array<Shifter, 2> shift; float shimL = 0, shimR = 0;
    float envIn[2] = { 0, 0 }, envOut[2] = { 0, 0 };
    float envOrig = 0, envMix = 0, shimGain = 1.0f;
    float hpXL = 0, hpYL = 0, hpXR = 0, hpYR = 0;
    plates::Dattorro dattorro;
    plates::Rings rings;
    double sr = 48000;
    std::array<Line, 8> lines;
    std::array<std::array<AP, 4>, 2> ap;
    std::vector<float> early; int ew = 0;
    std::array<float, 8> lfoPh {};
};
