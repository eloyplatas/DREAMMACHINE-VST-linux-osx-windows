#pragma once
// Presets de fábrica: cada uno parte de los valores por defecto y cambia solo lo indicado.
#include <vector>
#include <string>
#include <utility>
#include <initializer_list>

namespace factory
{
    using KV = std::pair<std::string, float>;
    struct Patch { std::string name; std::vector<KV> v; };

    // tablas: 0 YOUNG 7-3-2, 1 PRIME 31, 2 SAW>SQ, 3 VOICE, 4 BOWED, 5 ORGAN, 6 ODD>EVEN,
    //         7 HARM SWEEP, 8 SOPRANINO, 9 TANPURA, 10 SINE FOLD, 11 PULSE
    inline std::vector<KV> tune (float fund, int oct, int den, std::initializer_list<int> nums,
                                 std::initializer_list<int> primes = { 2, 3, 7, 31 },
                                 std::initializer_list<int> gates = { 1, 1, 1, 1, 1 })
    {
        std::vector<KV> v { { "fund", fund }, { "octave", (float) oct }, { "den", (float) den } };
        int i = 1; for (int n : nums)   v.push_back ({ "num" + std::to_string (i++), (float) n });
        i = 1;     for (int p : primes) v.push_back ({ "prime" + std::to_string (i++), (float) p });
        i = 0;     for (int g : gates)  v.push_back ({ "gate" + std::to_string (i++), (float) g });
        return v;
    }
    inline std::vector<KV> allWaves (int w) { std::vector<KV> v; for (int i = 0; i < 5; ++i) v.push_back ({ "wave" + std::to_string (i), (float) w }); return v; }
    inline std::vector<KV> operator+ (std::vector<KV> a, const std::vector<KV>& b) { a.insert (a.end(), b.begin(), b.end()); return a; }

    inline const std::vector<Patch>& patches()
    {
        const auto dream   = tune (29.135f, 1, 32, { 42, 56, 62, 63 });
        const auto ptdm    = tune (29.135f, 1, 32, { 42, 48, 56, 62 });
        const auto d171    = tune (29.135f, 2, 16, { 17, 18, 24, 28 }, { 2, 3, 7, 17 });
        const auto d353    = tune (29.135f, 1, 32, { 35, 36, 48, 56 }, { 2, 3, 5, 7 });
        const auto triad   = tune (29.135f, 1, 32, { 48, 56, 64, 112 }, { 2, 3, 7, 1 });
        const auto wtpTet  = tune (29.135f, 2, 256, { 336, 372, 378, 384 });
        const auto wtpDr   = tune (29.135f, 2, 256, { 448, 496, 504, 512 });
        const auto antler  = tune (29.135f, 2, 256, { 336, 378, 392, 448 }, { 2, 3, 7, 1 });
        const auto lake    = tune (29.135f, 2, 98, { 189, 147, 168, 196 }, { 2, 3, 7, 1 });
        const auto lim31   = tune (29.135f, 2, 48, { 62, 93, 96, 124 }, { 2, 3, 31, 1 });
        const auto blues   = tune (29.135f, 2, 16, { 28, 49, 24, 42 }, { 2, 3, 7, 1 });

        static const std::vector<Patch> p = {
            // ---------------- La Monte Young: afinaciones históricas
            { "Dream Chord /31", dream + std::vector<KV> { { "attack", 4 }, { "release", 6 }, { "revMix", 25 } } },
            { "Pre-Tortoise Dream Music 1964", ptdm + std::vector<KV> { { "wtable", 8 }, { "wpos", 30 }, { "revMix", 30 }, { "attack", 3 } } },
            { "Dream Chord 16:17:18", d171 + std::vector<KV> { { "wtable", 4 }, { "wpos", 50 }, { "revMix", 30 } } },
            { "Dream Chord 32:35:36 (Buffalo 1965)", d353 + std::vector<KV> { { "wtable", 4 }, { "wpos", 20 }, { "revMix", 30 } } },
            { "Triada 7-3-2 (32:48:56)", triad + allWaves (1) + std::vector<KV> { { "revMix", 20 } } },
            { "Well-Tuned Piano: tetracordo", wtpTet + std::vector<KV> { { "wtable", 7 }, { "wpos", 25 }, { "release", 8 }, { "revMix", 35 } } },
            { "Well-Tuned Piano: Dream", wtpDr + std::vector<KV> { { "wtable", 7 }, { "wpos", 40 }, { "release", 8 }, { "revMix", 35 } } },
            { "day of the antler 15 VIII 65", antler + std::vector<KV> { { "wtable", 4 }, { "diffAmt", 30 }, { "revMix", 30 } } },
            { "189/98 Lost Ancestral Lake Region", lake + std::vector<KV> { { "wtable", 9 }, { "wpos", 40 }, { "revModel", 3 }, { "revTime", 15 }, { "revMix", 45 } } },
            { "Acuario 120 Hz - 7/4 - 3/2 (senos)", tune (120, 0, 4, { 6, 7, 12, 14 }, { 2, 3, 7, 1 }, { 1, 1, 1, 0, 0 }) + allWaves (1) + std::vector<KV> { { "revMix", 10 } } },
            { "Red 60 Hz - 80 Hz (4:3)", tune (60, 0, 3, { 4, 6, 7, 8 }, { 2, 3, 7, 1 }, { 1, 1, 0, 1, 0 }) + allWaves (1) },
            { "Dream House Munich 1969 (50 Hz)", tune (50, 1, 8, { 12, 14, 21, 24 }, { 2, 3, 7, 1 }) + allWaves (1)
                + std::vector<KV> { { "invAmp", 1 }, { "walkOn", 1 }, { "walkSpeed", 0.08f }, { "revModel", 0 }, { "revMix", 15 } } },
            { "Composition 1960 #7 (Si - Fa#)", tune (123.47f, 1, 2, { 3, 4, 6, 8 }, { 2, 3, 1, 1 }, { 1, 1, 0, 0, 0 }) + std::vector<KV> { { "wtable", 4 }, { "wpos", 60 } } },
            { "7:4 sobre 30 Hz (52,5 Hz)", tune (30, 0, 4, { 7, 8, 12, 14 }, { 2, 3, 7, 1 }, { 1, 1, 0, 0, 0 }) + allWaves (1) + std::vector<KV> { { "master", 0 } } },
            { "7:4 agudo (840:480)", tune (480, 0, 4, { 7, 8, 12, 14 }, { 2, 3, 7, 1 }, { 1, 1, 0, 0, 0 }) + allWaves (1) + std::vector<KV> { { "master", -12 } } },
            { "Limite 31 puro", lim31 + std::vector<KV> { { "wtable", 1 }, { "wpos", 60 }, { "revMix", 30 } } },

            // ---------------- Técnicas del Theatre of Eternal Music
            { "Sopranino Blues (permutaciones)", blues + std::vector<KV> { { "wtable", 8 }, { "wpos", 50 }, { "arpOn", 1 }, { "arpRate", 14 }, { "arpMode", 4 }, { "arpSmooth", 6 }, { "attack", 0.3f }, { "revMix", 25 } } },
            { "Theatre of Eternal Music (contact mics)", dream + std::vector<KV> { { "wtable", 4 }, { "wpos", 40 }, { "diffAmt", 45 }, { "diffDrive", 55 }, { "vocAmt", 25 }, { "vocDrift", 8 }, { "revMix", 40 } } },
            { "Zazeela voice drone", triad + std::vector<KV> { { "wtable", 3 }, { "wlfoDepth", 60 }, { "wlfoRate", 0.05f }, { "vocAmt", 50 }, { "vocDrift", 6 }, { "vocQ", 20 }, { "revMix", 35 } } },
            { "Dream generativo lento", dream + std::vector<KV> { { "dreamOn", 1 }, { "dreamRate", 40 }, { "dreamDensity", 60 }, { "attack", 10 }, { "release", 15 }, { "revModel", 3 }, { "revTime", 20 }, { "revMix", 50 } } },
            { "Eleven Categories X=5", dream + std::vector<KV> { { "dreamOn", 1 }, { "dreamRate", 12 }, { "dreamDensity", 70 }, { "catOn", 1 }, { "catOrder", 1 }, { "catX", 5 }, { "attack", 5 }, { "release", 8 }, { "revMix", 30 } } },
            { "Lissajous senos 3:2", tune (110, 0, 2, { 3, 4, 6, 8 }, { 2, 3, 1, 1 }, { 1, 1, 0, 0, 0 }) + allWaves (1) + std::vector<KV> { { "revMix", 0 } } },

            // ---------------- Espacio y escucha
            { "Binaural theta 4 Hz", triad + allWaves (1) + std::vector<KV> { { "binOn", 1 }, { "binMode", 0 }, { "binHz", 4 }, { "revMix", 8 } } },
            { "Binaural proporcional", dream + std::vector<KV> { { "binOn", 1 }, { "binMode", 1 }, { "binHz", 6 }, { "revMix", 10 } } },
            { "Walking the Dream House", tune (50, 1, 4, { 6, 7, 8, 12 }, { 2, 3, 7, 1 }) + allWaves (1) + std::vector<KV> { { "invAmp", 1 }, { "walkOn", 1 }, { "walkSpeed", 0.35f }, { "walkDepth", 90 }, { "revMix", 10 } } },

            // ---------------- Diseño de sonido
            { "Shimmer Tortoise", dream + std::vector<KV> { { "revModel", 4 }, { "revShimmer", 60 }, { "revTime", 12 }, { "revMix", 50 }, { "attack", 6 } } },
            { "Granular Tortoise", lake + std::vector<KV> { { "wtable", 9 }, { "fxType", 5 }, { "fxMix", 55 }, { "fxP1", 60 }, { "fxP2", 45 }, { "fxP3", 50 }, { "revMix", 35 } } },
            { "Tape Delay Tanpura", tune (29.135f, 2, 4, { 6, 8, 7, 12 }, { 2, 3, 7, 1 }) + std::vector<KV> { { "wtable", 9 }, { "wpos", 60 }, { "fxType", 4 }, { "fxMix", 45 }, { "fxP1", 65 }, { "fxP2", 55 }, { "fxP3", 40 }, { "revMix", 25 } } },
            { "Sub 7-3-2", triad + std::vector<KV> { { "wtable", 0 }, { "fxType", 6 }, { "fxMix", 100 }, { "fxP1", 20 }, { "fxP2", 70 }, { "fxP3", 30 }, { "revMix", 20 } } },
            { "Resonator 31", lim31 + std::vector<KV> { { "wtable", 11 }, { "wpos", 50 }, { "fxType", 7 }, { "fxMix", 55 }, { "fxP1", 85 }, { "fxP2", 55 }, { "fxP3", 0 }, { "revMix", 30 } } },
            { "Phaser Organ", wtpTet + std::vector<KV> { { "wtable", 5 }, { "wpos", 50 }, { "fxType", 2 }, { "fxMix", 60 }, { "fxP1", 20 }, { "fxP2", 70 }, { "fxP3", 50 }, { "revMix", 25 } } },
            { "Chorus Strings 16:17:18", d171 + std::vector<KV> { { "wtable", 4 }, { "fxType", 1 }, { "fxMix", 50 }, { "fxP1", 30 }, { "fxP2", 50 }, { "fxP3", 70 }, { "revMix", 35 } } },
            { "FM desde la fundamental", dream + std::vector<KV> { { "fmSrc1", 3 }, { "fmSrc2", 3 }, { "fmSrc3", 3 }, { "fmSrc4", 3 }, { "fmDepth1", 20 }, { "fmDepth2", 15 }, { "fmDepth3", 25 }, { "fmDepth4", 10 }, { "fmRange", 1 }, { "revMix", 25 } } },
            { "Sine Fold 189/98", lake + std::vector<KV> { { "wtable", 10 }, { "wlfoDepth", 70 }, { "wlfoRate", 0.03f }, { "wspread", 60 }, { "revMix", 35 } } },
            { "Temperado (comparacion)", dream + std::vector<KV> { { "temper", 100 }, { "revMix", 20 } } },

            // ---------------- 1.2: efectos inspirados en el firmware del Deluge
            { "Dattorro Plate 7-3-2", triad + std::vector<KV> { { "wtable", 4 }, { "wpos", 30 }, { "revModel", 5 }, { "revTime", 8 }, { "revMix", 45 }, { "revDamp", 40 } } },
            { "Rings Dream House", dream + std::vector<KV> { { "attack", 6 }, { "release", 10 }, { "revModel", 6 }, { "revTime", 20 }, { "revMix", 55 }, { "revModDep", 60 } } },
            { "Harmonic Flanger 21/16", dream + std::vector<KV> { { "wtable", 2 }, { "wpos", 40 }, { "fxType", 8 }, { "fxMix", 60 }, { "fxP1", 0 }, { "fxP2", 90 }, { "fxP3", 30 }, { "revMix", 25 } } },
            { "Dimension Bowed 16:17:18", d171 + std::vector<KV> { { "wtable", 4 }, { "fxType", 9 }, { "fxMix", 70 }, { "fxP1", 50 }, { "fxP2", 80 }, { "fxP3", 60 }, { "revModel", 6 }, { "revMix", 35 } } },
            { "Warbler Tape Tortoise", ptdm + std::vector<KV> { { "wtable", 8 }, { "fxType", 10 }, { "fxMix", 55 }, { "fxP1", 20 }, { "fxP2", 30 }, { "fxP3", 30 },
                { "wlfoShape", 1 }, { "wlfoDepth", 40 }, { "wlfoRate", 0.2f }, { "revMix", 30 } } },
            { "Wavefold combination tones", dream + allWaves (1) + std::vector<KV> { { "foldAmt", 45 }, { "foldBias", 20 }, { "foldMix", 70 }, { "revMix", 25 } } },
            { "Harmonic Decimator", triad + allWaves (1) + std::vector<KV> { { "decOn", 1 }, { "decMode", 1 }, { "decHarm", 96 }, { "decBits", 10 }, { "decMix", 70 }, { "revMix", 20 } } },
            { "Ladder harmonic 12", wtpDr + std::vector<KV> { { "wtable", 2 }, { "wpos", 60 }, { "filtOn", 1 }, { "filtType", 0 }, { "filtHarmOn", 1 }, { "filtHarm", 12 },
                { "filtRes", 70 }, { "filtDrive", 30 }, { "revMix", 30 } } },
            { "SVF Notch on 7", dream + std::vector<KV> { { "wtable", 2 }, { "filtOn", 1 }, { "filtType", 2 }, { "filtHarmOn", 1 }, { "filtHarm", 7 }, { "filtRes", 40 }, { "revMix", 25 } } },
            { "SVF Morph sweep (warble)", lake + std::vector<KV> { { "wtable", 0 }, { "wpos", 70 }, { "filtOn", 1 }, { "filtType", 1 }, { "filtMorph", 50 }, { "filtHarmOn", 1 },
                { "filtHarm", 10 }, { "filtRes", 55 }, { "fmSrc1", 1 }, { "fmLfoShape", 5 }, { "fmDepth1", 20 }, { "revModel", 5 }, { "revMix", 40 } } },
            { "Granular reverse into Rings", lake + std::vector<KV> { { "wtable", 9 }, { "fxType", 5 }, { "fxMix", 55 }, { "fxP1", 55 }, { "fxP2", 45 }, { "fxP3", 50 },
                { "fxP4", 50 }, { "fxP5", 50 }, { "revModel", 6 }, { "revTime", 15 }, { "revMix", 40 } } },
            { "Dream generativo comprimido", dream + std::vector<KV> { { "dreamOn", 1 }, { "dreamRate", 15 }, { "dreamDensity", 65 }, { "attack", 4 }, { "release", 6 },
                { "compOn", 1 }, { "compThresh", -24 }, { "compRatio", 3 }, { "compRelease", 800 }, { "compBlend", 70 }, { "compMakeup", 4 },
                { "eqBass", 3 }, { "eqTreble", -3 }, { "revModel", 6 }, { "revMix", 40 } } },
        };
        return p;
    }
}
