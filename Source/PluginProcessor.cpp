#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static constexpr double kTwoPi = 6.283185307179586;

DreamMachineProcessor::DreamMachineProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Main", AudioChannelSet::stereo(), true)
                        .withOutput ("Fund + Env", AudioChannelSet::stereo(), false)
                        .withInput  ("Mod In", AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "DreamMachine", params::createLayout())
{
    for (int v = 0; v < NV; ++v)
    {
        pGate[(size_t) v]    = apvts.getRawParameterValue (params::vid ("gate", v));
        pGain[(size_t) v]    = apvts.getRawParameterValue (params::vid ("gain", v));
        pPan[(size_t) v]     = apvts.getRawParameterValue (params::vid ("pan", v));
        pWave[(size_t) v]    = apvts.getRawParameterValue (params::vid ("wave", v));
        pFmSrc[(size_t) v]   = apvts.getRawParameterValue (params::vid ("fmSrc", v));
        pFmDepth[(size_t) v] = apvts.getRawParameterValue (params::vid ("fmDepth", v));
        scope[(size_t) v].assign (SCOPE_N, 0.0f);
    }
    wavetables::builtIn();
    startTimerHz (30);
}

bool DreamMachineProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
    if (l.outputBuses.size() > 1)
    {
        const auto aux = l.getChannelSet (false, 1);
        if (! aux.isDisabled() && aux != AudioChannelSet::stereo()) return false;
    }
    if (l.inputBuses.size() > 0)
    {
        const auto in = l.getChannelSet (true, 0);
        if (! in.isDisabled() && in != AudioChannelSet::stereo() && in != AudioChannelSet::mono()) return false;
    }
    return true;
}

void DreamMachineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    reverb.prepare (sr);
    fx.prepare (sr);
    mixBuf.setSize (2, jmax (1, samplesPerBlock));
    sendBuf.setSize (2, jmax (1, samplesPerBlock));
    comp.reset();
    for (int c = 0; c < 2; ++c) { ladder[c].reset(); fsvf[c].reset(); eqLo[c].reset(); eqHi[c].reset(); }
    for (auto& v : voices) { v.curLog = 0; v.lastTarget = -1; v.env = 0; v.xfade = 1.0; v.arpAmp = 1.0f; }
}

// --------------------------------------------------------------------------
void DreamMachineProcessor::handleMidi (const MidiBuffer& midi)
{
    const int mode = (int) p ("midiMode");
    static const int gateNotes[NV] = { 48, 50, 52, 53, 55 };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (mode == 1 || mode == 2)
            {
                heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), n), heldNotes.end());
                heldNotes.push_back (n);
                lastNote = n;
            }
            else if (mode == 3)
                for (int v = 0; v < NV; ++v) if (gateNotes[v] == n) voices[(size_t) v].midiGate = true;
        }
        else if (m.isNoteOff())
        {
            const int n = m.getNoteNumber();
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), n), heldNotes.end());
            if (! heldNotes.empty()) lastNote = heldNotes.back();
            for (int v = 0; v < NV; ++v) if (gateNotes[v] == n) voices[(size_t) v].midiGate = false;
        }
        else if (m.isPitchWheel())
            bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldNotes.clear();
            for (auto& v : voices) v.midiGate = false;
        }
    }
}

std::array<int, DreamMachineProcessor::NV> DreamMachineProcessor::currentNums (int& denOut) const
{
    std::array<int, 4> primes;
    for (int i = 0; i < 4; ++i) primes[(size_t) i] = (int) pv ("prime", i + 1);
    const bool snap = p ("primeSnap") > 0.5f;
    int den = (int) p ("den");
    if (snap) den = tuning::snap (den, 1, 256, primes);
    std::array<int, NV> num { den, 0, 0, 0, 0 };
    for (int i = 1; i < NV; ++i)
    {
        int nn = (int) pv ("num", i);
        if (snap) nn = tuning::snap (nn, 1, 1024, primes);
        num[(size_t) i] = nn;
    }
    denOut = den;
    return num;
}

// --------------------------------------------------------------------------
void DreamMachineProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (mixBuf.getNumSamples() < n) mixBuf.setSize (2, n, false, false, true);

    float* inL = mixBuf.getWritePointer (0);
    float* inR = mixBuf.getWritePointer (1);
    FloatVectorOperations::clear (inL, n);
    FloatVectorOperations::clear (inR, n);
    if (getBusCount (true) > 0 && getBus (true, 0)->isEnabled())
    {
        auto in = getBusBuffer (buffer, true, 0);
        if (in.getNumChannels() > 0) FloatVectorOperations::copy (inL, in.getReadPointer (0), n);
        if (in.getNumChannels() > 1) FloatVectorOperations::copy (inR, in.getReadPointer (1), n);
        else FloatVectorOperations::copy (inR, inL, n);
    }
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    handleMidi (midi);

    {
        const SpinLock::ScopedTryLockType tl (tableLock);
        if (tl.isLocked()) audioUserTable = userTable;
    }

    const double dt = n / sr;

    // --- afinación
    int den = 1;
    const auto num = currentNums (den);
    const int midiMode = (int) p ("midiMode");
    double base;
    if ((midiMode == 1 || midiMode == 2) && lastNote >= 0)
        base = MidiMessage::getMidiNoteInHertz (lastNote) * std::pow (2.0, bend * p ("bendRange") / 12.0);
    else
        base = p ("fund") * std::pow (2.0, (double) p ("octave"));
    base *= std::pow (2.0, p ("transpose") / 12.0 + p ("fine") / 1200.0);
    const double temper = p ("temper") / 100.0;

    auto coefFor = [this] (float seconds) { const double t = jmax (0.001, (double) seconds / 4.6); return (float) (1.0 - std::exp (-1.0 / (t * sr))); };
    const float aCoef = coefFor (p ("attack"));
    const float rCoef = coefFor (p ("release"));
    const float slewT = p ("slew");
    const float slewCoef = slewT <= 0.0f ? 1.0f : coefFor (slewT);
    const float xfadeT = p ("xfade");
    const double xfadeInc = xfadeT > 0.0f ? 1.0 / (xfadeT * sr) : 1.0;

    static const float fmRanges[] = { 1.0f, 10.0f, 100.0f, 1000.0f };
    const float fmRange = fmRanges[jlimit (0, 3, (int) p ("fmRange"))];
    const float fmRate = p ("fmLfoRate");
    const int fmShape = (int) p ("fmLfoShape");
    const float rndCoef = (float) (1.0 - std::exp (-fmRate * 3.0 / sr));

    const int tIdx = (int) p ("wtable");
    const auto& builtIn = wavetables::builtIn();
    const WaveTable* mainTable = tIdx < (int) builtIn.size() ? builtIn[(size_t) tIdx].get()
                               : (audioUserTable ? audioUserTable.get() : builtIn[0].get());
    const float wpos = p ("wpos") / 100.0f, wspread = p ("wspread") / 100.0f;
    const float wlfoRate = p ("wlfoRate"), wlfoDepth = p ("wlfoDepth") / 100.0f;
    const bool wlfoWarble = (int) p ("wlfoShape") == 1;

    // --- puertas manuales / MIDI
    std::array<bool, NV> manual {};
    for (int v = 0; v < NV; ++v)
    {
        bool g = pGate[(size_t) v]->load() > 0.5f;
        if (midiMode == 2) g = g && ! heldNotes.empty();
        if (midiMode == 3) g = g || voices[(size_t) v].midiGate;
        manual[(size_t) v] = g;
    }

    // --- modo Dream (+ filtro de las Eleven Categories)
    const bool dreamOn = p ("dreamOn") > 0.5f;
    const bool dreamFund = p ("dreamFund") > 0.5f;
    const double dreamMean = p ("dreamRate");
    const float density = p ("dreamDensity") / 100.0f;
    const bool catOn = p ("catOn") > 0.5f;
    const uint32 allowed = allowedMask.load();
    auto maskWith = [&] (int which, bool state)
    {
        uint32 m = 0;
        for (int v = 0; v < NV; ++v)
        {
            bool on = manual[(size_t) v];
            if (dreamOn && (v > 0 || dreamFund)) on = on && (v == which ? state : voices[(size_t) v].dreamState);
            if (on) m |= 1u << v;
        }
        return m;
    };
    if (dreamOn && ! prevDreamOn)
        for (auto& v : voices) { v.dreamState = uni (rng) < density; v.dreamTimer = dreamMean * (0.2 + uni (rng)); }
    prevDreamOn = dreamOn;
    if (dreamOn)
    {
        for (int i = 0; i < NV; ++i)
        {
            auto& v = voices[(size_t) i];
            v.dreamTimer -= dt;
            if (v.dreamTimer <= 0.0)
            {
                const bool proposal = uni (rng) < density;
                if (! catOn || ((allowed >> maskWith (i, proposal)) & 1u)) v.dreamState = proposal;
                v.dreamTimer = -std::log (jmax (1e-6f, uni (rng))) * dreamMean;
            }
        }
    }

    std::array<bool, NV> eff {};
    for (int v = 0; v < NV; ++v)
    {
        bool g = manual[(size_t) v];
        if (dreamOn && (v > 0 || dreamFund)) g = g && voices[(size_t) v].dreamState;
        eff[(size_t) v] = g;
    }

    // --- permutación
    const bool arpOn = p ("arpOn") > 0.5f;
    const bool arpFund = p ("arpFund") > 0.5f;
    const int arpMode = (int) p ("arpMode");
    const int arpOct = (int) p ("arpOct");
    const double arpInc = p ("arpRate") / sr;
    const float arpCoef = coefFor (p ("arpSmooth") * 0.001f);
    std::vector<int> cand;
    for (int v = 0; v < NV; ++v) if (eff[(size_t) v] && (v > 0 || arpFund)) cand.push_back (v);
    auto arpStep = [&]
    {
        if (cand.empty()) { arpIdx = -1; return; }
        const int k = (int) cand.size();
        auto posOf = [&] (int v) { for (int i = 0; i < k; ++i) if (cand[(size_t) i] == v) return i; return -1; };
        int cur = posOf (arpIdx);
        switch (arpMode)
        {
            case 0: cur = (cur + 1) % k; break;
            case 1: cur = cur < 0 ? k - 1 : (cur - 1 + k) % k; break;
            case 2:
                if (k == 1) { cur = 0; break; }
                if (cur < 0) cur = 0;
                if (cur + arpDir >= k || cur + arpDir < 0) arpDir = -arpDir;
                cur += arpDir; break;
            case 3: { int r = (int) (uni (rng) * k) % k; if (k > 1 && r == cur) r = (r + 1) % k; cur = r; } break;
            default:
            {
                bool same = (int) arpOrder.size() == k;
                if (same) for (int v : arpOrder) same = same && posOf (v) >= 0;
                if (! same || arpPos >= (int) arpOrder.size())
                {
                    arpOrder = cand;
                    for (int i = k - 1; i > 0; --i) std::swap (arpOrder[(size_t) i], arpOrder[(size_t) ((int) (uni (rng) * (i + 1)) % (i + 1))]);
                    if (k > 1 && arpOrder[0] == arpIdx) std::swap (arpOrder[0], arpOrder[1]);
                    arpPos = 0;
                }
                cur = posOf (arpOrder[(size_t) arpPos++]);
            }
        }
        arpIdx = cand[(size_t) jlimit (0, k - 1, cur)];
        voices[(size_t) arpIdx].octMul = (float) std::pow (2.0, (int) (uni (rng) * (arpOct + 1)) % (arpOct + 1));
    };
    if (arpOn && (arpIdx < 0 || ! eff[(size_t) arpIdx])) arpStep();
    if (! arpOn) arpIdx = -1;

    // --- frecuencias objetivo
    std::array<double, NV> target {};
    std::array<float, NV> invGain {};
    const bool invAmp = p ("invAmp") > 0.5f;
    for (int v = 0; v < NV; ++v)
    {
        const double ratio = (double) num[(size_t) v] / (double) jmax (1, den);
        double c = tuning::cents (ratio);
        c += temper * (std::round (c / 100.0) * 100.0 - c);
        const float om = arpOn ? voices[(size_t) v].octMul : 1.0f;
        target[(size_t) v] = jlimit (0.01, sr * 0.45, base * std::pow (2.0, c / 1200.0) * om);
        invGain[(size_t) v] = invAmp ? (float) jlimit (0.05, 4.0, 1.0 / (ratio * om)) : 1.0f;
    }

    // --- periodo compuesto y ciclos (Young: la afinación es función del tiempo)
    double commonHz = 0.0;
    {
        int g = 0; uint32 m = 0;
        for (int v = 0; v < NV; ++v) if (eff[(size_t) v]) { g = std::gcd (g, num[(size_t) v]); m |= 1u << v; }
        const double common = g > 0 ? base * g / jmax (1, den) : 0.0;
        commonHz = common;
        int64 h = (int64) m;
        for (int v = 0; v < NV; ++v) h = h * 1315423911LL + num[(size_t) v];
        h = h * 31 + den + (int64) std::llround (base * 1000.0);
        if (h != chordHash) { chordHash = h; chordAge = 0; }
        else chordAge += dt;
        viewCommonHz.store ((float) common);
        viewCycles.store ((float) (chordAge * common));
        viewPeriodic.store (temper <= 0.0);
    }

    // --- diferencia / énfasis
    const float diffAmt = p ("diffAmt") / 100.0f;
    const float diffK = diffAmt * (1.0f + 9.0f * p ("diffDrive") / 100.0f);
    const float vocAmt = p ("vocAmt") / 100.0f;
    const int vocHarm = (int) p ("vocHarm");
    const float vocQ = p ("vocQ");
    const float vocDrift = p ("vocDrift");
    if (vocDrift <= 0.0f) emphTarget = vocHarm;
    else
    {
        emphTimer -= dt;
        if (emphTimer <= 0.0)
        {
            emphTarget = 1 + (int) (uni (rng) * jmin (32, vocHarm * 2));
            emphTimer = vocDrift * (0.5 + uni (rng));
        }
    }
    const float emphCoef = vocDrift <= 0.0f ? coefFor (0.05f) : coefFor (vocDrift * 0.3f);

    // --- walk (ondas estacionarias en la sala)
    const bool walkOn = p ("walkOn") > 0.5f;
    const double walkSpeed = p ("walkSpeed");
    const float walkDepth = p ("walkDepth") / 100.0f;
    if (walkSpeed > 0.0)
    {
        walkX += walkDir * walkSpeed * dt;
        if (uni (rng) < dt * 0.05) walkDir = -walkDir;
        if (walkX > 20.0) { walkX = 20.0; walkDir = -1.0; }
        if (walkX < 0.0)  { walkX = 0.0;  walkDir = 1.0; }
    }
    else walkX += (p ("walkPos") - walkX) * jmin (1.0, dt * 4.0);
    viewWalkX.store ((float) walkX);
    std::array<float, NV> wTL {}, wTR {};
    for (int v = 0; v < NV; ++v)
    {
        if (! walkOn) { wTL[(size_t) v] = wTR[(size_t) v] = 1.0f; continue; }
        const double k = kTwoPi * target[(size_t) v] / 343.0;
        const double ph = v * 1.3;
        wTL[(size_t) v] = (float) (1.0 - walkDepth + walkDepth * std::abs (std::cos (k * walkX + ph)));
        wTR[(size_t) v] = (float) (1.0 - walkDepth + walkDepth * std::abs (std::cos (k * (walkX + 0.18) + ph)));
    }
    const float walkCoef = coefFor (0.05f);

    // --- binaural
    const bool binOn = p ("binOn") > 0.5f;
    const bool binProp = (int) p ("binMode") == 1;
    const double binHz = p ("binHz");

    // --- salidas
    auto mainOut = getBusBuffer (buffer, false, 0);
    float* outL = mainOut.getWritePointer (0);
    float* outR = mainOut.getWritePointer (1);
    float* auxL = nullptr; float* auxR = nullptr;
    if (getBusCount (false) > 1 && getBus (false, 1)->isEnabled())
    {
        auto aux = getBusBuffer (buffer, false, 1);
        if (aux.getNumChannels() > 1) { auxL = aux.getWritePointer (0); auxR = aux.getWritePointer (1); }
    }

    std::array<float, NV> gainLin {}, panL {}, panR {};
    std::array<int, NV> wave {}, fmSrc {};
    std::array<float, NV> fmDepth {};
    for (int v = 0; v < NV; ++v)
    {
        const float g = pGain[(size_t) v]->load();
        gainLin[(size_t) v] = (g <= -40.0f ? 0.0f : Decibels::decibelsToGain (g)) * invGain[(size_t) v];
        const float ang = (pPan[(size_t) v]->load() / 100.0f + 1.0f) * MathConstants<float>::pi * 0.25f;
        panL[(size_t) v] = binOn ? 1.0f : std::cos (ang);
        panR[(size_t) v] = binOn ? 1.0f : std::sin (ang);
        wave[(size_t) v] = (int) pWave[(size_t) v]->load();
        fmSrc[(size_t) v] = (int) pFmSrc[(size_t) v]->load();
        fmDepth[(size_t) v] = pFmDepth[(size_t) v]->load() / 100.0f * fmRange;
    }

    const double nyq = sr * 0.5;
    float fundSample = 0.0f;
    int sw = scopeWrite.load();

    for (int s = 0; s < n; ++s)
    {
        if (arpOn)
        {
            arpPh += arpInc;
            if (arpPh >= 1.0) { arpPh -= 1.0; arpStep(); }
        }
        fmLfoPh += fmRate / sr;
        if (fmLfoPh >= 1.0) { fmLfoPh -= 1.0; fmSH = uni (rng) * 2.0f - 1.0f; }
        waveLfoPh += wlfoRate / sr;
        if (waveLfoPh >= 1.0) waveLfoPh -= 1.0;
        const float wlfo = wlfoWarble ? waveWarble.tick (wlfoRate, (float) sr) : (float) std::sin (kTwoPi * waveLfoPh);
        const float fmW = fmShape == 5 ? fmWarble.tick (fmRate, (float) sr) : 0.0f;

        float mono = 0.0f, sl = 0.0f, sr_ = 0.0f;
        for (int v = 0; v < NV; ++v)
        {
            auto& vo = voices[(size_t) v];
            const bool g = eff[(size_t) v];
            const float envTarget = g ? 1.0f : 0.0f;
            vo.env += (envTarget - vo.env) * (envTarget > vo.env ? aCoef : rCoef);
            const bool part = arpOn && g && (v > 0 || arpFund);
            const float arpTarget = part ? (v == arpIdx ? 1.0f : 0.0f) : 1.0f;
            vo.arpAmp += (arpTarget - vo.arpAmp) * arpCoef;
            vo.wL += (wTL[(size_t) v] - vo.wL) * walkCoef;
            vo.wR += (wTR[(size_t) v] - vo.wR) * walkCoef;

            const double tgt = target[(size_t) v];
            const double tLog = std::log2 (tgt);
            const bool useXfade = xfadeT > 0.0f && ! binOn;
            if (vo.lastTarget < 0) { vo.curLog = tLog; vo.lastTarget = tgt; }
            else if (std::abs (tuning::cents (tgt / vo.lastTarget)) > 0.5)
            {
                if (useXfade && vo.env > 1e-4f)
                {
                    vo.phaseA = vo.xfade < 1.0 ? vo.phaseB : vo.phaseA;
                    vo.oldFreq = std::exp2 (vo.curLog);
                    vo.curLog = tLog;
                    vo.phaseB = vo.phaseA;
                    vo.xfade = 0.0;
                }
                vo.lastTarget = tgt;
            }
            if (useXfade) vo.curLog = tLog;
            else vo.curLog += (tLog - vo.curLog) * slewCoef;

            if (vo.env < 1e-6f && envTarget == 0.0f && v != 0) { scope[(size_t) v][(size_t) sw] = 0.0f; continue; }

            float fm = 0.0f;
            switch (fmSrc[(size_t) v])
            {
                case 1:
                {
                    const double ph = std::fmod (fmLfoPh + v * 0.2, 1.0);
                    switch (fmShape)
                    {
                        case 0: fm = (float) std::sin (kTwoPi * ph); break;
                        case 1: fm = (float) (1.0 - 4.0 * std::abs (ph - 0.5)); break;
                        case 2: fm = ph < 0.5 ? 1.0f : -1.0f; break;
                        case 3: fm = (float) (2.0 * ph - 1.0); break;
                        case 5: fm = fmW; break;
                        default: fm = fmSH; break;
                    }
                    break;
                }
                case 2:
                    vo.rndPh += fmRate / sr;
                    if (vo.rndPh >= 1.0) { vo.rndPh -= 1.0; vo.rndTarget = uni (rng) * 2.0f - 1.0f; }
                    vo.rndCur += (vo.rndTarget - vo.rndCur) * rndCoef;
                    fm = vo.rndCur; break;
                case 3: fm = fundSample; break;
                case 4: fm = inL[s]; break;
                case 5: fm = inR[s]; break;
                default: break;
            }
            const double f = std::exp2 (vo.curLog) + fm * fmDepth[(size_t) v];

            const WaveTable* tab = wave[(size_t) v] == 0 ? mainTable : wavetables::basic (wave[(size_t) v] - 1).get();
            const float pos = jlimit (0.0f, 1.0f, wpos + wlfo * wlfoDepth * 0.5f + wspread * (float) (v - 2) * 0.25f);

            float sampleL, sampleR;
            if (binOn)
            {
                // cada oído recibe la voz desplazada +/- la mitad del batido
                const double delta = 0.5 * (binProp ? binHz * f / jmax (1.0, base) : binHz);
                const double fL = f - delta, fR = f + delta;
                vo.phaseA += fL / sr; vo.phaseA -= std::floor (vo.phaseA);
                vo.phaseR += fR / sr; vo.phaseR -= std::floor (vo.phaseR);
                sampleL = tab->read (vo.phaseA, pos, nyq / jmax (1.0, std::abs (fL)));
                sampleR = tab->read (vo.phaseR, pos, nyq / jmax (1.0, std::abs (fR)));
            }
            else
            {
                const double maxH = nyq / jmax (1.0, std::abs (f));
                vo.phaseA += f / sr;
                vo.phaseA -= std::floor (vo.phaseA);
                if (vo.xfade < 1.0)
                {
                    const double fOld = vo.oldFreq + fm * fmDepth[(size_t) v];
                    vo.phaseA += (fOld - f) / sr;
                    vo.phaseA -= std::floor (vo.phaseA);
                    vo.phaseB += f / sr;
                    vo.phaseB -= std::floor (vo.phaseB);
                    const float a = tab->read (vo.phaseA, pos, nyq / jmax (1.0, std::abs (fOld)));
                    const float b = tab->read (vo.phaseB, pos, maxH);
                    const float x = (float) vo.xfade;
                    sampleL = a * (1.0f - x) + b * x;
                    vo.xfade += xfadeInc;
                    if (vo.xfade >= 1.0) { vo.xfade = 1.0; vo.phaseA = vo.phaseB; }
                }
                else
                    sampleL = tab->read (vo.phaseA, pos, maxH);
                sampleR = sampleL;
            }

            scope[(size_t) v][(size_t) sw] = sampleL * jmin (1.0f, vo.env * 4.0f);
            if (v == 0)
            {
                fundSample = sampleL;
                if (auxL) { auxL[s] = sampleL; auxR[s] = vo.env; }
            }

            const float amp = vo.env * vo.arpAmp * gainLin[(size_t) v];
            mono += 0.5f * (sampleL + sampleR) * amp;
            sl += sampleL * amp * panL[(size_t) v] * vo.wL;
            sr_ += sampleR * amp * panR[(size_t) v] * vo.wR;
        }
        if (++sw >= SCOPE_N) sw = 0;

        sl *= 0.3f; sr_ *= 0.3f; mono *= 0.3f;

        if (diffK > 0.0f)
        {
            const float d = diffK * mono * mono;
            const float y = d - dcX + 0.9995f * dcY;
            dcX = d; dcY = y;
            sl += 0.5f * y; sr_ += 0.5f * y;
        }

        emphK += (emphTarget - emphK) * emphCoef;
        if (vocAmt > 0.0f)
        {
            const double fc = jlimit (20.0, sr * 0.45, base * emphK);
            const float gg = (float) std::tan (MathConstants<double>::pi * fc / sr);
            const float k = 1.0f / vocQ;
            const float a1 = 1.0f / (1.0f + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
            float* chans[2] = { &sl, &sr_ };
            for (int c = 0; c < 2; ++c)
            {
                const float x = *chans[c];
                const float v3 = x - svfIc2[c];
                const float v1 = a1 * svfIc1[c] + a2 * v3;
                const float v2 = svfIc2[c] + a2 * svfIc1[c] + a3 * v3;
                svfIc1[c] = 2.0f * v1 - svfIc1[c];
                svfIc2[c] = 2.0f * v2 - svfIc2[c];
                *chans[c] = x + vocAmt * 4.0f * k * v1 * (float) std::sqrt (vocQ);
            }
        }

        outL[s] = sl;
        outR[s] = sr_;
    }
    scopeWrite.store (sw);

    // --- wavefold sobre la mezcla: genera sumas y diferencias (tonos de combinación) de las voces
    {
        const float amt = p ("foldAmt") / 100.0f;
        if (amt > 0.0f)
        {
            const float g = 1.0f + amt * 10.0f, bias = p ("foldBias") / 100.0f * 0.6f, mix = p ("foldMix") / 100.0f;
            float* ch[2] = { outL, outR };
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < n; ++s)
                {
                    const float x = ch[c][s];
                    const float y = std::sin (MathConstants<float>::halfPi * (x * g + bias)) * 0.35f;
                    const float yb = y - foldDcX[c] + 0.9995f * foldDcY[c];   // quita la continua que añade el bias
                    foldDcX[c] = y; foldDcY[c] = yb;
                    ch[c][s] = x * (1.0f - mix) + yb * mix;
                }
        }
    }

    // --- filtro global (ladder LP, SVF con morph LP-BP-HP, notch); corte libre o en un armónico
    float filtHz = 0.0f;
    if (p ("filtOn") > 0.5f)
    {
        const bool harm = p ("filtHarmOn") > 0.5f;
        filtHz = (float) jlimit (20.0, sr * 0.45, harm ? base * p ("filtHarm") : (double) p ("filtFreq"));
        const int type = (int) p ("filtType");
        const float res = p ("filtRes") / 100.0f, morph = p ("filtMorph") / 100.0f;
        const float drive = 1.0f + p ("filtDrive") / 100.0f * 9.0f;
        float* ch[2] = { outL, outR };
        if (type == 0)
        {
            const float g = 1.0f - std::exp (-MathConstants<float>::twoPi * filtHz / (float) sr);
            const float k = res * 3.8f;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < n; ++s) ch[c][s] = ladder[c].process (ch[c][s], g, k, drive);
        }
        else
        {
            const float g = std::tan (MathConstants<float>::pi * filtHz / (float) sr);
            const float k = 2.0f - 1.95f * res;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < n; ++s)
                {
                    float x = ch[c][s];
                    if (drive > 1.001f) x = std::tanh (x * drive) / std::sqrt (drive);
                    float lp, bp, hp;
                    fsvf[c].process (x, g, k, lp, bp, hp);
                    float y;
                    if (type == 2) y = lp + hp;
                    else if (morph < 0.5f) { const float t = morph * 2.0f; y = lp * (1.0f - t) + bp * t; }
                    else { const float t = (morph - 0.5f) * 2.0f; y = bp * (1.0f - t) + hp * t; }
                    ch[c][s] = y;
                }
        }
    }
    viewFiltHz.store (filtHz);

    // --- decimador: con frecuencia de muestreo múltiplo de la frecuencia compuesta,
    // el aliasing cae sobre armónicos de la fundamental común
    float decHz = 0.0f;
    if (p ("decOn") > 0.5f)
    {
        const bool harm = (int) p ("decMode") == 1;
        const double ref = commonHz > 0.0 ? commonHz : base;
        decHz = (float) jlimit (20.0, sr, harm ? ref * p ("decHarm") : (double) p ("decHz"));
        const float bits = p ("decBits"), mix = p ("decMix") / 100.0f;
        const float q = bits < 23.9f ? std::pow (2.0f, bits - 1.0f) : 0.0f;
        const double inc = decHz / sr;
        for (int s = 0; s < n; ++s)
        {
            decPh += inc;
            if (decPh >= 1.0 || inc >= 1.0)
            {
                decPh -= std::floor (decPh);
                decHoldL = outL[s]; decHoldR = outR[s];
                if (q > 0.0f) { decHoldL = std::round (decHoldL * q) / q; decHoldR = std::round (decHoldR * q) / q; }
            }
            outL[s] = outL[s] * (1.0f - mix) + decHoldL * mix;
            outR[s] = outR[s] * (1.0f - mix) + decHoldR * mix;
        }
    }
    viewDecHz.store (decHz);

    // --- efecto antes de la reverb
    DreamFx::Context ctx;
    ctx.base = base;
    for (int v = 0; v < NV; ++v) { ctx.freqs[(size_t) v] = target[(size_t) v]; ctx.active[(size_t) v] = eff[(size_t) v]; }
    ctx.fundEnv = voices[0].env;
    if (sendBuf.getNumSamples() < n) sendBuf.setSize (2, n, false, false, true);
    sendBuf.clear (0, n);
    float* sendL = sendBuf.getWritePointer (0);
    float* sendR = sendBuf.getWritePointer (1);
    fx.process (outL, outR, n, (int) p ("fxType"), p ("fxMix") / 100.0f, p ("fxP1") / 100.0f, p ("fxP2") / 100.0f, p ("fxP3") / 100.0f, ctx,
                p ("fxP4") / 100.0f, p ("fxP5") / 100.0f, sendL, sendR);

    // --- reverb
    DreamReverb::Settings rs;
    rs.mix = p ("revMix") / 100.0f;
    rs.timeSec = p ("revTime");
    rs.model = (int) p ("revModel");
    rs.size = p ("revSize") / 100.0f;
    rs.damp = p ("revDamp") / 100.0f;
    rs.modHz = p ("revModSpd");
    rs.modDepth = p ("revModDep") / 100.0f;
    rs.earlyGain = p ("revEarly") <= -40.0f ? 0.0f : Decibels::decibelsToGain (p ("revEarly"));
    rs.diffGain = p ("revDiff") <= -40.0f ? 0.0f : Decibels::decibelsToGain (p ("revDiff"));
    rs.shimmer = p ("revShimmer") / 100.0f;
    reverb.process (outL, outR, n, rs, sendL, sendR);

    // --- EQ de graves y agudos (estantes)
    {
        const float bdB = p ("eqBass"), tdB = p ("eqTreble");
        if (std::abs (bdB) > 0.01f || std::abs (tdB) > 0.01f)
        {
            for (int c = 0; c < 2; ++c)
            {
                eqLo[c].shelf (false, p ("eqBassFreq"), bdB, (float) sr);
                eqHi[c].shelf (true, p ("eqTrebleFreq"), tdB, (float) sr);
            }
            float* ch[2] = { outL, outR };
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < n; ++s) ch[c][s] = eqHi[c].process (eqLo[c].process (ch[c][s]));
        }
    }

    // --- compresor
    if (p ("compOn") > 0.5f)
    {
        comp.process (outL, outR, n, (float) sr, p ("compThresh"), p ("compRatio"), p ("compAttack"), p ("compRelease"),
                      p ("compHpf"), p ("compBlend") / 100.0f, p ("compMakeup"));
        viewGR.store (comp.grDb);
    }
    else { comp.grDb = 0.0f; viewGR.store (0.0f); }

    // seguridad: si algún módulo produce un valor no finito (NaN/Inf), se reinicia
    // en lugar de dejar el plugin mudo para siempre
    {
        bool bad = false;
        for (int s = 0; s < n && ! bad; ++s) bad = ! std::isfinite (outL[s]) || ! std::isfinite (outR[s]);
        if (bad)
        {
            reverb.reset(); fx.reset(); comp.reset();
            for (int c = 0; c < 2; ++c) { ladder[c].reset(); fsvf[c].reset(); eqLo[c].reset(); eqHi[c].reset(); foldDcX[c] = foldDcY[c] = 0.0f; }
            decHoldL = decHoldR = 0.0f;
            svfIc1[0] = svfIc1[1] = svfIc2[0] = svfIc2[1] = 0.0f; dcX = dcY = 0.0f;
            for (auto& v : voices) { v.phaseA = v.phaseB = v.phaseR = 0.0; v.rndCur = 0.0f; }
            FloatVectorOperations::clear (outL, n);
            FloatVectorOperations::clear (outR, n);
            ++safetyResets;
        }
    }

    const float mg = p ("master") <= -40.0f ? 0.0f : Decibels::decibelsToGain (p ("master"));
    for (int s = 0; s < n; ++s)
    {
        outL[s] = std::tanh (outL[s] * mg);
        outR[s] = std::tanh (outR[s] * mg);
    }

    viewBase.store ((float) base);
    viewEmphHarm.store ((float) emphK);
    viewArpVoice.store (arpIdx);
    for (int v = 0; v < NV; ++v)
    {
        view[(size_t) v].freq.store ((float) std::exp2 (voices[(size_t) v].curLog));
        view[(size_t) v].env.store (voices[(size_t) v].env * voices[(size_t) v].arpAmp);
        view[(size_t) v].gate.store (eff[(size_t) v]);
        view[(size_t) v].num.store (num[(size_t) v]);
        view[(size_t) v].den.store (den);
    }
}

// ==========================================================================
// Eleven Categories: combinaciones de voces permitidas
void DreamMachineProcessor::recomputeCategories()
{
    int den = 1;
    const auto num = currentNums (den);
    const int order = (int) p ("catOrder");
    const int X = (int) p ("catX");
    int64 h = order * 1000 + X;
    for (int v : num) h = h * 1315423911LL + v;
    if (h == catHash) return;
    catHash = h;
    uint32 mask = 0;
    for (uint32 m = 0; m < 32; ++m)
    {
        std::vector<int> gen;
        for (int v = 0; v < NV; ++v) if (m & (1u << v)) gen.push_back (num[(size_t) v]);
        if (combtones::allowed (gen, order, X)) mask |= 1u << m;
    }
    allowedMask.store (mask);
}

// ==========================================================================
// Transiciones (morph) de parámetros: hilo de mensajes
bool DreamMachineProcessor::isPerformanceParam (const String& id)
{
    static const StringArray perf { "midiMode", "bendRange", "rndAmount", "rndTime", "presetTime", "rndScope", "rndTrigger" };
    return perf.contains (id);
}

std::map<String, float> DreamMachineProcessor::defaultTargets() const
{
    std::map<String, float> t;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
            if (! isPerformanceParam (r->getParameterID()))
                t[r->getParameterID()] = r->convertFrom0to1 (r->getDefaultValue());
    return t;
}

void DreamMachineProcessor::startMorph (const std::map<String, float>& plainTargets, double seconds)
{
    morph.clear();
    for (auto& [id, val] : plainTargets)
    {
        auto* r = apvts.getParameter (id);
        if (r == nullptr) continue;
        const bool discrete = dynamic_cast<AudioParameterBool*> (r) != nullptr || dynamic_cast<AudioParameterChoice*> (r) != nullptr;
        const float to = r->convertTo0to1 (val);
        const float from = r->getValue();
        if (std::abs (to - from) < 1e-6f) continue;
        if (seconds <= 0.0)
        {
            r->beginChangeGesture();
            r->setValueNotifyingHost (to);
            r->endChangeGesture();
        }
        else
            morph.push_back ({ r, from, to, discrete, false });
    }
    morphActive = seconds > 0.0 && ! morph.empty();
    morphStart = Time::getMillisecondCounterHiRes() * 0.001;
    morphDur = seconds;
    morphProgressValue = morphActive ? 0.0f : 1.0f;
}

void DreamMachineProcessor::timerCallback()
{
    // disparo de random por automatización
    const bool trig = p ("rndTrigger") > 0.5f;
    if (trig && ! lastTrigger) randomize();
    lastTrigger = trig;

    if (morphActive)
    {
        const double t = jlimit (0.0, 1.0, (Time::getMillisecondCounterHiRes() * 0.001 - morphStart) / jmax (0.001, morphDur));
        const double e = t * t * (3.0 - 2.0 * t);    // suavizado
        for (auto& m : morph)
        {
            if (m.done) continue;
            float v;
            if (m.discrete) { if (t < 0.5) continue; v = m.to; m.done = true; }
            else v = (float) (m.from + (m.to - m.from) * e);
            if (t >= 1.0) { v = m.to; m.done = true; }
            if (std::abs (m.p->getValue() - v) > 1e-6f) m.p->setValueNotifyingHost (v);
        }
        morphProgressValue = (float) t;
        if (t >= 1.0) { morphActive = false; morph.clear(); }
    }

    recomputeCategories();
}

// --------------------------------------------------------------------------
void DreamMachineProcessor::randomize()
{
    const float amount = p ("rndAmount") / 100.0f;
    const int scopeSel = (int) p ("rndScope");
    static const StringArray never { "master", "midiMode", "bendRange", "rndAmount", "rndTime", "presetTime", "rndScope",
                                     "rndTrigger", "binOn", "binMode", "transpose", "temper", "prime1", "prime2", "prime3",
                                     // herramientas de mezcla: el random no las toca
                                     "eqBass", "eqBassFreq", "eqTreble", "eqTrebleFreq", "compOn", "compThresh", "compRatio",
                                     "compAttack", "compRelease", "compHpf", "compBlend", "compMakeup" };
    auto isTuning = [] (const String& id)
    {
        return id == "fund" || id == "octave" || id == "fine" || id == "den" || id.startsWith ("num")
            || id.startsWith ("prime") || id == "primeSnap";
    };
    auto& R = msgRandom;
    auto logU = [&R] (double lo, double hi) { return (float) (lo * std::pow (hi / lo, R.nextDouble())); };
    auto uniF = [&R] (double lo, double hi) { return (float) (lo + (hi - lo) * R.nextDouble()); };
    auto chance = [&R] (double p) { return R.nextDouble() < p; };
    auto pick = [&R] (std::initializer_list<int> l) { auto it = l.begin(); std::advance (it, R.nextInt ((int) l.size())); return (float) *it; };

    std::map<String, float> targets;
    float newDen = p ("den");
    std::array<int, 4> primes;
    for (int i = 0; i < 4; ++i) primes[(size_t) i] = (int) pv ("prime", i + 1);

    for (auto* prm : getParameters())
    {
        auto* r = dynamic_cast<RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        const String id = r->getParameterID();
        if (never.contains (id)) continue;
        const bool tun = isTuning (id), gate = id.startsWith ("gate");
        if (scopeSel == 1 && (tun || gate)) continue;
        if (scopeSel == 2 && ! tun) continue;
        if (scopeSel == 3 && ! gate) continue;

        const bool discrete = dynamic_cast<AudioParameterBool*> (r) || dynamic_cast<AudioParameterChoice*> (r);
        const float cur = r->convertFrom0to1 (r->getValue());
        float v;
        bool custom = true;
        if (id == "fund") v = logU (20, 120);
        else if (id == "octave") v = pick ({ 0, 1, 1, 2 });
        else if (id == "fine") v = uniF (-10, 10);
        else if (id == "den") { v = pick ({ 4, 8, 12, 16, 24, 32, 48, 64 }); newDen = v; }
        else if (id.startsWith ("num"))
        {
            const int d = (int) newDen;
            v = (float) tuning::snap (d + R.nextInt (3 * d + 1), 1, 1024, primes);
        }
        else if (id == "prime4") { v = pick ({ 1, 5, 11, 13, 17, 19, 23, 29, 31 }); primes[3] = (int) v; }
        else if (id == "gate0") v = chance (0.9) ? 1.0f : 0.0f;
        else if (gate) v = chance (0.7) ? 1.0f : 0.0f;
        else if (id.startsWith ("gain")) v = uniF (-12, 0);
        else if (id.startsWith ("wave")) v = chance (0.6) ? 0.0f : (float) (1 + R.nextInt (4));
        else if (id.startsWith ("fmSrc")) v = chance (0.7) ? 0.0f : (float) (1 + R.nextInt (3));
        else if (id.startsWith ("fmDepth")) v = uniF (0, 40);
        else if (id == "attack") v = logU (0.2, 20);
        else if (id == "release") v = logU (0.5, 30);
        else if (id == "slew" || id == "xfade") v = chance (0.6) ? 0.0f : uniF (0.1, 5);
        else if (id == "fmLfoRate") v = logU (0.02, 5);
        else if (id == "wlfoRate") v = logU (0.01, 1);
        else if (id == "wtable") v = (float) R.nextInt (audioUserTable || userTable ? 13 : 12);
        else if (id == "revMix") v = uniF (10, 60);
        else if (id == "revTime") v = logU (1, 20);
        else if (id == "revShimmer") v = uniF (20, 80);
        else if (id == "revEarly") v = uniF (-20, 0);
        else if (id == "revDiff") v = uniF (-12, 0);
        else if (id == "dreamOn") v = chance (0.35) ? 1.0f : 0.0f;
        else if (id == "dreamRate") v = logU (4, 120);
        else if (id == "dreamDensity") v = uniF (30, 90);
        else if (id == "dreamFund") v = chance (0.2) ? 1.0f : 0.0f;
        else if (id == "arpOn") v = chance (0.2) ? 1.0f : 0.0f;
        else if (id == "arpRate") v = logU (2, 20);
        else if (id == "diffAmt") v = chance (0.5) ? 0.0f : uniF (0, 50);
        else if (id == "vocAmt") v = chance (0.5) ? 0.0f : uniF (0, 70);
        else if (id == "vocHarm") v = (float) (1 + R.nextInt (16));
        else if (id == "fxType") v = chance (0.4) ? 0.0f : (float) (1 + R.nextInt (10));
        else if (id == "filtOn") v = chance (0.3) ? 1.0f : 0.0f;
        else if (id == "filtHarmOn") v = chance (0.6) ? 1.0f : 0.0f;
        else if (id == "filtHarm") v = (float) (2 + R.nextInt (24));
        else if (id == "filtFreq") v = logU (200, 8000);
        else if (id == "filtRes") v = uniF (0, 75);
        else if (id == "filtDrive") v = chance (0.5) ? 0.0f : uniF (0, 50);
        else if (id == "foldAmt") v = chance (0.7) ? 0.0f : uniF (5, 50);
        else if (id == "foldBias") v = uniF (0, 40);
        else if (id == "decOn") v = chance (0.12) ? 1.0f : 0.0f;
        else if (id == "decMode") v = chance (0.8) ? 1.0f : 0.0f;
        else if (id == "decHarm") v = (float) (16 + R.nextInt (1000));
        else if (id == "decBits") v = chance (0.6) ? 24.0f : uniF (6, 16);
        else if (id == "fxP4" || id == "fxP5") v = uniF (0, 60);
        else if (id == "fxMix") v = uniF (20, 70);
        else if (id == "catOn") v = chance (0.2) ? 1.0f : 0.0f;
        else if (id == "invAmp") v = chance (0.3) ? 1.0f : 0.0f;
        else if (id == "walkOn") v = chance (0.25) ? 1.0f : 0.0f;
        else if (id == "walkSpeed") v = logU (0.02, 0.6);
        else if (id == "primeSnap") v = chance (0.3) ? 1.0f : 0.0f;
        else { custom = false; v = r->convertFrom0to1 (R.nextFloat()); }
        ignoreUnused (custom);

        // "amount": interpola entre el valor actual y el aleatorio
        if (discrete) { if (! chance (amount)) continue; }
        else
        {
            const float nCur = r->convertTo0to1 (cur), nNew = r->convertTo0to1 (v);
            v = r->convertFrom0to1 (nCur + amount * (nNew - nCur));
        }
        targets[id] = v;
    }
    currentPresetName = "Random";
    startMorph (targets, p ("rndTime"));
}

// --------------------------------------------------------------------------
// Presets
File DreamMachineProcessor::userPresetDir()
{
    auto d = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("DreamMachine").getChildFile ("Presets");
    d.createDirectory();
    return d;
}

Array<File> DreamMachineProcessor::userPresets() const
{
    auto files = userPresetDir().findChildFiles (File::findFiles, false, "*.dmpreset");
    files.sort();
    return files;
}

void DreamMachineProcessor::loadFactory (int index)
{
    const auto& ps = factory::patches();
    if (index < 0 || index >= (int) ps.size()) return;
    auto t = defaultTargets();
    for (auto& [id, val] : ps[(size_t) index].v) t[String (id)] = val;
    currentProgram = index;
    currentPresetName = String::fromUTF8 (ps[(size_t) index].name.c_str());
    startMorph (t, p ("presetTime"));
}

void DreamMachineProcessor::setCurrentProgram (int index) { loadFactory (jlimit (0, getNumPrograms() - 1, index)); }

const String DreamMachineProcessor::getProgramName (int index)
{
    const auto& ps = factory::patches();
    return (index >= 0 && index < (int) ps.size()) ? String::fromUTF8 (ps[(size_t) index].name.c_str()) : String();
}

File DreamMachineProcessor::savePreset (const String& name)
{
    XmlElement xml ("DreamPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", "1.1");
    if (userWavPath.isNotEmpty()) xml.setAttribute ("userWav", userWavPath);
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if (isPerformanceParam (r->getParameterID())) continue;
            auto* e = xml.createNewChildElement ("P");
            e->setAttribute ("id", r->getParameterID());
            e->setAttribute ("v", (double) r->convertFrom0to1 (r->getValue()));
        }
    auto f = userPresetDir().getChildFile (File::createLegalFileName (name) + ".dmpreset");
    if (xml.writeTo (f)) { currentPresetName = name; return f; }
    return {};
}

bool DreamMachineProcessor::loadPresetFile (const File& f)
{
    auto xml = XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("DreamPreset")) return false;
    auto t = defaultTargets();
    for (auto* e : xml->getChildWithTagNameIterator ("P"))
    {
        const String id = e->getStringAttribute ("id");
        if (t.count (id)) t[id] = (float) e->getDoubleAttribute ("v");
    }
    const String wav = xml->getStringAttribute ("userWav");
    if (wav.isNotEmpty() && File (wav).existsAsFile() && wav != userWavPath)
    {
        const float wt = t["wtable"];
        loadUserWavetable (File (wav));
        t["wtable"] = wt;
    }
    currentPresetName = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
    startMorph (t, p ("presetTime"));
    return true;
}

bool DreamMachineProcessor::loadUserWavetable (const File& f)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) return false;
    const int len = (int) jmin<int64> (reader->lengthInSamples, (int64) 256 * WaveTable::N);
    if (len <= 0) return false;
    AudioBuffer<float> b ((int) reader->numChannels, len);
    reader->read (&b, 0, len, 0, true, true);
    auto t = wavetables::loadFromSamples (f.getFileNameWithoutExtension().toStdString(), b.getReadPointer (0), len);
    if (! t) return false;
    {
        const SpinLock::ScopedLockType sl (tableLock);
        if (userTable) retiredTables.push_back (userTable);
        userTable = t;
    }
    userWavPath = f.getFullPathName();
    if (auto* prm = apvts.getParameter ("wtable"))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) wavetables::builtInNames().size()));
    return true;
}

void DreamMachineProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("userWav", userWavPath, nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void DreamMachineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            morphActive = false; morph.clear();
            auto tree = ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            for (auto* prm : getParameters())
                if (auto* b = dynamic_cast<AudioProcessorParameter*> (dynamic_cast<AudioParameterBool*> (prm)))
                {
                    const float raw = b->getValue(), snapped = raw >= 0.5f ? 1.0f : 0.0f;
                    if (raw != snapped) b->setValueNotifyingHost (snapped);
                }
            currentProgram = (int) tree.getProperty ("program", 0);
            currentPresetName = tree.getProperty ("presetName", currentPresetName).toString();
            const String path = tree.getProperty ("userWav", "").toString();
            if (path.isNotEmpty() && File (path).existsAsFile())
            {
                const float keep = apvts.getRawParameterValue ("wtable")->load();
                loadUserWavetable (File (path));
                if (auto* prm = apvts.getParameter ("wtable"))
                    prm->setValueNotifyingHost (prm->convertTo0to1 (keep));
            }
        }
}

AudioProcessorEditor* DreamMachineProcessor::createEditor() { return new DreamMachineEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DreamMachineProcessor(); }
