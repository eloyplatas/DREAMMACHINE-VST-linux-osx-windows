#pragma once
// Matemáticas de afinación justa: razones, primos, factorización, presets históricos.
#include <array>
#include <vector>
#include <string>
#include <numeric>
#include <cmath>

namespace tuning
{
    // ¿n se factoriza solo con los "primos" dados? (los valores <= 1 se ignoran)
    inline bool isSmooth (int n, const std::array<int, 4>& primes)
    {
        if (n < 1) return false;
        for (int p : primes)
            if (p > 1)
                while (n % p == 0) n /= p;
        return n == 1;
    }

    // Valor permitido más cercano (empates hacia abajo). Si no hay ninguno, 1.
    inline int snap (int n, int lo, int hi, const std::array<int, 4>& primes)
    {
        for (int d = 0; d <= hi - lo; ++d)
        {
            if (n - d >= lo && isSmooth (n - d, primes)) return n - d;
            if (n + d <= hi && isSmooth (n + d, primes)) return n + d;
        }
        return 1;
    }

    // Factorización en primos verdaderos: "2³·3·7"
    inline std::vector<std::pair<int,int>> factorize (int n)
    {
        std::vector<std::pair<int,int>> f;
        for (int p = 2; (long long) p * p <= n; ++p)
        {
            int e = 0;
            while (n % p == 0) { n /= p; ++e; }
            if (e) f.push_back ({ p, e });
        }
        if (n > 1) f.push_back ({ n, 1 });
        return f;
    }

    inline std::string supNum (int e)
    {
        static const char* sup[] = { "⁰","¹","²","³","⁴","⁵","⁶","⁷","⁸","⁹" };
        std::string s, digits = std::to_string (e);
        for (char c : digits) s += sup[c - '0'];
        return s;
    }

    inline std::string factorString (int n)
    {
        if (n == 1) return "1";
        std::string s;
        for (auto [p, e] : factorize (n))
        {
            if (! s.empty()) s += "·";
            s += std::to_string (p);
            if (e > 1) s += supNum (e);
        }
        return s;
    }

    // Mayor primo de la factorización (límite primo)
    inline int primeLimit (int n)
    {
        int lim = 1;
        for (auto [p, e] : factorize (n)) lim = std::max (lim, p);
        return lim;
    }

    inline bool hasFactor (int n, int p) { return n > 0 && n % p == 0; }

    inline double cents (double ratio) { return 1200.0 * std::log2 (ratio); }

    // ---------------------------------------------------------------
    // Presets históricos (La Monte Young / disting EX)
    struct Preset
    {
        const char* name;
        double fund;       // Hz
        int octave;
        int den;
        std::array<int, 4> num;
        std::array<int, 4> primes;
        std::array<bool, 5> gates;
        int wave;          // -1 = no tocar, si no forma de onda para todas las voces
    };

    inline const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> p = {
            { "Dream Chord /31", 29.135, 1, 32,  { 42, 56, 62, 63 },   { 2, 3, 7, 31 },  { true, true, true, true, true }, -1 },
            { "Pre-Tortoise Dream Music 1964",          29.135, 1, 32,  { 42, 48, 56, 62 },   { 2, 3, 7, 31 },  { true, true, true, true, true }, -1 },
            { "Dream Chord 16:17:18 (partitura actual)",29.135, 2, 16,  { 17, 18, 24, 28 },   { 2, 3, 7, 17 },  { true, true, true, true, true }, -1 },
            { "Dream Chord 32:35:36 (Buffalo 1965)",    29.135, 1, 32,  { 35, 36, 48, 56 },   { 2, 3, 5, 7 },   { true, true, true, true, true }, -1 },
            { "Tríada 7·3·2 (32:48:56)",                29.135, 1, 32,  { 48, 56, 64, 112 },  { 2, 3, 7, 1 },   { true, true, true, true, true }, -1 },
            { "Well-Tuned Piano: tetracordo /256",      29.135, 2, 256, { 336, 372, 378, 384 },{ 2, 3, 7, 31 }, { true, true, true, true, true }, -1 },
            { "Well-Tuned Piano: Dream /256",           29.135, 2, 256, { 448, 496, 504, 512 },{ 2, 3, 7, 31 }, { true, true, true, true, true }, -1 },
            { "day of the antler 15 VIII 65",           29.135, 2, 256, { 336, 378, 392, 448 },{ 2, 3, 7, 1 },  { true, true, true, true, true }, -1 },
            { "189/98 Lost Ancestral Lake Region",      29.135, 2, 98,  { 189, 147, 168, 196 },{ 2, 3, 7, 1 },  { true, true, true, true, true }, -1 },
            { "120 Hz acuario · 7/4 · 3/2 (senos)",     120.0,  0, 4,   { 6, 7, 12, 14 },     { 2, 3, 7, 1 },   { true, true, true, false, false }, 1 },
            { "Red 60 Hz · 80 Hz (4:3)",                60.0,   0, 3,   { 4, 6, 7, 8 },       { 2, 3, 7, 1 },   { true, true, false, true, false }, -1 },
            { "Red 50 Hz (Europa) · 7/4 · 3/2",         50.0,   1, 4,   { 6, 7, 8, 14 },      { 2, 3, 7, 1 },   { true, true, true, false, true }, -1 },
            { "Composition 1960 #7 (Si–Fa#)",           123.47, 1, 2,   { 3, 4, 6, 8 },       { 2, 3, 1, 1 },   { true, true, false, false, false }, 1 },
            { "Séptimas de blues (7/4 apiladas)",       29.135, 2, 16,  { 28, 49, 24, 42 },   { 2, 3, 7, 1 },   { true, true, true, true, true }, -1 },
            { "Límite 31 puro (31/16, 31/24...)",       29.135, 2, 48,  { 62, 93, 96, 124 },  { 2, 3, 31, 1 },  { true, true, true, true, true }, -1 },
        };
        return p;
    }
}
