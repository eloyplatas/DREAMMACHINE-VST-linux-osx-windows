#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Wavetables.h"

namespace params
{
    inline const char* voiceTag (int v) { static const char* t[] = { "FUND", "T1", "T2", "T3", "T4" }; return t[v]; }
    inline juce::String vid (const char* base, int v) { return juce::String (base) + juce::String (v); }

    inline juce::StringArray waveChoices()  { return { "Wavetable", "Sine", "Triangle", "Square", "Saw" }; }
    inline juce::StringArray fmChoices()    { return { "Off", "LFO", "Random", "Fundamental", "Input L", "Input R" }; }
    // los nuevos tipos se añaden al final para no romper proyectos guardados
    inline juce::StringArray fxChoices()    { return { "Off", "Chorus", "Phaser", "Delay", "Tape Delay", "Granulator", "Sub Bass", "Resonator",
                                                       "Harm Flanger", "Dimension", "Warbler" }; }
    // nombres de los 5 parámetros del efecto según el tipo ("-" = no se usa)
    inline juce::StringArray fxParamNames (int type)
    {
        switch (type)
        {
            case 1:  return { "Rate", "Depth", "Spread", "-", "-" };
            case 2:  return { "Rate", "Depth", "Feedback", "-", "-" };
            case 3:  return { "Time", "Feedback", "Tone", "-", "-" };
            case 4:  return { "Time", "Feedback", "Wow", "-", "-" };
            case 5:  return { "Size", "Density", "Pitch JI", "Reverse", "To verb" };
            case 6:  return { "Octave", "Level", "Drive", "-", "-" };
            case 7:  return { "Decay", "Bright", "Octave", "-", "-" };
            case 8:  return { "Periods", "Feedback", "Sweep", "-", "-" };
            case 9:  return { "Mode", "Width", "Tone", "-", "-" };
            case 10: return { "Rate", "Depth", "Feedback", "-", "-" };
            default: return { "-", "-", "-", "-", "-" };
        }
    }
    inline juce::StringArray reverbChoices() { return { "Room", "Hall", "Plate", "Dream House", "Shimmer", "Dattorro", "Rings" }; }
    inline juce::StringArray tableChoices()
    {
        juce::StringArray s;
        for (auto& n : wavetables::builtInNames()) s.add (n);
        s.add ("USER WAV");
        return s;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout L;
        auto fl = [&L] (const String& id, const String& name, float lo, float hi, float def, float skewCentre = 0.0f,
                        const String& label = {}, float step = 0.0f)
        {
            NormalisableRange<float> r (lo, hi, step);
            if (skewCentre > 0.0f) r.setSkewForCentre (skewCentre);
            L.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                     AudioParameterFloatAttributes().withLabel (label)));
        };
        auto in = [&L] (const String& id, const String& name, int lo, int hi, int def)
        { L.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
        auto bo = [&L] (const String& id, const String& name, bool def)
        { L.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
        auto ch = [&L] (const String& id, const String& name, const StringArray& c, int def)
        { L.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def)); };

        // --- Afinación
        fl ("fund", "Fundamental", 1.0f, 2000.0f, 29.135f, 110.0f, "Hz", 0.001f);
        in ("octave", "Octave", -8, 8, 1);
        in ("transpose", "Transpose", -60, 60, 0);
        fl ("fine", "Fine tune", -100.0f, 100.0f, 0.0f, 0.0f, "ct");
        fl ("temper", "JI > 12-TET", 0.0f, 100.0f, 0.0f, 0.0f, "%");
        in ("den", "Denominator", 1, 256, 32);
        const int defNum[] = { 42, 56, 62, 63 };
        for (int i = 1; i <= 4; ++i) in (vid ("num", i), "Numerator " + String (i), 1, 1024, defNum[i - 1]);
        const int defP[] = { 2, 3, 7, 31 };
        for (int i = 1; i <= 4; ++i) in (vid ("prime", i), "Prime " + String (i), 1, 199, defP[i - 1]);
        bo ("primeSnap", "Prime snap", false);

        // --- Voces (0 = fundamental)
        for (int v = 0; v < 5; ++v)
        {
            const String t = voiceTag (v);
            bo (vid ("gate", v), t + " Gate", false);
            fl (vid ("gain", v), t + " Gain", -40.0f, 6.0f, 0.0f, 0.0f, "dB");
            fl (vid ("pan", v), t + " Pan", -100.0f, 100.0f, 0.0f, 0.0f, "%");
            ch (vid ("wave", v), t + " Waveform", waveChoices(), 0);
            ch (vid ("fmSrc", v), t + " FM source", fmChoices(), 0);
            fl (vid ("fmDepth", v), t + " FM depth", 0.0f, 100.0f, 0.0f, 0.0f, "%");
        }

        // --- Wavetable
        ch ("wtable", "Wavetable", tableChoices(), 0);
        fl ("wpos", "Wave position", 0.0f, 100.0f, 0.0f);
        fl ("wspread", "Wave spread", -100.0f, 100.0f, 0.0f);
        fl ("wlfoRate", "Wave LFO rate", 0.01f, 10.0f, 0.1f, 0.5f, "Hz");
        fl ("wlfoDepth", "Wave LFO depth", 0.0f, 100.0f, 0.0f, 0.0f, "%");

        // --- Envolventes y transiciones
        fl ("attack", "Attack", 0.0f, 60.0f, 1.0f, 3.0f, "s");
        fl ("release", "Release", 0.0f, 60.0f, 3.0f, 3.0f, "s");
        fl ("slew", "Frequency slew", 0.0f, 30.0f, 0.0f, 2.0f, "s");
        fl ("xfade", "Crossfade time", 0.0f, 30.0f, 0.0f, 2.0f, "s");

        // --- FM
        ch ("fmRange", "FM range", { "1 Hz", "10 Hz", "100 Hz", "1 kHz" }, 0);
        fl ("fmLfoRate", "FM LFO rate", 0.01f, 50.0f, 0.2f, 1.0f, "Hz");
        ch ("fmLfoShape", "FM LFO shape", { "Sine", "Triangle", "Square", "Saw", "S&H", "Warble" }, 0);

        // --- Reverb
        fl ("revMix", "Reverb mix", 0.0f, 100.0f, 20.0f, 0.0f, "%");
        fl ("revTime", "Reverb time", 0.4f, 30.0f, 1.7f, 4.0f, "s");
        ch ("revModel", "Reverb model", reverbChoices(), 1);
        fl ("revSize", "Reverb size", 1.0f, 100.0f, 100.0f, 0.0f, "%");
        fl ("revDamp", "Reverb high damp", 0.0f, 100.0f, 60.0f);
        fl ("revModSpd", "Reverb mod speed", 0.01f, 5.0f, 2.5f, 1.0f, "Hz");
        fl ("revModDep", "Reverb mod depth", 0.0f, 100.0f, 25.0f);
        fl ("revEarly", "Reverb early gain", -40.0f, 6.0f, -12.0f, 0.0f, "dB");
        fl ("revDiff", "Reverb diffuse gain", -40.0f, 6.0f, -6.0f, 0.0f, "dB");

        // --- Modo Dream (generativo, entradas y salidas independientes)
        bo ("dreamOn", "Dream mode", false);
        fl ("dreamRate", "Dream change time", 1.0f, 600.0f, 20.0f, 30.0f, "s");
        fl ("dreamDensity", "Dream density", 0.0f, 100.0f, 60.0f, 0.0f, "%");
        bo ("dreamFund", "Dream includes fundamental", false);

        // --- Permutaciones (técnica del sopranino)
        bo ("arpOn", "Permutation", false);
        fl ("arpRate", "Permutation rate", 0.5f, 40.0f, 8.0f, 6.0f, "Hz");
        ch ("arpMode", "Permutation mode", { "Up", "Down", "Up/Down", "Random", "Permutation" }, 4);
        fl ("arpSmooth", "Permutation smooth", 1.0f, 200.0f, 8.0f, 20.0f, "ms");
        in ("arpOct", "Permutation octaves", 0, 3, 0);
        bo ("arpFund", "Permutation includes fundamental", false);

        // --- Tonos de diferencia
        fl ("diffAmt", "Difference tones", 0.0f, 100.0f, 0.0f, 0.0f, "%");
        fl ("diffDrive", "Difference drive", 0.0f, 100.0f, 30.0f, 0.0f, "%");

        // --- Énfasis armónico (drone vocal)
        fl ("vocAmt", "Harmonic emphasis", 0.0f, 100.0f, 0.0f, 0.0f, "%");
        in ("vocHarm", "Emphasis harmonic", 1, 32, 6);
        fl ("vocQ", "Emphasis Q", 1.0f, 60.0f, 12.0f, 10.0f);
        fl ("vocDrift", "Emphasis drift", 0.0f, 60.0f, 0.0f, 8.0f, "s");

        fl ("revShimmer", "Reverb shimmer", 0.0f, 100.0f, 50.0f, 0.0f, "%");

        // --- Efecto antes de la reverb
        ch ("fxType", "FX type", fxChoices(), 0);
        fl ("fxMix", "FX mix", 0.0f, 100.0f, 40.0f, 0.0f, "%");
        fl ("fxP1", "FX param 1", 0.0f, 100.0f, 50.0f);
        fl ("fxP2", "FX param 2", 0.0f, 100.0f, 50.0f);
        fl ("fxP3", "FX param 3", 0.0f, 100.0f, 50.0f);

        // --- Teoría de las Eleven Categories (filtro del modo Dream)
        bo ("catOn", "Categories filter", false);
        in ("catOrder", "Combination order", 1, 3, 2);
        in ("catX", "Excluded factor X", 2, 31, 5);

        // --- Amplitud inversamente proporcional a la frecuencia
        bo ("invAmp", "1/f amplitude", false);

        // --- Espacio: caminar por la sala (ondas estacionarias)
        bo ("walkOn", "Walk", false);
        fl ("walkSpeed", "Walk speed", 0.0f, 2.0f, 0.15f, 0.3f, "m/s");
        fl ("walkPos", "Walk position", 0.0f, 20.0f, 3.0f, 0.0f, "m");
        fl ("walkDepth", "Walk depth", 0.0f, 100.0f, 70.0f, 0.0f, "%");

        // --- Binaural
        bo ("binOn", "Binaural", false);
        ch ("binMode", "Binaural mode", { "Fixed Hz", "Proportional" }, 0);
        fl ("binHz", "Binaural beat", 0.1f, 40.0f, 4.0f, 6.0f, "Hz");

        // --- Random y transiciones
        fl ("rndAmount", "Random amount", 0.0f, 100.0f, 100.0f, 0.0f, "%");
        fl ("rndTime", "Random morph time", 0.0f, 300.0f, 0.0f, 20.0f, "s");
        fl ("presetTime", "Preset morph time", 0.0f, 300.0f, 0.0f, 20.0f, "s");
        ch ("rndScope", "Random scope", { "All", "Sound", "Tuning", "Gates" }, 0);
        bo ("rndTrigger", "Random trigger", false);

        // ================= 1.2 (inspirado en el firmware del Deluge) =================
        // --- Efecto: dos controles más (granulador: reverse y envío a la reverb)
        fl ("fxP4", "FX param 4", 0.0f, 100.0f, 0.0f);
        fl ("fxP5", "FX param 5", 0.0f, 100.0f, 0.0f);
        ch ("wlfoShape", "Wave LFO shape", { "Sine", "Warble" }, 0);

        // --- Filtro global
        bo ("filtOn", "Filter", false);
        ch ("filtType", "Filter type", { "Ladder LP", "SVF Morph", "SVF Notch" }, 0);
        fl ("filtFreq", "Filter cutoff", 20.0f, 20000.0f, 2000.0f, 1000.0f, "Hz");
        bo ("filtHarmOn", "Filter harmonic cutoff", false);
        in ("filtHarm", "Filter harmonic", 1, 64, 8);
        fl ("filtRes", "Filter resonance", 0.0f, 100.0f, 20.0f, 0.0f, "%");
        fl ("filtMorph", "Filter morph", 0.0f, 100.0f, 0.0f, 0.0f, "%");
        fl ("filtDrive", "Filter drive", 0.0f, 100.0f, 0.0f, 0.0f, "%");

        // --- Wavefold sobre la mezcla
        fl ("foldAmt", "Wavefold", 0.0f, 100.0f, 0.0f, 0.0f, "%");
        fl ("foldBias", "Wavefold bias", 0.0f, 100.0f, 0.0f, 0.0f, "%");
        fl ("foldMix", "Wavefold mix", 0.0f, 100.0f, 100.0f, 0.0f, "%");

        // --- Decimador armónico
        bo ("decOn", "Decimator", false);
        ch ("decMode", "Decimator mode", { "Free Hz", "Harmonic" }, 1);
        fl ("decHz", "Decimator rate", 100.0f, 48000.0f, 8000.0f, 4000.0f, "Hz");
        in ("decHarm", "Decimator harmonic", 1, 2048, 1024);
        fl ("decBits", "Decimator bits", 2.0f, 24.0f, 24.0f, 0.0f, "bit");
        fl ("decMix", "Decimator mix", 0.0f, 100.0f, 100.0f, 0.0f, "%");

        // --- EQ de graves y agudos
        fl ("eqBass", "EQ bass", -18.0f, 18.0f, 0.0f, 0.0f, "dB");
        fl ("eqBassFreq", "EQ bass freq", 20.0f, 1000.0f, 120.0f, 150.0f, "Hz");
        fl ("eqTreble", "EQ treble", -18.0f, 18.0f, 0.0f, 0.0f, "dB");
        fl ("eqTrebleFreq", "EQ treble freq", 1000.0f, 16000.0f, 6000.0f, 5000.0f, "Hz");

        // --- Compresor
        bo ("compOn", "Compressor", false);
        fl ("compThresh", "Comp threshold", -60.0f, 0.0f, -18.0f, 0.0f, "dB");
        fl ("compRatio", "Comp ratio", 1.0f, 20.0f, 4.0f, 4.0f, ":1");
        fl ("compAttack", "Comp attack", 0.1f, 200.0f, 10.0f, 15.0f, "ms");
        fl ("compRelease", "Comp release", 10.0f, 3000.0f, 250.0f, 300.0f, "ms");
        fl ("compHpf", "Comp sidechain HPF", 20.0f, 500.0f, 20.0f, 100.0f, "Hz");
        fl ("compBlend", "Comp blend", 0.0f, 100.0f, 100.0f, 0.0f, "%");
        fl ("compMakeup", "Comp makeup", 0.0f, 24.0f, 0.0f, 0.0f, "dB");

        // --- Master y MIDI
        fl ("master", "Master gain", -40.0f, 6.0f, -6.0f, 0.0f, "dB");
        ch ("midiMode", "MIDI mode", { "Off", "Note > Fundamental", "Note > Fund + Gate", "Notes > Gates" }, 0);
        in ("bendRange", "Bend range", 0, 24, 2);

        return L;
    }
}
