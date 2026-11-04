// Fuzz: mueve parámetros al azar mientras suena y detecta NaN/Inf o silencios inesperados.
#include "../Source/PluginProcessor.h"
#include <iostream>
using namespace juce;

static void setN (DreamMachineProcessor& p, RangedAudioParameter* r, float n) { r->setValueNotifyingHost (n); }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const int seeds = argc > 1 ? atoi (argv[1]) : 20;
    const double sr = 48000; const int bs = 256;
    int problems = 0, totalResets = 0;
    for (int seed = 1; seed <= seeds; ++seed)
    {
        DreamMachineProcessor proc;
        proc.setRateAndBufferSizeDetails (sr, bs);
        proc.prepareToPlay (sr, bs);
        Random R (seed);
        AudioBuffer<float> buf (2, bs); MidiBuffer midi;
        Array<RangedAudioParameter*> ps;
        for (auto* p : proc.getParameters()) if (auto* r = dynamic_cast<RangedAudioParameter*> (p)) ps.add (r);
        auto get = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
        // estado base: todo sonando
        for (int v = 0; v < 5; ++v) proc.apvts.getParameter ("gate" + String (v))->setValueNotifyingHost (1.0f);
        proc.apvts.getParameter ("attack")->setValueNotifyingHost (0.0f);
        int silentBlocks = 0; String lastChanges;
        for (int b = 0; b < 48000 * 40 / bs; ++b)   // 40 s
        {
            // "mover parámetros": 2 cambios por bloque, como al arrastrar
            if (R.nextInt (4) == 0)
            {
                auto* r = ps[R.nextInt (ps.size())];
                const String id = r->getParameterID();
                if (id.startsWith ("gate") || id == "master" || id == "midiMode" || id.startsWith ("gain") || id == "rndTrigger" || id == "dreamOn") continue;
                const float n = R.nextFloat();
                setN (proc, r, n);
                lastChanges = (lastChanges + " " + id + "=" + String (r->convertFrom0to1 (n), 2)).getLastCharacters (600);
            }
            buf.clear(); proc.processBlock (buf, midi);
            float peak = 0; bool bad = false;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float x = buf.getSample (c, i); if (! std::isfinite (x)) bad = true; peak = jmax (peak, std::abs (x)); }
            if (bad)
            {
                std::cout << "seed " << seed << " NAN/INF en t=" << b * bs / sr << " s\n  ultimos cambios:" << lastChanges << std::endl;
                ++problems; break;
            }
            silentBlocks = peak < 1e-5f ? silentBlocks + 1 : 0;
            if (silentBlocks * bs / sr > 2.0)
            {
                std::cout << "seed " << seed << " SILENCIO >2 s en t=" << b * bs / sr << " s  (dream " << get ("dreamOn") << " cat " << get ("catOn")
                          << " arp " << get ("arpOn") << " midi " << get ("midiMode") << " fx " << get ("fxType") << " fxMix " << get ("fxMix") << ")\n  ultimos cambios:" << lastChanges << std::endl;
                ++problems; break;
            }
        }
        totalResets += proc.safetyResets.load();
    }
    std::cout << "reinicios de seguridad: " << totalResets << std::endl;
    std::cout << "problemas: " << problems << " de " << seeds << std::endl;
    return 0;
}
