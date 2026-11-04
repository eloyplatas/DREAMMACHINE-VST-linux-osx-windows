#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <random>
#include <map>
#include "Params.h"
#include "Wavetables.h"
#include "Reverb.h"
#include "Fx.h"
#include "Tuning.h"
#include "CombTones.h"
#include "FactoryPresets.h"
#include "Dsp2.h"

class DreamMachineProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int NV = 5;
    static constexpr int SCOPE_N = 32768;

    DreamMachineProcessor();
    ~DreamMachineProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dream Machine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return (int) factory::patches().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- wavetable de usuario
    bool loadUserWavetable (const juce::File& f);
    juce::String getUserWavetableName() const { return userWavPath.isEmpty() ? juce::String ("(ninguna)") : juce::File (userWavPath).getFileName(); }

    // --- presets (todas las llamadas desde el hilo de mensajes)
    static juce::File userPresetDir();
    juce::Array<juce::File> userPresets() const;
    void loadFactory (int index);
    bool loadPresetFile (const juce::File& f);
    juce::File savePreset (const juce::String& name);
    juce::String currentPresetName { "Dream Chord /31" };

    // --- random y transiciones
    void randomize();
    bool isMorphing() const { return morphActive; }
    float morphProgress() const { return morphProgressValue; }

    juce::AudioProcessorValueTreeState apvts;

    // --- estado para la GUI
    struct VoiceView
    {
        std::atomic<float> freq { 0 }, env { 0 };
        std::atomic<bool> gate { false };
        std::atomic<int> num { 1 }, den { 1 };
    };
    std::array<VoiceView, NV> view;
    std::atomic<float> viewBase { 0 }, viewEmphHarm { 0 }, viewCommonHz { 0 }, viewCycles { 0 }, viewWalkX { 0 };
    std::atomic<int> viewArpVoice { -1 };
    std::atomic<bool> viewPeriodic { true };
    std::atomic<int> safetyResets { 0 };
    std::atomic<float> viewGR { 0 }, viewDecHz { 0 }, viewFiltHz { 0 };
    std::atomic<juce::uint32> allowedMask { 0xffffffffu };
    std::array<std::vector<float>, NV> scope;
    std::atomic<int> scopeWrite { 0 };

private:
    struct Voice
    {
        double phaseA = 0, phaseB = 0, phaseR = 0;
        double curLog = 0;
        double oldFreq = 0;
        double xfade = 1.0;
        double lastTarget = -1;
        float env = 0;
        float arpAmp = 1.0f;
        float octMul = 1.0f;
        float rndCur = 0, rndTarget = 0; double rndPh = 0;
        bool dreamState = true;
        double dreamTimer = 0;
        bool midiGate = false;
        float wL = 1, wR = 1;      // ganancias de "walk"
    };

    struct MorphItem { juce::RangedAudioParameter* p; float from, to; bool discrete; bool done; };

    void timerCallback() override;
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    float pv (const char* base, int v) const { return apvts.getRawParameterValue (params::vid (base, v))->load(); }

    void handleMidi (const juce::MidiBuffer& midi);
    void startMorph (const std::map<juce::String, float>& plainTargets, double seconds);
    std::map<juce::String, float> defaultTargets() const;
    static bool isPerformanceParam (const juce::String& id);
    void recomputeCategories();
    std::array<int, NV> currentNums (int& denOut) const;

    std::array<Voice, NV> voices;
    std::array<std::atomic<float>*, NV> pGate {}, pGain {}, pPan {}, pWave {}, pFmSrc {}, pFmDepth {};

    double sr = 48000.0;
    DreamReverb reverb;
    DreamFx fx;
    std::mt19937 rng { 1234567u };
    std::uniform_real_distribution<float> uni { 0.0f, 1.0f };
    juce::Random msgRandom;

    double fmLfoPh = 0, waveLfoPh = 0; float fmSH = 0;
    float dcX = 0, dcY = 0;
    float svfIc1[2] = { 0, 0 }, svfIc2[2] = { 0, 0 };
    double emphK = 6, emphTarget = 6, emphTimer = 0;
    double arpPh = 0; int arpIdx = -1; int arpDir = 1; std::vector<int> arpOrder; int arpPos = 0;
    std::vector<int> heldNotes; int lastNote = -1; float bend = 0;
    bool prevDreamOn = false;
    double walkX = 3.0; double walkDir = 1.0;
    double chordAge = 0; juce::int64 chordHash = 0;

    // morph (hilo de mensajes)
    std::vector<MorphItem> morph;
    double morphStart = 0, morphDur = 0;
    bool morphActive = false;
    float morphProgressValue = 0;
    bool lastTrigger = false;
    juce::int64 catHash = -1;

    std::shared_ptr<WaveTable> userTable, audioUserTable;
    std::vector<std::shared_ptr<WaveTable>> retiredTables;
    juce::SpinLock tableLock;
    juce::String userWavPath;
    int currentProgram = 0;
    juce::AudioBuffer<float> mixBuf, sendBuf;
    // 1.2
    std::array<dsp2::Ladder, 2> ladder;
    std::array<dsp2::Svf, 2> fsvf;
    std::array<dsp2::Biquad, 2> eqLo, eqHi;
    dsp2::Compressor comp;
    dsp2::WarbleLfo waveWarble { 11u }, fmWarble { 12u };
    double decPh = 0; float decHoldL = 0, decHoldR = 0;
    float foldDcX[2] = { 0, 0 }, foldDcY[2] = { 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamMachineProcessor)
};
