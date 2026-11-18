#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class LineCompEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit LineCompEditor (LineCompProcessor&);
    ~LineCompEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    enum Kind { None, Knob, Style, Slider, ThrLine, RatioLine, BypassBtn, ResetBtn, SizeBtn };
    struct Target
    {
        Kind kind = None; int index = -1;
        bool operator== (const Target& o) const { return kind == o.kind && index == o.index; }
    };
    struct SliderDef { const char* id; const char* label; juce::Rectangle<float> row; float labelW, valueW; juce::Colour col; };

    Target findTarget (juce::Point<float>) const;
    juce::RangedAudioParameter* param (const char* id) const { return proc.apvts.getParameter (id); }
    float value (const char* id) const { return proc.raw (id); }
    void setNorm (const char* id, float n);
    void setReal (const char* id, float v);
    void resetAll();
    void openSizeMenu();
    juce::Rectangle<float> trackRect (const SliderDef&) const;
    juce::String formatGain (float db) const;

    // display mapping (amplitude in dB <-> distance from the centre line)
    float dbToY (float db) const;
    float yToDb (float y) const;
    juce::Range<float> ratioXSpan() const;
    juce::Rectangle<float> thrPill() const;
    juce::Rectangle<float> ratioPill() const;
    void dragLineTo (Kind k, float y);

    float uiScale() const { return (float) getWidth() / (float) W; }
    juce::Point<float> pos (const juce::MouseEvent& e) const { return e.position / uiScale(); }

    void drawPanel (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, juce::Colour dot) const;
    void drawPill (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, juce::Colour fill,
                   juce::Colour textCol, float fontSize = 11.0f) const;
    void drawKnob (juce::Graphics&) const;
    void drawDisplay (juce::Graphics&) const;
    void drawGrMeter (juce::Graphics&) const;

    static constexpr int W = 760, H = 420;

    LineCompProcessor& proc;

    juce::Rectangle<float> pHeader, pLeft, pDisplay, knobBox, display, grMeter, bSize, bBypass, bReset;
    juce::Rectangle<float> bStyle[3];
    std::vector<SliderDef> sliders;

    std::array<float, LineCompProcessor::numCols> viewIn {}, viewOut {};
    float grDisp = 0.0f, grHold = 0.0f;
    int grHoldTicks = 0;

    Target hover, active;
    float dragStartY = 0, dragStartX = 0, dragStartVal = 0;
    bool dragFine = false;
    bool sizeReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LineCompEditor)
};
