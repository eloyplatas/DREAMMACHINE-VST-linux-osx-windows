#pragma once
// Tonos de combinación según "The Two Systems of Eleven Categories" (La Monte Young):
// las frecuencias generadoras (orden 0) son múltiplos enteros de una fundamental común;
// los tonos de orden n son sumas y diferencias de todos los de orden inferior.
#include <vector>
#include <numeric>
#include <algorithm>

namespace combtones
{
    constexpr int CAP = 1024;   // mayor número de armónico que se calcula

    // Reduce un conjunto de armónicos a su forma mínima (divide por el MCD)
    inline std::vector<int> reduce (std::vector<int> h)
    {
        int g = 0;
        for (int x : h) g = std::gcd (g, x);
        if (g > 1) for (auto& x : h) x /= g;
        return h;
    }

    // levels[k] = tonos que aparecen por primera vez en el orden k (k = 0 generadoras)
    inline std::vector<std::vector<int>> levels (const std::vector<int>& gen, int order)
    {
        std::vector<char> seen (CAP + 1, 0);
        std::vector<std::vector<int>> lv (1);
        std::vector<int> all;
        for (int x : gen)
            if (x > 0 && x <= CAP && ! seen[(size_t) x]) { seen[(size_t) x] = 1; lv[0].push_back (x); all.push_back (x); }
        for (int k = 1; k <= order; ++k)
        {
            std::vector<int> nw;
            for (int a : lv[(size_t) k - 1])
                for (int b : all)
                    for (int v : { a + b, std::abs (a - b) })
                        if (v > 0 && v <= CAP && ! seen[(size_t) v]) { seen[(size_t) v] = 1; nw.push_back (v); }
            std::sort (nw.begin(), nw.end());
            for (int v : nw) all.push_back (v);
            lv.push_back (std::move (nw));
        }
        return lv;
    }

    // ¿El conjunto (y sus tonos de combinación hasta 'order') evita los múltiplos de X?
    inline bool allowed (const std::vector<int>& genIn, int order, int X)
    {
        if (genIn.size() <= 1 || X < 2) return true;
        const auto gen = reduce (genIn);
        for (auto& lv : levels (gen, order))
            for (int v : lv)
                if (v % X == 0) return false;
        return true;
    }
}
