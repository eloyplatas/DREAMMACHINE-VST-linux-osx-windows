// Prueba offline: renderiza audio y una captura de la GUI
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
using namespace juce;

static void setP (DreamMachineProcessor& p, const String& id, float v)
{ auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }

static void render (DreamMachineProcessor& proc, const File& out, double seconds, std::function<void(int)> perBlock = {})
{
    const double sr = 48000; const int bs = 512;
    proc.setRateAndBufferSizeDetails (sr, bs);
    proc.prepareToPlay (sr, bs);
    AudioBuffer<float> buf (2, bs);
    AudioBuffer<float> all (2, (int) (seconds * sr));
    MidiBuffer midi;
    int pos = 0, blk = 0; float peak = 0; double rms = 0; bool nan = false;
    while (pos + bs <= all.getNumSamples())
    {
        if (perBlock) perBlock (blk);
        buf.clear(); proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) { all.copyFrom (c, pos, buf, c, 0, bs);
            for (int i = 0; i < bs; ++i) { float x = buf.getSample (c, i); if (! std::isfinite (x)) nan = true; peak = jmax (peak, std::abs (x)); rms += x * x; } }
        pos += bs; ++blk;
    }
    std::cout << out.getFileName() << ": peak " << peak << " rms " << std::sqrt (rms / (2.0 * pos)) << (nan ? " NAN!" : "") << std::endl;
    out.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (new FileOutputStream (out), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (all, 0, pos);
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    File dir (argc > 1 ? argv[1] : ".");
    auto t0 = Time::getMillisecondCounterHiRes();
    DreamMachineProcessor proc;
    std::cout << "tablas en " << (Time::getMillisecondCounterHiRes() - t0) << " ms" << std::endl;

    // 1. Dream Chord por defecto, todas las puertas
    proc.setCurrentProgram (0);
    setP (proc, "attack", 0.5f); setP (proc, "revMix", 30);
    render (proc, dir.getChildFile ("01_dream_chord.wav"), 8.0);

    // 2. Permutación + dream + énfasis + tonos de diferencia + crossfade
    DreamMachineProcessor p2;
    p2.setCurrentProgram (5);
    setP (p2, "attack", 0.1f); setP (p2, "arpOn", 1); setP (p2, "arpOct", 2); setP (p2, "dreamOn", 1); setP (p2, "dreamRate", 1.0f);
    setP (p2, "vocAmt", 60); setP (p2, "vocDrift", 2); setP (p2, "diffAmt", 50); setP (p2, "xfade", 0.3f);
    setP (p2, "revModel", 3); setP (p2, "revTime", 12); setP (p2, "revMix", 50);
    setP (p2, "fmSrc1", 1); setP (p2, "fmDepth1", 50); setP (p2, "wtable", 3); setP (p2, "wlfoDepth", 80);
    render (p2, dir.getChildFile ("02_permutation_dream.wav"), 8.0, [&] (int b) { if (b == 300) setP (p2, "num2", 400); });

    // 3. Extremos: FM fundamental 1 kHz, temper, snap
    DreamMachineProcessor p3;
    for (int v = 0; v < 5; ++v) setP (p3, "gate" + String (v), 1);
    setP (p3, "fmRange", 3); for (int v = 1; v < 5; ++v) { setP (p3, "fmSrc" + String (v), 3); setP (p3, "fmDepth" + String (v), 100); }
    setP (p3, "primeSnap", 1); setP (p3, "temper", 50); setP (p3, "fund", 2000); setP (p3, "octave", 3); setP (p3, "master", 6);
    render (p3, dir.getChildFile ("03_extremos.wav"), 3.0);

    // 3b. Todos los efectos, shimmer, binaural, walk, categorías, 1/f
    for (int fxT = 1; fxT <= 10; ++fxT)
    {
        DreamMachineProcessor pf;
        pf.setCurrentProgram (0);
        setP (pf, "attack", 0.05f); setP (pf, "fxType", (float) fxT); setP (pf, "fxMix", 80);
        setP (pf, "fxP1", 70); setP (pf, "fxP2", 90); setP (pf, "fxP3", 60); setP (pf, "fxP4", 50); setP (pf, "fxP5", 60);
        setP (pf, "filtOn", 1); setP (pf, "filtType", (float) (fxT % 3)); setP (pf, "filtHarmOn", (float) (fxT % 2)); setP (pf, "filtRes", 95); setP (pf, "filtDrive", 80);
        setP (pf, "foldAmt", 60); setP (pf, "foldBias", 40); setP (pf, "decOn", 1); setP (pf, "decMode", (float) (fxT % 2)); setP (pf, "decBits", 6);
        setP (pf, "eqBass", 12); setP (pf, "eqTreble", -12); setP (pf, "compOn", 1); setP (pf, "compThresh", -40); setP (pf, "compRatio", 20); setP (pf, "compMakeup", 24);
        setP (pf, "wlfoShape", 1); setP (pf, "wlfoDepth", 80); setP (pf, "fmLfoShape", 5); setP (pf, "fmSrc2", 1); setP (pf, "fmDepth2", 80);
        setP (pf, "revModel", fxT % 3 == 0 ? 4.0f : (fxT % 3 == 1 ? 5.0f : 6.0f)); setP (pf, "revShimmer", 100); setP (pf, "revTime", 30); setP (pf, "revMix", 60);
        setP (pf, "binOn", 1); setP (pf, "binMode", (float) (fxT % 2)); setP (pf, "walkOn", 1); setP (pf, "walkSpeed", 2);
        setP (pf, "invAmp", 1); setP (pf, "dreamOn", 1); setP (pf, "dreamRate", 1); setP (pf, "catOn", 1); setP (pf, "catOrder", 3);
        render (pf, dir.getChildFile ("fx_" + String (fxT) + ".wav"), 6.0);
    }

    // 3c. Random + presets de usuario
    {
        DreamMachineProcessor pr;
        pr.randomize();
        render (pr, dir.getChildFile ("random.wav"), 3.0);
        auto f = pr.savePreset ("__test_preset__");
        DreamMachineProcessor pl;
        std::cout << "preset guardado: " << f.getFileName() << " carga " << (pl.loadPresetFile (f) ? "ok" : "MAL")
                  << " den " << pl.apvts.getRawParameterValue ("den")->load() << " = " << pr.apvts.getRawParameterValue ("den")->load() << std::endl;
        f.deleteFile();
        std::cout << "fabrica: " << pr.getNumPrograms() << " presets" << std::endl;
        for (int i = 0; i < pr.getNumPrograms(); ++i) { DreamMachineProcessor pp; pp.setCurrentProgram (i); render (pp, dir.getChildFile ("00_tmp.wav"), 0.5); }
    }

    // 4. Estado: guardar / restaurar
    MemoryBlock mb; proc.getStateInformation (mb);
    DreamMachineProcessor p4; p4.setStateInformation (mb.getData(), (int) mb.getSize());
    std::cout << "estado restaurado num1=" << p4.apvts.getRawParameterValue ("num1")->load() << std::endl;

    // GUI
    proc.setCurrentProgram (0);
    setP (proc, "fxType", 5); setP (proc, "catOn", 1); setP (proc, "dreamOn", 0); setP (proc, "revModel", 6);
    setP (proc, "fxP4", 40); setP (proc, "fxP5", 30); setP (proc, "filtOn", 1); setP (proc, "filtHarmOn", 1); setP (proc, "filtHarm", 12); setP (proc, "filtRes", 40);
    setP (proc, "foldAmt", 20); setP (proc, "compOn", 1); setP (proc, "compThresh", -30); setP (proc, "decOn", 1); setP (proc, "decMix", 30);
    render (proc, dir.getChildFile ("00_tmp.wav"), 1.0);
    std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (DreamMachineEditor::W, DreamMachineEditor::H);
    dynamic_cast<DreamMachineEditor*> (ed.get())->refreshNow();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    File png = dir.getChildFile ("gui.png"); png.deleteFile();
    FileOutputStream os (png); PNGImageFormat().writeImageToStream (img, os);
    {
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        File png2 = dir.getChildFile ("gui@2x.png"); png2.deleteFile();
        FileOutputStream os2 (png2); PNGImageFormat().writeImageToStream (img2, os2);
    }
    std::cout << "gui ok" << std::endl;
    ed.reset();
    return 0;
}
