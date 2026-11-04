#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// ---------------------------------------------------------------------------
// Estética GenderBender: madera oscura, paneles negros translúcidos,
// barras blancas verticales, etiquetas grises giradas, Arial bold blanca.
namespace gb
{
    // Paleta 1.3: azules, grises, negros y blancos
    const juce::Colour panel   { juce::Colour::fromFloatRGBA (0.01f, 0.025f, 0.06f, 0.62f) };   // negro azulado translúcido
    const juce::Colour title   { juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.95f) };        // blanco
    const juce::Colour label   { juce::Colour::fromFloatRGBA (0.56f, 0.62f, 0.70f, 1.0f) };      // gris azulado
    const juce::Colour bar     { juce::Colour (0xff6fb2ff) };                                     // azul claro
    const juce::Colour barHi   { juce::Colour (0xffd6e9ff) };                                     // borde superior casi blanco
    const juce::Colour track   { juce::Colour::fromFloatRGBA (0.55f, 0.70f, 1.0f, 0.08f) };
    const juce::Colour btn     { juce::Colour::fromFloatRGBA (0.10f, 0.12f, 0.16f, 0.95f) };      // gris oscuro
    const juce::Colour btnHi   { juce::Colour::fromFloatRGBA (0.20f, 0.26f, 0.36f, 0.95f) };
    const juce::Colour btnOn   { juce::Colour (0xff2f7ae0) };                                     // azul (conmutador activo)
    const juce::Colour border  { juce::Colour::fromFloatRGBA (0.38f, 0.45f, 0.56f, 1.0f) };
    const juce::Colour text    { juce::Colour::fromFloatRGBA (0.93f, 0.95f, 0.98f, 1.0f) };
    const juce::Colour warn    { juce::Colour (0xff3d9bff) };   // azul intenso: razones con el factor 5
    const juce::Colour alert   { juce::Colour (0xffa9bdd6) };   // gris azulado claro: primos fuera del conjunto, avisos
    const juce::Colour trace   { juce::Colour (0xff9fd0ff) };   // osciloscopio

    // Arial si existe; si no, una equivalente instalada (en Linux Arial suele faltar)
    inline const juce::String& family()
    {
        static const juce::String name = []
        {
            const auto all = juce::Font::findAllTypefaceNames();
            for (auto* n : { "Arial", "Liberation Sans", "Arimo", "Helvetica", "Nimbus Sans", "DejaVu Sans", "Noto Sans" })
                if (all.contains (n, true)) return juce::String (n);
            return juce::Font::getDefaultSansSerifFontName();
        }();
        return name;
    }

    inline juce::Font font (float size, bool bold = true)
    {
        return juce::Font (juce::FontOptions (family(), size, bold ? juce::Font::bold : juce::Font::plain));
    }
}

class GBLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GBLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hover, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool hover, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return gb::font (11.0f); }
    juce::Font getPopupMenuFont() override { return gb::font (12.0f, false); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return gb::font (11.0f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    juce::Font getAlertWindowMessageFont() override { return gb::font (13.0f, false); }
    juce::Font getAlertWindowTitleFont() override { return gb::font (15.0f); }
};

// Barra vertical precisa (arrastre relativo, Shift = fino, Ctrl = muy fino,
// rueda, doble clic = valor por defecto, doble clic en el número o clic derecho = escribir)
class Fader : public juce::Component, public juce::SettableTooltipClient
{
public:
    Fader (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& name);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refresh();
    void setDisplayName (const juce::String& n) { if (n.toUpperCase() != name) { name = n.toUpperCase(); repaint(); } }

private:
    juce::String format (float plain) const;
    juce::Rectangle<float> barArea() const { return { 14.0f, 2.0f, 20.0f, (float) getHeight() - 17.0f }; }
    void setNorm (float n);

    juce::RangedAudioParameter* prm = nullptr;
    juce::String id, name;
    juce::Label valueLabel;
    bool isInt = false, isHz = false, dragging = false;
    float dragNorm = 0, lastY = 0;
};

class Toggle : public juce::TextButton
{
public:
    Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& text);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class Choice : public juce::ComboBox
{
public:
    Choice (juce::AudioProcessorValueTreeState& s, const juce::String& paramId);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

class Panel : public juce::Component
{
public:
    explicit Panel (const juce::String& t) : titleText (t) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics&) override;
    juce::String titleText;
    juce::String rightText;
    juce::Colour rightColour = gb::label;
};

// Osciloscopio X/Y (figuras de Lissajous) entre dos voces
class ScopeView : public juce::Component
{
public:
    explicit ScopeView (DreamMachineProcessor& p) : proc (p) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
    int vx = 0, vy = 1;
private:
    DreamMachineProcessor& proc;
};

// ---------------------------------------------------------------------------
class DreamMachineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int W = 1240, H = 1118;
    explicit DreamMachineEditor (DreamMachineProcessor&);
    ~DreamMachineEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    void buildWood();
    void layoutContent();
    void rebuildPresetList();
    void stepPreset (int delta);
    void selectPresetId (int id);
    void savePresetDialog();
    void deletePresetDialog();

    class Content : public juce::Component
    {
    public:
        explicit Content (DreamMachineEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override;
        void paintOverChildren (juce::Graphics& g) override;
        DreamMachineEditor& ed;
    };

    void paintRatioDisplay (juce::Graphics&, juce::Rectangle<int>);

    DreamMachineProcessor& proc;
    GBLookAndFeel lnf;
    Content content { *this };
    juce::Image wood;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<std::unique_ptr<Panel>> panels;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Fader*> faders;
    std::array<Fader*, 5> fxFaders {};
    std::array<Panel*, 5> voicePanels {};
    Panel *ratioPanel = nullptr, *walkPanel = nullptr, *topPanel = nullptr, *randomPanel = nullptr;
    Panel *filterPanel = nullptr, *decPanel = nullptr, *compPanel = nullptr;
    ScopeView* scopeView = nullptr;

    juce::ComboBox presetBox, refBox, scopeXBox, scopeYBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, loadBtn { "LOAD" }, delBtn { "DELETE" },
                     randomBtn { "RANDOM" }, loadWavBtn { "LOAD WAV" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label userWavLabel;
    juce::Array<juce::File> userFiles;
    std::vector<int> presetOrder;  // ids del combo en orden

    template <typename T, typename... Args>
    T* make (Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto* raw = c.get();
        content.addAndMakeVisible (*raw);
        owned.push_back (std::move (c));
        return raw;
    }
    Panel* addPanel (const juce::String& title, juce::Rectangle<int> r);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamMachineEditor)
};
