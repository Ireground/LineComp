#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct StyleSettings { float kneeDb, attackMs, releaseMs, slowReleaseMs; };
    // SOFT = wide knee, gentle and slow;  HARD = no knee, fast and firm.
    constexpr StyleSettings styles[3] = {
        { 12.0f, 20.0f, 150.0f, 1200.0f },   // soft
        {  6.0f,  8.0f,  90.0f,  900.0f },   // neutral
        {  0.0f,  2.5f,  60.0f,  600.0f },   // hard
    };

    float coeff (float ms, double fs) { return (float) std::exp (-1.0 / (ms * 0.001 * fs)); }

    // static curve: returns gain reduction in dB (>= 0)
    float gainReduction (float x, float t, float r, float knee)
    {
        const float over = x - t;
        if (knee > 0.01f && 2.0f * std::abs (over) <= knee)
        {
            const float k = over + knee * 0.5f;
            return (1.0f - 1.0f / r) * k * k / (2.0f * knee);
        }
        return over > 0.0f ? over * (1.0f - 1.0f / r) : 0.0f;
    }
}

LineCompProcessor::LineCompProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LineComp", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout LineCompProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Compression",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thrTrim", 1 }, "Threshold Trim",
                                                  NormalisableRange<float> (-48.0f, 48.0f, 0.01f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ratioTrim", 1 }, "Ratio Trim",
                                                  NormalisableRange<float> (-2.0f, 2.0f, 0.001f), 0.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style",
                                                   StringArray { "Soft", "Neutral", "Hard" }, 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "inGain", 1 }, "In Gain",
                                                  NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "outGain", 1 }, "Out Gain",
                                                  NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

bool LineCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

void LineCompProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto* s : { &inSm, &outSm, &wetSm }) s->reset (sampleRate, 0.02);
    thrSm.reset (sampleRate, 0.05);
    ratSm.reset (sampleRate, 0.05);
    inSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw ("inGain")));
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw ("outGain")));
    thrSm.setCurrentAndTargetValue (currentThreshold());
    ratSm.setCurrentAndTargetValue (currentRatio());
    wetSm.setCurrentAndTargetValue (raw ("bypass") > 0.5f ? 0.0f : 1.0f);
    grFast = grSlow = 0.0f;

    // the display shows about 4 seconds
    samplesPerCol = juce::jmax (16, (int) (sampleRate * 4.0 / numCols));
    colCount = 0;
    colIn = colOut = 0.0f;
}

void LineCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (numCh == 0) return;

    inSm.setTargetValue (juce::Decibels::decibelsToGain (raw ("inGain")));
    outSm.setTargetValue (juce::Decibels::decibelsToGain (raw ("outGain")));
    thrSm.setTargetValue (currentThreshold());
    ratSm.setTargetValue (currentRatio());
    wetSm.setTargetValue (raw ("bypass") > 0.5f ? 0.0f : 1.0f);

    const auto& st = styles[juce::jlimit (0, 2, (int) raw ("style"))];
    const float aAtt  = coeff (st.attackMs, fs);
    const float aRel  = coeff (st.releaseMs, fs);
    const float aSAtt = coeff (400.0f, fs);
    const float aSRel = coeff (st.slowReleaseMs, fs);

    float* d[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numCh > 1 ? 1 : 0) };
    float grPeak = grNow.load();

    for (int i = 0; i < n; ++i)
    {
        const float inG = inSm.getNextValue(), outG = outSm.getNextValue();
        const float thr = thrSm.getNextValue(), ratio = ratSm.getNextValue();
        const float wet = wetSm.getNextValue();

        // stereo-linked peak detector
        float pk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) pk = juce::jmax (pk, std::abs (d[ch][i] * inG));
        const float xdb = pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -120.0f;
        const float target = gainReduction (xdb, thr, ratio, st.kneeDb);

        // two-stage release: fast for transients, slow once compression has been sustained
        grFast = target + (target > grFast ? aAtt : aRel) * (grFast - target);
        grSlow = grFast + (grFast > grSlow ? aSAtt : aSRel) * (grSlow - grFast);
        const float gr = juce::jmax (grFast, grSlow);
        const float g = std::exp (gr * -0.1151292546f);   // dB -> gain

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = d[ch][i];
            const float w = dry * inG * g * outG;
            d[ch][i] = dry + wet * (w - dry);
        }

        colIn  = juce::jmax (colIn, pk);
        colOut = juce::jmax (colOut, pk * g);
        grPeak = juce::jmax (grPeak, gr);
        if (++colCount >= samplesPerCol)
        {
            const int w = histWrite.load (std::memory_order_relaxed);
            histIn[(size_t) w].store (colIn, std::memory_order_relaxed);
            histOut[(size_t) w].store (colOut, std::memory_order_relaxed);
            histWrite.store ((w + 1) % numCols, std::memory_order_release);
            colIn = colOut = 0.0f;
            colCount = 0;
        }
    }
    if (numCh == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    grNow.store (grPeak);
}

juce::AudioProcessorEditor* LineCompProcessor::createEditor() { return new LineCompEditor (*this); }

void LineCompProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void LineCompProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            uiScale = (float) t.getProperty ("uiScale", 1.0f);
            apvts.replaceState (t);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LineCompProcessor(); }
