#pragma once
#include <JuceHeader.h>
#include <array>

// LineComp: a clean, simple feed-forward compressor.
//
// The COMPRESSION knob is a macro that moves the threshold line and the ratio line together.
// Dragging a line in the display adds a personal offset ("trim") on top of the knob, so everything
// stays automatable and the knob keeps working after you've fine-tuned the lines.
class LineCompProcessor : public juce::AudioProcessor
{
public:
    LineCompProcessor();
    ~LineCompProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LineComp"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    // ---- line maths (shared with the editor) ----
    static constexpr float floorDb = -48.0f;         // lowest threshold
    static constexpr float displayFloorDb = -60.0f;  // centre line of the display
    static constexpr float maxRatio = 20.0f;
    // the ratio line sits up to 24 dB under the threshold line (20:1), less if the display runs out of room
    static float ratioSpan (float thr) { return juce::jmin (24.0f, thr - displayFloorDb); }
    static float baseThreshold (float amount) { return -0.36f * amount; }
    static float baseRatio (float amount)     { return 1.5f + 0.065f * amount; }
    static float effectiveThreshold (float amount, float trim) { return juce::jlimit (floorDb, 0.0f, baseThreshold (amount) + trim); }
    static float effectiveRatio (float amount, float trim)     { return juce::jlimit (1.0f, maxRatio, baseRatio (amount) * std::pow (4.0f, trim)); }
    static float ratioLineDb (float thr, float ratio) { return thr - ratioSpan (thr) * std::log (ratio) / std::log (maxRatio); }

    float currentThreshold() const { return effectiveThreshold (raw ("amount"), raw ("thrTrim")); }
    float currentRatio() const     { return effectiveRatio (raw ("amount"), raw ("ratioTrim")); }
    float raw (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> uiScale { 1.0f };

    // ---- display history (written by audio thread, read by editor) ----
    static constexpr int numCols = 408;
    std::array<std::atomic<float>, numCols> histIn {}, histOut {};
    std::atomic<int> histWrite { 0 };
    std::atomic<float> grNow { 0.0f };   // dB of gain reduction (positive), peak since last read

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    double fs = 44100.0;
    juce::SmoothedValue<float> inSm, outSm, thrSm, ratSm, wetSm;
    float grFast = 0.0f, grSlow = 0.0f;

    int samplesPerCol = 400, colCount = 0;
    float colIn = 0.0f, colOut = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LineCompProcessor)
};
