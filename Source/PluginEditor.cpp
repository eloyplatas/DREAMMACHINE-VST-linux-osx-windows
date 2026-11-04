#include "PluginEditor.h"

using namespace juce;

// ===========================================================================
GBLookAndFeel::GBLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId, Colour::fromFloatRGBA (0.04f, 0.06f, 0.10f, 0.98f));
    setColour (PopupMenu::textColourId, gb::text);
    setColour (PopupMenu::headerTextColourId, gb::label);
    setColour (PopupMenu::highlightedBackgroundColourId, gb::btnHi);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (ComboBox::textColourId, gb::text);
    setColour (ComboBox::arrowColourId, gb::label);
    setColour (Label::textColourId, gb::text);
    setColour (TextButton::textColourOffId, gb::text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (TextEditor::backgroundColourId, Colour::fromFloatRGBA (0.03f, 0.05f, 0.09f, 1.0f));
    setColour (TextEditor::textColourId, Colours::white);
    setColour (TextEditor::highlightColourId, gb::btnHi);
    setColour (TextEditor::outlineColourId, gb::border);
    setColour (TextEditor::focusedOutlineColourId, gb::bar);
    setColour (CaretComponent::caretColourId, Colours::white);
    setColour (AlertWindow::backgroundColourId, Colour::fromFloatRGBA (0.06f, 0.08f, 0.13f, 1.0f));
    setColour (AlertWindow::textColourId, gb::text);
    setColour (AlertWindow::outlineColourId, gb::border);
    setColour (TooltipWindow::backgroundColourId, Colour::fromFloatRGBA (0.03f, 0.05f, 0.09f, 0.95f));
    setColour (TooltipWindow::textColourId, gb::text);
}

void GBLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool hover, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    if (b.getToggleState()) g.setColour (gb::btnOn);
    else g.setColour (hover || down ? gb::btnHi : gb::btn);
    g.fillRect (r);
    g.setColour (gb::border);
    g.drawRect (r, 1.0f);
}

void GBLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setFont (gb::font (11.0f));
    g.setColour (b.getToggleState() ? Colours::white : gb::text);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), Justification::centred, 1);
}

void GBLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (box.isMouseOver() ? gb::btnHi : gb::btn);
    g.fillRect (r);
    g.setColour (gb::border);
    g.drawRect (r, 1.0f);
    Path p;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (gb::label);
    g.fillPath (p);
}

void GBLookAndFeel::positionComboBoxText (ComboBox& box, Label& l)
{
    l.setBounds (4, 0, box.getWidth() - 18, box.getHeight());
    l.setFont (gb::font (11.0f));
}

// ===========================================================================
Fader::Fader (AudioProcessorValueTreeState& s, const String& paramId, const String& n)
    : id (paramId), name (n.toUpperCase())
{
    prm = s.getParameter (paramId);
    jassert (prm != nullptr);
    isInt = dynamic_cast<AudioParameterInt*> (prm) != nullptr;
    isHz = paramId == "fund";

    valueLabel.setFont (gb::font (9.0f, false));
    valueLabel.setJustificationType (Justification::centred);
    valueLabel.setColour (Label::textColourId, gb::title);
    valueLabel.setEditable (false, true, false);
    valueLabel.setTooltip ("Doble clic para escribir un valor");
    valueLabel.onTextChange = [this]
    {
        const String t = valueLabel.getText().trim().replaceCharacter (',', '.');
        const String digits = t.retainCharacters ("0123456789.-+eE");
        if (digits.isEmpty()) { refresh(); return; }
        const float v = (float) digits.getDoubleValue();
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (jlimit (prm->getNormalisableRange().start, prm->getNormalisableRange().end, v)));
        prm->endChangeGesture();
        refresh();
    };
    addAndMakeVisible (valueLabel);
    setTooltip (n + "  -  arrastra (Shift: fino, Ctrl: muy fino), rueda, doble clic: por defecto, clic derecho o doble clic en el numero: escribir");
    setRepaintsOnMouseActivity (true);
    refresh();
}

String Fader::format (float v) const
{
    if (isInt) return String ((int) std::lround (v));
    if (isHz) return String (v, 3);
    const float a = std::abs (v);
    if (a >= 100.0f) return String (v, 0);
    if (a >= 10.0f)  return String (v, 1);
    return String (v, 2);
}

void Fader::refresh()
{
    if (valueLabel.isBeingEdited()) return;
    const String t = format (prm->convertFrom0to1 (prm->getValue()));
    if (t != valueLabel.getText()) valueLabel.setText (t, dontSendNotification);
}

void Fader::resized()
{
    valueLabel.setBounds (0, getHeight() - 14, getWidth(), 13);
}

void Fader::setNorm (float n)
{
    n = jlimit (0.0f, 1.0f, n);
    if (std::abs (n - prm->getValue()) > 1e-7f) prm->setValueNotifyingHost (n);
    refresh();
    repaint();
}

void Fader::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { valueLabel.showEditor(); return; }
    dragging = true;
    dragNorm = prm->getValue();
    lastY = e.position.y;
    prm->beginChangeGesture();
}

void Fader::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    const float dy = lastY - e.position.y;
    lastY = e.position.y;
    // 400 píxeles = recorrido completo; Shift /10; Ctrl /100
    float sens = 1.0f / 400.0f;
    if (e.mods.isShiftDown()) sens *= 0.1f;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isAltDown()) sens *= 0.01f;
    if (isInt)
    {
        // en enteros: como mínimo un paso cada 3 píxeles en modo fino
        const int steps = jmax (2, prm->getNumSteps());
        const float stepN = 1.0f / (float) (steps - 1);
        sens = jmax (sens, (e.mods.isShiftDown() || e.mods.isCtrlDown()) ? stepN / 3.0f : 0.0f);
    }
    dragNorm = jlimit (0.0f, 1.0f, dragNorm + dy * sens);
    setNorm (dragNorm);
}

void Fader::mouseUp (const MouseEvent&)
{
    if (dragging) prm->endChangeGesture();
    dragging = false;
}

void Fader::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    prm->beginChangeGesture();
    setNorm (prm->getDefaultValue());
    prm->endChangeGesture();
}

void Fader::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const float dir = (w.deltaY != 0.0f ? w.deltaY : -w.deltaX) > 0 ? 1.0f : -1.0f;
    float step;
    if (isInt) step = 1.0f / (float) jmax (1, prm->getNumSteps() - 1);
    else step = e.mods.isShiftDown() ? 0.001f : 0.01f;
    prm->beginChangeGesture();
    setNorm (prm->getValue() + dir * step);
    prm->endChangeGesture();
}

void Fader::paint (Graphics& g)
{
    const auto bar = barArea();
    // etiqueta girada
    g.saveState();
    g.setColour (gb::label);
    g.setFont (gb::font (9.5f));
    g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, 0.0f, 0.0f).translated (0.0f, bar.getBottom()));
    g.drawText (name, Rectangle<float> (0.0f, 0.0f, bar.getHeight(), 12.0f), Justification::centredLeft, true);
    g.restoreState();

    g.setColour (gb::track);
    g.fillRect (bar);
    const auto range = prm->getNormalisableRange();
    const float norm = prm->getValue();
    const float y = bar.getBottom() - norm * bar.getHeight();
    g.setColour (gb::bar);
    if (range.start < 0.0f && range.end > 0.0f)
    {
        const float zy = bar.getBottom() - prm->convertTo0to1 (0.0f) * bar.getHeight();
        g.fillRect (Rectangle<float>::leftTopRightBottom (bar.getX(), jmin (zy, y), bar.getRight(), jmax (zy, y) + 1.0f));
        g.setColour (gb::label.withAlpha (0.6f));
        g.fillRect (bar.getX(), zy, bar.getWidth(), 1.0f);
    }
    else
        g.fillRect (Rectangle<float>::leftTopRightBottom (bar.getX(), y, bar.getRight(), bar.getBottom()));
    g.setColour (gb::barHi);
    g.fillRect (bar.getX(), jlimit (bar.getY(), bar.getBottom() - 1.0f, y), bar.getWidth(), 1.5f);
    if (isMouseOverOrDragging())
    {
        g.setColour (Colours::white.withAlpha (0.25f));
        g.drawRect (bar, 1.0f);
    }
}

Toggle::Toggle (AudioProcessorValueTreeState& s, const String& paramId, const String& t) : TextButton (t)
{
    setClickingTogglesState (true);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, paramId, *this);
    if (auto* prm = s.getParameter (paramId)) setTooltip (prm->getName (64));
}

Choice::Choice (AudioProcessorValueTreeState& s, const String& paramId)
{
    if (auto* c = dynamic_cast<AudioParameterChoice*> (s.getParameter (paramId)))
    {
        addItemList (c->choices, 1);
        setTooltip (c->getName (64));
    }
    att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, paramId, *this);
}

void Panel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (gb::panel);
    g.fillRoundedRectangle (r, 5.0f);
    const float ty = r.getHeight() < 40.0f ? (r.getHeight() - 18.0f) * 0.5f : 4.0f;
    g.setColour (gb::title);
    g.setFont (gb::font (13.0f));
    g.drawText (titleText, Rectangle<float> (12.0f, ty, r.getWidth() - 24.0f, 18.0f), Justification::centredLeft, true);
    if (rightText.isNotEmpty())
    {
        g.setColour (rightColour);
        g.setFont (gb::font (12.0f));
        g.drawText (rightText, Rectangle<float> (12.0f, ty, r.getWidth() - 22.0f, 18.0f), Justification::centredRight, true);
    }
}

void ScopeView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour::fromFloatRGBA (0.0f, 0.01f, 0.03f, 0.7f));
    g.fillRect (r);
    g.setColour (gb::track.withAlpha (0.12f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
    g.drawVerticalLine ((int) r.getCentreX(), r.getY(), r.getBottom());

    const auto& bx = proc.scope[(size_t) vx];
    const auto& by = proc.scope[(size_t) vy];
    const int N = DreamMachineProcessor::SCOPE_N;
    const int w = proc.scopeWrite.load();
    const int count = N - 512, stride = 4;
    float mx = 1e-4f, my = 1e-4f;
    for (int i = 0; i < count; i += stride)
    {
        const int k = (w - count + i + N) % N;
        mx = jmax (mx, std::abs (bx[(size_t) k])); my = jmax (my, std::abs (by[(size_t) k]));
    }
    Path p;
    const float cx = r.getCentreX(), cy = r.getCentreY(), sx = r.getWidth() * 0.46f / mx, sy = r.getHeight() * 0.46f / my;
    for (int i = 0; i < count; i += stride)
    {
        const int k = (w - count + i + N) % N;
        const float x = cx + bx[(size_t) k] * sx, y = cy - by[(size_t) k] * sy;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (gb::trace.withAlpha (0.8f));
    g.strokePath (p, PathStrokeType (0.8f));

    // razón entre las dos voces
    const int n1 = proc.view[(size_t) vx].num.load(), n2 = proc.view[(size_t) vy].num.load();
    const int gg = std::gcd (n1, n2);
    g.setFont (gb::font (11.0f));
    g.setColour (gb::title);
    if (gg > 0)
        g.drawText (String (n2 / gg) + ":" + String (n1 / gg), r.reduced (4).toNearestInt(), Justification::bottomRight);
}

// ===========================================================================
Panel* DreamMachineEditor::addPanel (const String& title, Rectangle<int> r)
{
    panels.push_back (std::make_unique<Panel> (title));
    auto* p = panels.back().get();
    content.addAndMakeVisible (*p);
    p->setBounds (r);
    p->toBack();
    return p;
}

DreamMachineEditor::DreamMachineEditor (DreamMachineProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setSize (W, H);
    buildWood();
    layoutContent();
    rebuildPresetList();

    setResizable (true, true);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / (double) H);
    setResizeLimits (W / 2, H / 2, W * 2, H * 2);
    setSize ((int) (W * 0.78), (int) (H * 0.78));
    startTimerHz (25);
}

DreamMachineEditor::~DreamMachineEditor()
{
    stopTimer();
    content.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void DreamMachineEditor::layoutContent()
{
    auto& s = proc.apvts;
    auto at = [] (Panel* pn, int x, int y, int w, int h) { return Rectangle<int> (pn->getX() + x, pn->getY() + y, w, h); };
    auto addFaders = [&] (Panel* pn, int x, int y, int h, int step, std::initializer_list<std::pair<String, String>> list)
    {
        int xx = pn->getX() + x;
        for (auto& [pid, nm] : list)
        {
            auto* f = make<Fader> (s, pid, nm);
            f->setBounds (xx, pn->getY() + y, jmin (step, 44), h);
            faders.push_back (f);
            xx += step;
        }
    };
    const int row1 = 26;

    // ---------------- barra superior: presets
    topPanel = addPanel ("", { 8, 8, W - 16, 34 });
    topPanel->rightText = "DREAM MACHINE  -  after La Monte Young & Marian Zazeela  -  version 1.3";
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &loadBtn, &delBtn }) content.addAndMakeVisible (*b);
    content.addAndMakeVisible (presetBox);
    prevBtn.setBounds (at (topPanel, 10, 7, 24, 20));
    presetBox.setBounds (at (topPanel, 38, 7, 330, 20));
    nextBtn.setBounds (at (topPanel, 372, 7, 24, 20));
    saveBtn.setBounds (at (topPanel, 404, 7, 56, 20));
    loadBtn.setBounds (at (topPanel, 464, 7, 56, 20));
    delBtn.setBounds (at (topPanel, 524, 7, 64, 20));
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    saveBtn.onClick = [this] { savePresetDialog(); };
    delBtn.onClick = [this] { deletePresetDialog(); };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Cargar preset", DreamMachineProcessor::userPresetDir(), "*.dmpreset");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile() && ! proc.loadPresetFile (f))
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Dream Machine", "No es un preset valido: " + f.getFileName());
        });
    };
    presetBox.onChange = [this] { selectPresetId (presetBox.getSelectedId()); };
    presetBox.setTooltip ("Presets de fabrica y de usuario (el cambio usa PRESET TIME)");

    // ---------------- fila A: voces
    const int yA = 50, hA = 180, fadYA = 52, fadHA = hA - fadYA - 6;
    {
        auto* pn = addPanel ("FUNDAMENTAL", { 8, yA, 286, hA });
        voicePanels[0] = pn;
        make<Toggle> (s, "gate0", "GATE")->setBounds (at (pn, 12, row1, 50, 20));
        make<Choice> (s, "wave0")->setBounds (at (pn, 68, row1, 100, 20));
        make<Choice> (s, "fmSrc0")->setBounds (at (pn, 174, row1, 100, 20));
        addFaders (pn, 10, fadYA, fadHA, 38, { { "fund", "Hz" }, { "octave", "Octave" }, { "transpose", "Transp" }, { "fine", "Fine" },
                                               { "gain0", "Gain" }, { "pan0", "Pan" }, { "fmDepth0", "FM" } });
    }
    const int tw = 226;
    for (int v = 1; v <= 4; ++v)
    {
        auto* pn = addPanel ("TONE " + String (v), { 302 + (v - 1) * (tw + 8), yA, tw, hA });
        voicePanels[(size_t) v] = pn;
        make<Toggle> (s, params::vid ("gate", v), "GATE")->setBounds (at (pn, 12, row1, 46, 20));
        make<Choice> (s, params::vid ("wave", v))->setBounds (at (pn, 62, row1, 74, 20));
        make<Choice> (s, params::vid ("fmSrc", v))->setBounds (at (pn, 140, row1, 74, 20));
        addFaders (pn, 14, fadYA, fadHA, 50, { { params::vid ("num", v), "Numerator" }, { params::vid ("gain", v), "Gain" },
                                               { params::vid ("pan", v), "Pan" }, { params::vid ("fmDepth", v), "FM" } });
    }

    // ---------------- fila B
    const int yB = 238, hB = 160, fadY = 52, fadH = hB - fadY - 6, fadYT = 28, fadHT = hB - fadYT - 6;
    {
        auto* pn = addPanel ("TUNING", { 8, yB, 380, hB });
        refBox.addItemList ({ "Si b 58.27 Hz", "Si b 116.54 Hz", "Red 50 Hz", "Red 60 Hz",
                              "80 Hz (4/3 de 60)", "100 Hz", "120 Hz (acuario)", "30 Hz" }, 1);
        refBox.setTextWhenNothingSelected ("REF Hz");
        refBox.onChange = [this]
        {
            static const double hz[] = { 29.135, 29.135, 50, 60, 80, 100, 120, 30 };
            static const int oct[] = { 1, 2, 0, 0, 0, 0, 0, 0 };
            const int i = refBox.getSelectedId() - 1;
            if (i < 0) return;
            for (auto [pid, v] : { std::pair<const char*, float> { "fund", (float) hz[i] }, { "octave", (float) oct[i] } })
                if (auto* prm = proc.apvts.getParameter (pid))
                {
                    prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture();
                }
        };
        content.addAndMakeVisible (refBox);
        refBox.setBounds (at (pn, 12, row1, 150, 20));
        make<Toggle> (s, "primeSnap", "PRIME SNAP")->setBounds (at (pn, 168, row1, 96, 20));
        make<Toggle> (s, "invAmp", "1/f AMP")->setBounds (at (pn, 270, row1, 96, 20));
        addFaders (pn, 10, fadY, fadH, 42, { { "den", "Denom" }, { "prime1", "Prime 1" }, { "prime2", "Prime 2" },
                                             { "prime3", "Prime 3" }, { "prime4", "Prime 4" }, { "temper", "JI>12TET" } });
    }
    {
        auto* pn = addPanel ("WAVETABLE", { 396, yB, 270, hB });
        make<Choice> (s, "wtable")->setBounds (at (pn, 12, row1, 150, 20));
        content.addAndMakeVisible (loadWavBtn);
        loadWavBtn.setBounds (at (pn, 168, row1, 90, 20));
        loadWavBtn.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("Wavetable (WAV/AIFF/FLAC, frames de 2048)", File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.existsAsFile() && ! proc.loadUserWavetable (f))
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Dream Machine", "No se pudo cargar " + f.getFileName());
            });
        };
        addFaders (pn, 10, fadY, fadH, 38, { { "wpos", "Position" }, { "wspread", "Spread" }, { "wlfoRate", "LFO rate" }, { "wlfoDepth", "LFO depth" } });
        userWavLabel.setFont (gb::font (9.5f, false));
        userWavLabel.setColour (Label::textColourId, gb::label);
        userWavLabel.setJustificationType (Justification::topLeft);
        content.addAndMakeVisible (userWavLabel);
        userWavLabel.setBounds (at (pn, 168, fadY, 96, 60));
        make<Choice> (s, "wlfoShape")->setBounds (at (pn, 168, fadY + 66, 90, 20));
    }
    {
        auto* pn = addPanel ("ENVELOPE", { 674, yB, 176, hB });
        addFaders (pn, 10, fadYT, fadHT, 38, { { "attack", "Attack" }, { "release", "Release" }, { "slew", "Slew" }, { "xfade", "Crossfade" } });
    }
    {
        auto* pn = addPanel ("FM", { 858, yB, 160, hB });
        make<Choice> (s, "fmRange")->setBounds (at (pn, 12, row1, 64, 20));
        make<Choice> (s, "fmLfoShape")->setBounds (at (pn, 80, row1, 70, 20));
        addFaders (pn, 10, fadY, fadH, 38, { { "fmLfoRate", "LFO rate" } });
    }
    {
        auto* pn = addPanel ("MASTER / MIDI", { 1026, yB, 206, hB });
        make<Choice> (s, "midiMode")->setBounds (at (pn, 12, row1, 182, 20));
        addFaders (pn, 10, fadY, fadH, 42, { { "master", "Master" }, { "bendRange", "Bend" } });
    }

    // ---------------- fila C
    const int yC = 406, hC = 160;
    {
        auto* pn = addPanel ("REVERB", { 8, yC, 410, hC });
        make<Choice> (s, "revModel")->setBounds (at (pn, 12, row1, 130, 20));
        addFaders (pn, 10, fadY, fadH, 42, { { "revMix", "Mix" }, { "revTime", "Time" }, { "revSize", "Size" }, { "revDamp", "High damp" },
                                             { "revModSpd", "Mod speed" }, { "revModDep", "Mod depth" }, { "revEarly", "Early" },
                                             { "revDiff", "Diffuse" }, { "revShimmer", "Shimmer" } });
    }
    {
        auto* pn = addPanel ("FX (PRE REVERB)", { 426, yC, 300, hC });
        make<Choice> (s, "fxType")->setBounds (at (pn, 12, row1, 150, 20));
        addFaders (pn, 10, fadY, fadH, 46, { { "fxMix", "Mix" } });
        const char* ids[] = { "fxP1", "fxP2", "fxP3", "fxP4", "fxP5" };
        for (int i = 0; i < 5; ++i)
        {
            auto* f = make<Fader> (s, ids[i], "-");
            f->setBounds (pn->getX() + 10 + 46 * (i + 1), pn->getY() + fadY, 44, fadH);
            faders.push_back (f);
            fxFaders[(size_t) i] = f;
        }
    }
    {
        auto* pn = addPanel ("DIFF TONES", { 734, yC, 120, hC });
        addFaders (pn, 10, fadYT, fadHT, 42, { { "diffAmt", "Amount" }, { "diffDrive", "Drive" } });
    }
    {
        auto* pn = addPanel ("EMPHASIS", { 862, yC, 190, hC });
        addFaders (pn, 10, fadYT, fadHT, 42, { { "vocAmt", "Amount" }, { "vocHarm", "Harmonic" }, { "vocQ", "Q" }, { "vocDrift", "Drift" } });
    }
    {
        walkPanel = addPanel ("WALK", { 1060, yC, 172, hC });
        make<Toggle> (s, "walkOn", "ON")->setBounds (at (walkPanel, 12, row1, 44, 20));
        addFaders (walkPanel, 10, fadY, fadH, 46, { { "walkSpeed", "Speed" }, { "walkPos", "Position" }, { "walkDepth", "Depth" } });
    }

    // ---------------- fila D
    const int yD = 574, hD = 160;
    {
        auto* pn = addPanel ("DREAM", { 8, yD, 330, hD });
        make<Toggle> (s, "dreamOn", "ON")->setBounds (at (pn, 12, row1, 40, 20));
        make<Toggle> (s, "dreamFund", "+FUND")->setBounds (at (pn, 56, row1, 54, 20));
        make<Toggle> (s, "catOn", "11 CATEGORIES")->setBounds (at (pn, 114, row1, 104, 20));
        addFaders (pn, 10, fadY, fadH, 46, { { "dreamRate", "Change time" }, { "dreamDensity", "Density" },
                                             { "catOrder", "Comb order" }, { "catX", "Exclude X" } });
    }
    {
        auto* pn = addPanel ("PERMUTATION", { 346, yD, 260, hD });
        make<Toggle> (s, "arpOn", "ON")->setBounds (at (pn, 12, row1, 40, 20));
        make<Toggle> (s, "arpFund", "+FUND")->setBounds (at (pn, 56, row1, 54, 20));
        make<Choice> (s, "arpMode")->setBounds (at (pn, 114, row1, 134, 20));
        addFaders (pn, 10, fadY, fadH, 42, { { "arpRate", "Rate" }, { "arpSmooth", "Smooth" }, { "arpOct", "Octaves" } });
    }
    {
        auto* pn = addPanel ("BINAURAL", { 614, yD, 230, hD });
        make<Toggle> (s, "binOn", "ON")->setBounds (at (pn, 12, row1, 40, 20));
        make<Choice> (s, "binMode")->setBounds (at (pn, 56, row1, 120, 20));
        addFaders (pn, 10, fadY, fadH, 42, { { "binHz", "Beat Hz" } });
        auto* l = make<Label>();
        l->setFont (gb::font (9.5f, false));
        l->setColour (Label::textColourId, gb::label);
        l->setJustificationType (Justification::topLeft);
        l->setText ("Usa auriculares.\nCada oido recibe la voz\n+/- medio batido.\nProportional mantiene las\nrazones justas en cada oido.", dontSendNotification);
        l->setBounds (at (pn, 60, fadY, 164, 80));
    }
    {
        randomPanel = addPanel ("RANDOM / MORPH", { 852, yD, 380, hD });
        content.addAndMakeVisible (randomBtn);
        randomBtn.setBounds (at (randomPanel, 12, row1, 90, 20));
        randomBtn.setTooltip ("Valores aleatorios (con transicion de RND TIME)");
        randomBtn.onClick = [this] { proc.randomize(); };
        make<Choice> (s, "rndScope")->setBounds (at (randomPanel, 108, row1, 100, 20));
        addFaders (randomPanel, 10, fadY, fadH, 56, { { "rndAmount", "Amount" }, { "rndTime", "Rnd time" }, { "presetTime", "Preset time" } });
        auto* l = make<Label>();
        l->setFont (gb::font (9.5f, false));
        l->setColour (Label::textColourId, gb::label);
        l->setJustificationType (Justification::topLeft);
        l->setText ("Tiempos de 0 (inmediato)\na 300 s (5 min).\nRND TIME: boton RANDOM.\nPRESET TIME: cambio de\npreset (fabrica o usuario).", dontSendNotification);
        l->setBounds (at (randomPanel, 184, fadY, 190, 80));
    }

    // ---------------- fila F (1.2): filtro, wavefold, decimador, EQ, compresor
    const int yF = 742, hF = 160;
    {
        filterPanel = addPanel ("FILTER", { 8, yF, 290, hF });
        make<Toggle> (s, "filtOn", "ON")->setBounds (at (filterPanel, 12, row1, 40, 20));
        make<Choice> (s, "filtType")->setBounds (at (filterPanel, 56, row1, 120, 20));
        make<Toggle> (s, "filtHarmOn", "HARMONIC")->setBounds (at (filterPanel, 180, row1, 98, 20));
        addFaders (filterPanel, 10, fadY, fadH, 46, { { "filtFreq", "Cutoff Hz" }, { "filtHarm", "Harmonic" }, { "filtRes", "Resonance" },
                                                      { "filtMorph", "Morph" }, { "filtDrive", "Drive" } });
    }
    {
        auto* pn = addPanel ("WAVEFOLD", { 306, yF, 170, hF });
        addFaders (pn, 10, fadYT, fadHT, 46, { { "foldAmt", "Amount" }, { "foldBias", "Bias" }, { "foldMix", "Mix" } });
    }
    {
        decPanel = addPanel ("DECIMATOR", { 484, yF, 250, hF });
        make<Toggle> (s, "decOn", "ON")->setBounds (at (decPanel, 12, row1, 40, 20));
        make<Choice> (s, "decMode")->setBounds (at (decPanel, 56, row1, 100, 20));
        addFaders (decPanel, 10, fadY, fadH, 46, { { "decHz", "Free Hz" }, { "decHarm", "Harmonic" }, { "decBits", "Bits" }, { "decMix", "Mix" } });
    }
    {
        auto* pn = addPanel ("EQ", { 742, yF, 196, hF });
        addFaders (pn, 10, fadYT, fadHT, 44, { { "eqBass", "Bass dB" }, { "eqBassFreq", "Bass Hz" }, { "eqTreble", "Treble dB" }, { "eqTrebleFreq", "Treble Hz" } });
    }
    {
        compPanel = addPanel ("COMPRESSOR", { 946, yF, 286, hF });
        make<Toggle> (s, "compOn", "ON")->setBounds (at (compPanel, 12, row1, 40, 20));
        addFaders (compPanel, 10, fadY, fadH, 38, { { "compThresh", "Thresh" }, { "compRatio", "Ratio" }, { "compAttack", "Attack" },
                                                    { "compRelease", "Release" }, { "compHpf", "SC HPF" }, { "compBlend", "Blend" }, { "compMakeup", "Makeup" } });
    }

    // ---------------- fila E: razones y osciloscopio
    const int yE = 910;
    ratioPanel = addPanel ("RATIOS", { 8, yE, 1000, 200 });
    {
        auto* pn = addPanel ("LISSAJOUS", { 1016, yE, 216, 200 });
        for (auto* b : { &scopeXBox, &scopeYBox })
        {
            for (int v = 0; v < 5; ++v) b->addItem (params::voiceTag (v), v + 1);
            content.addAndMakeVisible (*b);
        }
        scopeXBox.setSelectedId (1, dontSendNotification);
        scopeYBox.setSelectedId (2, dontSendNotification);
        scopeXBox.setBounds (at (pn, 12, row1, 92, 20));
        scopeYBox.setBounds (at (pn, 112, row1, 92, 20));
        scopeXBox.setTooltip ("Voz en el eje X");
        scopeYBox.setTooltip ("Voz en el eje Y");
        scopeView = make<ScopeView> (proc);
        scopeView->setBounds (at (pn, 12, 52, 192, 142));
        scopeXBox.onChange = [this] { scopeView->vx = scopeXBox.getSelectedId() - 1; };
        scopeYBox.onChange = [this] { scopeView->vy = scopeYBox.getSelectedId() - 1; };
    }
}

// ===========================================================================
// Presets
void DreamMachineEditor::rebuildPresetList()
{
    presetBox.clear (dontSendNotification);
    presetOrder.clear();
    presetBox.addSectionHeading ("FABRICA");
    for (int i = 0; i < proc.getNumPrograms(); ++i) { presetBox.addItem (proc.getProgramName (i), i + 1); presetOrder.push_back (i + 1); }
    userFiles = proc.userPresets();
    if (! userFiles.isEmpty())
    {
        presetBox.addSectionHeading ("USUARIO");
        for (int j = 0; j < userFiles.size(); ++j) { presetBox.addItem (userFiles[j].getFileNameWithoutExtension(), 1000 + j); presetOrder.push_back (1000 + j); }
    }
    presetBox.setText (proc.currentPresetName, dontSendNotification);
}

void DreamMachineEditor::selectPresetId (int id)
{
    if (id <= 0) return;
    if (id < 1000) proc.loadFactory (id - 1);
    else if (id - 1000 < userFiles.size()) proc.loadPresetFile (userFiles[id - 1000]);
}

void DreamMachineEditor::stepPreset (int delta)
{
    if (presetOrder.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < presetOrder.size(); ++i)
        if (presetBox.getItemText ((int) i) == proc.currentPresetName) { idx = (int) i; break; }
    // los encabezados de sección no cuentan como elementos, así que buscamos por nombre
    if (idx < 0)
        for (size_t i = 0; i < presetOrder.size(); ++i)
        {
            const int id = presetOrder[i];
            const String nm = id < 1000 ? proc.getProgramName (id - 1) : userFiles[id - 1000].getFileNameWithoutExtension();
            if (nm == proc.currentPresetName) { idx = (int) i; break; }
        }
    idx = (idx + delta + (int) presetOrder.size()) % (int) presetOrder.size();
    presetBox.setSelectedId (presetOrder[(size_t) idx], sendNotificationSync);
}

void DreamMachineEditor::savePresetDialog()
{
    auto* aw = new AlertWindow ("Guardar preset", "Nombre del preset:", MessageBoxIconType::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("name", proc.currentPresetName == "Random" ? String ("Mi drone") : proc.currentPresetName);
    aw->addButton ("GUARDAR", 1, KeyPress (KeyPress::returnKey));
    aw->addButton ("CANCELAR", 0, KeyPress (KeyPress::escapeKey));
    aw->enterModalState (true, ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            const String nm = aw->getTextEditorContents ("name").trim();
            if (nm.isNotEmpty())
            {
                auto f = proc.savePreset (nm);
                if (f == File())
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Dream Machine", "No se pudo guardar el preset.");
                rebuildPresetList();
            }
        }
    }), true);
}

void DreamMachineEditor::deletePresetDialog()
{
    File target;
    for (auto& f : userFiles) if (f.getFileNameWithoutExtension() == proc.currentPresetName) target = f;
    if (target == File())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Dream Machine", "Solo se pueden borrar presets de usuario.\nSelecciona uno primero.");
        return;
    }
    AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Borrar preset",
                                  "Mover a la papelera \"" + target.getFileNameWithoutExtension() + "\"?", "BORRAR", "CANCELAR", this,
                                  ModalCallbackFunction::create ([this, target] (int r)
                                  {
                                      if (r == 1) { if (! target.moveToTrash()) target.deleteFile(); rebuildPresetList(); }
                                  }));
}

// ===========================================================================
void DreamMachineEditor::buildWood()
{
    wood = Image (Image::RGB, W, H, true);
    Image::BitmapData bd (wood, Image::BitmapData::writeOnly);
    Random rnd (23);
    const int plank = 92;
    std::vector<float> offs (64);
    for (auto& o : offs) o = rnd.nextFloat() * 1000.0f;
    for (int y = 0; y < H; ++y)
    {
        const int pi = y / plank;
        const float po = offs[(size_t) (pi % 64)];
        for (int x = 0; x < W; ++x)
        {
            const float fx = (float) x + po;
            const float grain = std::sin ((float) y * 0.55f + 3.0f * std::sin (fx * 0.006f) + 1.5f * std::sin (fx * 0.021f + po));
            const float fine = std::sin ((float) y * 2.3f + fx * 0.002f + po) * 0.5f;
            float v = 0.15f + 0.018f * grain + 0.008f * fine + 0.010f * (rnd.nextFloat() - 0.5f);
            const float kx = std::fmod (po * 7.0f, (float) W), ky = (float) (pi * plank + plank / 2);
            const float dk = std::hypot (((float) x - kx) * 0.6f, (float) y - ky);
            if (dk < 18.0f) v -= 0.06f * (1.0f - dk / 18.0f);
            if (y % plank < 2) v = 0.06f;
            else if (y % plank < 4) v *= 0.8f;
            v = jlimit (0.0f, 1.0f, v);
            // madera teñida de azul noche
            bd.setPixelColour (x, y, Colour::fromFloatRGBA (v * 0.62f, v * 0.78f, jmin (1.0f, v * 1.25f), 1.0f));
        }
    }
}

void DreamMachineEditor::paint (Graphics& g) { g.fillAll (Colour::fromFloatRGBA (0.03f, 0.04f, 0.07f, 1.0f)); }

void DreamMachineEditor::resized()
{
    const float sc = jmin ((float) getWidth() / (float) W, (float) getHeight() / (float) H);
    const float ox = ((float) getWidth() - W * sc) * 0.5f, oy = ((float) getHeight() - H * sc) * 0.5f;
    content.setTransform (AffineTransform::scale (sc).translated (ox, oy));
}

void DreamMachineEditor::Content::paint (Graphics& g) { g.drawImageAt (ed.wood, 0, 0); }

void DreamMachineEditor::Content::paintOverChildren (Graphics& g)
{
    if (ed.ratioPanel) ed.paintRatioDisplay (g, ed.ratioPanel->getBounds());
}

// ===========================================================================
static String ratioText (int n, int d)
{
    const int g = std::gcd (n, d);
    return String (n / g) + "/" + String (d / g);
}

void DreamMachineEditor::timerCallback()
{
    std::array<int, 4> primes;
    for (int i = 0; i < 4; ++i) primes[(size_t) i] = (int) proc.apvts.getRawParameterValue (params::vid ("prime", i + 1))->load();
    for (int v = 1; v < 5; ++v)
    {
        const int n = proc.view[(size_t) v].num.load(), d = jmax (1, proc.view[(size_t) v].den.load());
        auto* pn = voicePanels[(size_t) v];
        const double c = tuning::cents ((double) n / d);
        pn->rightText = ratioText (n, d) + "   " + String (c >= 0 ? "+" : "") + String (c, 1) + "c";
        const int g = std::gcd (n, d);
        const bool five = tuning::hasFactor (n / g, 5) || tuning::hasFactor (d / g, 5);
        const bool outside = ! tuning::isSmooth (n, primes) || ! tuning::isSmooth (d, primes);
        pn->rightColour = five ? gb::warn : (outside ? gb::alert : gb::title);
    }
    voicePanels[0]->rightText = String (proc.viewBase.load(), 2) + " Hz";
    voicePanels[0]->rightColour = gb::title;
    walkPanel->rightText = "x = " + String (proc.viewWalkX.load(), 2) + " m";
    randomPanel->rightText = proc.isMorphing() ? "MORPH " + String (roundToInt (proc.morphProgress() * 100.0f)) + "%" : String();
    randomPanel->rightColour = gb::alert;
    userWavLabel.setText ("USER WAV:\n" + proc.getUserWavetableName(), dontSendNotification);

    const auto names = params::fxParamNames ((int) proc.apvts.getRawParameterValue ("fxType")->load());
    for (int i = 0; i < 5; ++i)
    {
        fxFaders[(size_t) i]->setDisplayName (names[i]);
        fxFaders[(size_t) i]->setAlpha (names[i] == "-" ? 0.3f : 1.0f);
    }
    const float fHz = proc.viewFiltHz.load(), dHz = proc.viewDecHz.load(), gr = proc.viewGR.load();
    filterPanel->rightText = fHz > 0.0f ? String (fHz, fHz < 1000.0f ? 1 : 0) + " Hz" : String();
    decPanel->rightText = dHz > 0.0f ? "fs " + String (dHz, dHz < 1000.0f ? 1 : 0) + " Hz" : String();
    compPanel->rightText = gr > 0.05f ? "GR -" + String (gr, 1) + " dB" : String();
    compPanel->rightColour = gb::alert;
    for (auto* f : faders) f->refresh();

    if (presetBox.getText() != proc.currentPresetName && ! presetBox.isPopupActive())
        presetBox.setText (proc.currentPresetName, dontSendNotification);

    content.repaint();
}

void DreamMachineEditor::paintRatioDisplay (Graphics& g, Rectangle<int> r)
{
    std::array<int, 4> primes;
    for (int i = 0; i < 4; ++i) primes[(size_t) i] = (int) proc.apvts.getRawParameterValue (params::vid ("prime", i + 1))->load();

    const int x0 = r.getX() + 14, y0 = r.getY() + 28;
    const int cols[] = { 0, 44, 82, 154, 210, 330, 384 };
    const char* heads[] = { "VOICE", "GATE", "RATIO", "REDUCED", "FACTORS", "CENTS", "HZ" };
    g.setFont (gb::font (9.5f));
    g.setColour (gb::label);
    for (int i = 0; i < 7; ++i) g.drawText (heads[i], x0 + cols[i], y0, 120, 12, Justification::centredLeft);

    std::array<float, 5> f {};
    std::array<bool, 5> on {};
    std::array<int, 5> nums {};
    const int arpV = proc.viewArpVoice.load();
    for (int v = 0; v < 5; ++v)
    {
        const int y = y0 + 16 + v * 17;
        const int n = proc.view[(size_t) v].num.load(), d = jmax (1, proc.view[(size_t) v].den.load());
        nums[(size_t) v] = n;
        const float env = proc.view[(size_t) v].env.load();
        on[(size_t) v] = proc.view[(size_t) v].gate.load();
        f[(size_t) v] = proc.view[(size_t) v].freq.load();

        g.setFont (gb::font (11.0f));
        g.setColour (gb::title);
        g.drawText (params::voiceTag (v), x0 + cols[0], y, 44, 14, Justification::centredLeft);

        auto led = Rectangle<float> ((float) (x0 + cols[1]), (float) y + 2.0f, 28.0f, 10.0f);
        g.setColour (gb::track);
        g.fillRect (led);
        g.setColour (gb::bar);
        g.fillRect (led.withWidth (28.0f * jlimit (0.0f, 1.0f, env)));
        g.setColour (v == arpV ? gb::alert : gb::border);
        g.drawRect (led, 1.0f);

        const int gg = std::gcd (n, d);
        const int rn = n / gg, rd = d / gg;
        g.setFont (gb::font (11.0f, false));
        g.setColour (gb::text);
        g.drawText (String (n) + "/" + String (d), x0 + cols[2], y, 70, 14, Justification::centredLeft);
        g.setFont (gb::font (11.0f));
        g.drawText (String (rn) + "/" + String (rd), x0 + cols[3], y, 56, 14, Justification::centredLeft);

        const bool five = tuning::hasFactor (rn, 5) || tuning::hasFactor (rd, 5);
        const bool outside = ! tuning::isSmooth (n, primes) || ! tuning::isSmooth (d, primes);
        g.setColour (five ? gb::warn : (outside ? gb::alert : gb::text));
        g.setFont (gb::font (11.0f, false));
        String fs = String::fromUTF8 (tuning::factorString (rn).c_str()) + " / " + String::fromUTF8 (tuning::factorString (rd).c_str());
        g.drawText (fs, x0 + cols[4], y, 120, 14, Justification::centredLeft);

        g.setColour (gb::text);
        g.drawText (String (tuning::cents ((double) n / d), 1), x0 + cols[5], y, 54, 14, Justification::centredLeft);
        g.drawText (String (f[(size_t) v], 3), x0 + cols[6], y, 80, 14, Justification::centredLeft);
    }

    // estado bajo la tabla: periodo compuesto (la afinación es función del tiempo)
    {
        const float base = proc.viewBase.load(), k = proc.viewEmphHarm.load();
        const float common = proc.viewCommonHz.load();
        int y = y0 + 16 + 5 * 17 + 8;
        g.setFont (gb::font (11.0f, false));
        g.setColour (gb::title);
        String per;
        if (! proc.viewPeriodic.load()) per = "Forma de onda compuesta: NO periodica (temperada)";
        else if (common > 0.0f)
            per = "Periodo compuesto: " + String (common, 3) + " Hz = " + String (1000.0f / common, 1) + " ms   -   ciclos completos: "
                + String ((int) proc.viewCycles.load());
        else per = "Periodo compuesto: -";
        g.drawText (per, x0, y, 470, 14, Justification::centredLeft); y += 17;
        g.setColour (gb::text);
        String pr;
        for (int i = 0; i < 4; ++i) { const int pp = primes[(size_t) i]; if (pp > 1) pr << (pr.isEmpty() ? "" : " ") << pp; }
        g.drawText ("Base " + String (base, 3) + " Hz   -   Primos {" + pr + "}   -   Enfasis H" + String (k, 1) + " = " + String (base * k, 1) + " Hz",
                    x0, y, 470, 14, Justification::centredLeft); y += 17;
        g.drawText ("Permutacion: " + String (arpV >= 0 ? params::voiceTag (arpV) : "-"), x0, y, 470, 14, Justification::centredLeft);
    }

    // ---- columna central: batidos
    const int xb = r.getX() + 500;
    g.setFont (gb::font (9.5f));
    g.setColour (gb::label);
    g.drawText ("BEATS (< 20 Hz)", xb, y0, 150, 12, Justification::centredLeft);
    struct Pair { int a, b; float df; };
    std::vector<Pair> pairs;
    for (int a = 0; a < 5; ++a)
        for (int b = a + 1; b < 5; ++b)
            if (on[(size_t) a] && on[(size_t) b]) pairs.push_back ({ a, b, std::abs (f[(size_t) a] - f[(size_t) b]) });
    std::sort (pairs.begin(), pairs.end(), [] (const Pair& p1, const Pair& p2) { return p1.df < p2.df; });
    g.setFont (gb::font (11.0f, false));
    int yy = y0 + 16, cnt = 0;
    for (auto& pr : pairs)
    {
        if (pr.df >= 20.0f || cnt >= 8) break;
        g.setColour (gb::text);
        g.drawText (String (params::voiceTag (pr.a)) + "-" + params::voiceTag (pr.b) + ":  " + String (pr.df, 2) + " Hz", xb, yy, 150, 14, Justification::centredLeft);
        yy += 17; ++cnt;
    }
    if (cnt == 0) { g.setColour (gb::label); g.drawText ("-", xb, yy, 150, 14, Justification::centredLeft); }

    // ---- derecha: tonos de combinación (Two Systems of Eleven Categories)
    const int xc = r.getX() + 660;
    const int order = (int) proc.apvts.getRawParameterValue ("catOrder")->load();
    const int X = (int) proc.apvts.getRawParameterValue ("catX")->load();
    g.setFont (gb::font (9.5f));
    g.setColour (gb::label);
    g.drawText ("COMBINATION TONES (harmonics, order " + String (order) + ", X=" + String (X) + ")", xc, y0, 330, 12, Justification::centredLeft);

    std::vector<int> gen;
    for (int v = 0; v < 5; ++v) if (on[(size_t) v]) gen.push_back (nums[(size_t) v]);
    gen = combtones::reduce (gen);
    yy = y0 + 16;
    if (gen.size() < 2)
    {
        g.setFont (gb::font (11.0f, false));
        g.setColour (gb::label);
        g.drawText ("(activa al menos dos voces)", xc, yy, 330, 14, Justification::centredLeft);
    }
    else
    {
        const auto lv = combtones::levels (gen, order);
        int excluded = 0, total = 0;
        for (size_t k = 0; k < lv.size(); ++k)
        {
            // escribimos los números uno a uno para marcar en rojo los múltiplos de X
            float xx = (float) xc;
            g.setFont (gb::font (10.0f));
            g.setColour (gb::label);
            const String lab = k == 0 ? "GEN " : "O" + String ((int) k) + "  ";
            g.drawText (lab, (int) xx, yy, 34, 14, Justification::centredLeft);
            xx += 34.0f;
            g.setFont (gb::font (10.5f, false));
            int shown = 0;
            for (int val : lv[k])
            {
                ++total;
                const bool bad = val % X == 0;
                if (bad) ++excluded;
                if (xx > (float) (xc + 300)) continue;
                const String t = String (val);
                const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 5.0f;
                g.setColour (bad ? gb::warn : gb::text);
                g.drawText (t, (int) xx, yy, (int) tw + 2, 14, Justification::centredLeft);
                xx += tw;
                ++shown;
            }
            if (shown < (int) lv[k].size())
            {
                g.setColour (gb::label);
                g.drawText ("... (" + String ((int) lv[k].size()) + ")", (int) xx, yy, 60, 14, Justification::centredLeft);
            }
            yy += 17;
        }
        g.setFont (gb::font (11.0f, false));
        g.setColour (excluded > 0 ? gb::warn : gb::title);
        g.drawText ("Multiplos de " + String (X) + ": " + String (excluded) + " de " + String (total)
                    + (excluded == 0 ? "   -   conjunto PERMITIDO" : "   -   conjunto excluido"), xc, yy + 4, 330, 14, Justification::centredLeft);
        const uint32 mask = proc.allowedMask.load();
        int allowedCount = 0;
        for (int m = 0; m < 32; ++m) if ((mask >> m) & 1u) ++allowedCount;
        g.setColour (gb::label);
        g.drawText ("Combinaciones de voces permitidas: " + String (allowedCount) + " / 32", xc, yy + 21, 330, 14, Justification::centredLeft);
    }
}
