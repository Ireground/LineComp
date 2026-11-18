#include "PluginEditor.h"

namespace
{
    // HatsOff light scheme
    const juce::Colour cBg      { 0xFF141414 };
    const juce::Colour cPanel   { 0xFFE4E4E1 };
    const juce::Colour cInset   { 0xFFC9C9C5 };
    const juce::Colour cInset2  { 0xFFBBBBB7 };
    const juce::Colour cLabel   { 0xFF5C5C59 };
    const juce::Colour cText    { 0xFF1C1C1A };
    const juce::Colour cTrack   { 0xFFB4B4B0 };
    const juce::Colour cOrange  { 0xFFEF5C25 };
    const juce::Colour cOrangeH { 0xFFD94E1C };
    const juce::Colour cBlue    { 0xFF3960B6 };
    const juce::Colour cTeal    { 0xFF2D908B };
    const juce::Colour cTealHov { 0xFF267A76 };
    const juce::Colour cPurple  { 0xFF8A4AB8 };
    const juce::Colour cGreen   { 0xFF23955F };
    const juce::Colour cWave    { 0xFFA4A4A0 };

    juce::Font mono (float size, bool bold = false)
    {
       #if JUCE_WINDOWS
        const juce::String name ("Consolas");
       #else
        const juce::String name (juce::Font::getDefaultMonospacedFontName());
       #endif
        return juce::Font (juce::FontOptions (name, size, bold ? juce::Font::bold : juce::Font::plain));
    }

    const juce::String dotSep (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    const char* styleNames[3] = { "SOFT", "NEUTRAL", "HARD" };
    constexpr float grRange = 24.0f;
    constexpr float knobA0 = -2.35f, knobA1 = 2.35f;   // radians, 0 = 12 o'clock

    juce::String ratioText (float r)
    {
        return (r >= 9.95f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1)) + ":1";
    }
}

//==============================================================================
LineCompEditor::LineCompEditor (LineCompProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    using R = juce::Rectangle<float>;
    pHeader  = R (10, 10, 740, 46);
    bSize    = R (316, 23, 96, 20);

    pLeft    = R (10, 64, 240, 346);
    knobBox  = R (24, 90, 212, 150);
    for (int i = 0; i < 3; ++i) bStyle[i] = R (24 + i * 74.0f, 252, 64, 24);
    sliders = {
        { "inGain",  "IN GAIN",  R (24, 292, 212, 26), 62, 66, cPurple },
        { "outGain", "OUT GAIN", R (24, 320, 212, 26), 62, 66, cPurple },
    };
    bBypass  = R (24, 372, 100, 24);
    bReset   = R (136, 372, 100, 24);

    pDisplay = R (258, 64, 492, 346);
    display  = R (272, 90, 408, 306);
    grMeter  = R (690, 90, 46, 306);

    // Read the saved size BEFORE setting resize limits: setResizeLimits() triggers resized()
    // at the minimum size, which would otherwise overwrite the saved scale.
    const float s = juce::jlimit (0.75f, 2.5f, proc.uiScale.load());
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 5 / 2, H * 5 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / (double) H);
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    sizeReady = true;
    startTimerHz (30);
}

void LineCompEditor::resized() { if (sizeReady) proc.uiScale = uiScale(); }

void LineCompEditor::timerCallback()
{
    const int w = proc.histWrite.load (std::memory_order_acquire);
    for (int i = 0; i < LineCompProcessor::numCols; ++i)
    {
        const auto idx = (size_t) ((w + i) % LineCompProcessor::numCols);
        viewIn[(size_t) i]  = proc.histIn[idx].load (std::memory_order_relaxed);
        viewOut[(size_t) i] = proc.histOut[idx].load (std::memory_order_relaxed);
    }

    const float gr = proc.grNow.exchange (0.0f);
    grDisp = gr > grDisp ? gr : grDisp + 0.3f * (gr - grDisp);
    if (gr >= grHold) { grHold = gr; grHoldTicks = 30; }
    else if (--grHoldTicks <= 0) grHold = juce::jmax (0.0f, grHold - 0.4f);
    repaint();
}

//==============================================================================
float LineCompEditor::dbToY (float db) const
{
    const float half = display.getHeight() * 0.5f - 6.0f;
    const float f = LineCompProcessor::displayFloorDb;
    return display.getCentreY() - (juce::jlimit (f, 0.0f, db) - f) / -f * half;
}

float LineCompEditor::yToDb (float y) const
{
    const float half = display.getHeight() * 0.5f - 6.0f;
    const float f = LineCompProcessor::displayFloorDb;
    return juce::jlimit (f, 0.0f, f + (display.getCentreY() - y) / half * -f);
}

juce::Range<float> LineCompEditor::ratioXSpan() const
{
    return { display.getX() + display.getWidth() * 0.18f, display.getX() + display.getWidth() * 0.60f };
}

juce::Rectangle<float> LineCompEditor::thrPill() const
{
    const float y = juce::jlimit (display.getY() + 11.0f, display.getBottom() - 11.0f, dbToY (proc.currentThreshold()));
    return juce::Rectangle<float> (150, 18).withCentre ({ display.getX() + display.getWidth() * 0.80f, y });
}

juce::Rectangle<float> LineCompEditor::ratioPill() const
{
    const float y = juce::jlimit (display.getY() + 11.0f, display.getBottom() - 11.0f,
                                  dbToY (LineCompProcessor::ratioLineDb (proc.currentThreshold(), proc.currentRatio())));
    return { ratioXSpan().getStart(), y - 9.0f, 104.0f, 18.0f };
}

juce::Rectangle<float> LineCompEditor::trackRect (const SliderDef& s) const
{
    return { s.row.getX() + s.labelW, s.row.getY(), s.row.getWidth() - s.labelW - s.valueW, s.row.getHeight() };
}

juce::String LineCompEditor::formatGain (float v) const
{
    return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
}

void LineCompEditor::setNorm (const char* id, float n)
{
    if (auto* p = param (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n));
}

void LineCompEditor::setReal (const char* id, float v)
{
    if (auto* p = param (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void LineCompEditor::resetAll()
{
    for (auto* id : { "amount", "thrTrim", "ratioTrim", "style", "inGain", "outGain", "bypass" })
    {
        auto* p = param (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

LineCompEditor::Target LineCompEditor::findTarget (juce::Point<float> pt) const
{
    if (bSize.contains (pt))   return { SizeBtn, -1 };
    if (bBypass.contains (pt)) return { BypassBtn, -1 };
    if (bReset.contains (pt))  return { ResetBtn, -1 };
    if (knobBox.contains (pt)) return { Knob, -1 };
    for (int i = 0; i < 3; ++i) if (bStyle[i].contains (pt)) return { Style, i };
    for (int i = 0; i < (int) sliders.size(); ++i)
        if (trackRect (sliders[(size_t) i]).expanded (6, 0).contains (pt)) return { Slider, i };

    if (display.expanded (0, 4).contains (pt))
    {
        if (ratioPill().contains (pt)) return { RatioLine, -1 };
        if (thrPill().contains (pt))   return { ThrLine, -1 };
        const float thrY = dbToY (proc.currentThreshold());
        const float ratY = dbToY (LineCompProcessor::ratioLineDb (proc.currentThreshold(), proc.currentRatio()));
        const float dT = std::abs (pt.y - thrY);
        const bool inSpan = ratioXSpan().expanded (12.0f).contains (pt.x);
        const float dR = inSpan ? std::abs (pt.y - ratY) : 1.0e9f;
        const bool hitT = dT <= 7.0f, hitR = dR <= 7.0f;
        if (hitT && hitR)
        {
            if (std::abs (dR - dT) < 0.5f) return { pt.y > thrY ? RatioLine : ThrLine, -1 };
            return { dR < dT ? RatioLine : ThrLine, -1 };
        }
        if (hitR) return { RatioLine, -1 };
        if (hitT) return { ThrLine, -1 };
    }
    return {};
}

//==============================================================================
void LineCompEditor::dragLineTo (Kind k, float y)
{
    const float amount = value ("amount");
    if (k == ThrLine)
    {
        const float thr = juce::jlimit (LineCompProcessor::floorDb, 0.0f, yToDb (y));
        setReal ("thrTrim", thr - LineCompProcessor::baseThreshold (amount));
    }
    else if (k == RatioLine)
    {
        const float thr = proc.currentThreshold();
        const float span = LineCompProcessor::ratioSpan (thr);
        const float dist = juce::jlimit (0.0f, span, thr - yToDb (y));
        const float ratio = std::pow (LineCompProcessor::maxRatio, dist / span);
        setReal ("ratioTrim", std::log (ratio / LineCompProcessor::baseRatio (amount)) / std::log (4.0f));
    }
}

void LineCompEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    if (! (t == hover))
    {
        hover = t;
        setMouseCursor ((t.kind == ThrLine || t.kind == RatioLine || t.kind == Knob) ? juce::MouseCursor::UpDownResizeCursor
                        : t.kind == Slider ? juce::MouseCursor::LeftRightResizeCursor
                        : t.kind == None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void LineCompEditor::mouseExit (const juce::MouseEvent&) { hover = {}; repaint(); }

void LineCompEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto pt = pos (e);
    const auto t = findTarget (pt);
    active = {};
    dragStartX = pt.x;
    dragStartY = pt.y;
    dragFine = e.mods.isShiftDown();

    switch (t.kind)
    {
        case SizeBtn:   openSizeMenu(); return;
        case ResetBtn:  resetAll(); break;
        case BypassBtn:
        {
            auto* p = param ("bypass");
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
            break;
        }
        case Style:
        {
            auto* p = param ("style");
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) t.index));
            p->endChangeGesture();
            break;
        }
        case Knob:
        case Slider:
            active = t;
            dragStartVal = param (t.kind == Knob ? "amount" : sliders[(size_t) t.index].id)->getValue();
            param (t.kind == Knob ? "amount" : sliders[(size_t) t.index].id)->beginChangeGesture();
            break;
        case ThrLine:
            active = t;
            dragStartVal = dbToY (proc.currentThreshold());   // line y at grab time, so it doesn't jump
            param ("thrTrim")->beginChangeGesture();
            break;
        case RatioLine:
            active = t;
            dragStartVal = dbToY (LineCompProcessor::ratioLineDb (proc.currentThreshold(), proc.currentRatio()));
            param ("ratioTrim")->beginChangeGesture();
            break;
        case None: default: break;
    }
    repaint();
}

void LineCompEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (active.kind == None) return;
    const auto pt = pos (e);
    const bool fine = e.mods.isShiftDown();

    if (active.kind == ThrLine || active.kind == RatioLine)
    {
        if (fine != dragFine)   // re-anchor so switching to fine mode doesn't jump
        {
            dragFine = fine;
            dragStartVal = active.kind == ThrLine ? dbToY (proc.currentThreshold())
                                                  : dbToY (LineCompProcessor::ratioLineDb (proc.currentThreshold(), proc.currentRatio()));
            dragStartY = pt.y;
        }
        dragLineTo (active.kind, dragStartVal + (pt.y - dragStartY) * (fine ? 0.2f : 1.0f));
        repaint();
        return;
    }

    const char* id = active.kind == Knob ? "amount" : sliders[(size_t) active.index].id;
    if (fine != dragFine)
    {
        dragFine = fine;
        dragStartX = pt.x;
        dragStartY = pt.y;
        dragStartVal = param (id)->getValue();
    }
    if (active.kind == Slider)
        setNorm (id, dragStartVal + (pt.x - dragStartX) / trackRect (sliders[(size_t) active.index]).getWidth() * (fine ? 0.15f : 1.0f));
    else
        setNorm (id, dragStartVal + (dragStartY - pt.y) / (fine ? 1200.0f : 200.0f));
    repaint();
}

void LineCompEditor::mouseUp (const juce::MouseEvent& e)
{
    switch (active.kind)
    {
        case Knob:      param ("amount")->endChangeGesture(); break;
        case Slider:    param (sliders[(size_t) active.index].id)->endChangeGesture(); break;
        case ThrLine:   param ("thrTrim")->endChangeGesture(); break;
        case RatioLine: param ("ratioTrim")->endChangeGesture(); break;
        default: break;
    }
    active = {};
    hover = findTarget (pos (e));
    repaint();
}

void LineCompEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    const char* id = t.kind == Knob ? "amount"
                   : t.kind == Slider ? sliders[(size_t) t.index].id
                   : t.kind == ThrLine ? "thrTrim"
                   : t.kind == RatioLine ? "ratioTrim" : nullptr;
    if (id == nullptr) return;
    auto* p = param (id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
    repaint();
}

void LineCompEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-4f) return;
    const float dir = w.deltaY > 0 ? 1.0f : -1.0f;
    const bool fine = e.mods.isShiftDown();
    const auto t = findTarget (pos (e));

    if (t.kind == ThrLine || t.kind == RatioLine)
    {
        // wheel up = line moves up (less compression)
        const char* id = t.kind == ThrLine ? "thrTrim" : "ratioTrim";
        param (id)->beginChangeGesture();
        if (t.kind == ThrLine)
            setReal (id, value (id) + dir * (fine ? 0.1f : 0.5f));
        else
            setReal (id, value (id) - dir * (fine ? 0.005f : 0.025f));
        param (id)->endChangeGesture();
        repaint();
        return;
    }
    if (t.kind != Slider && t.kind != Knob) return;
    auto* p = param (t.kind == Knob ? "amount" : sliders[(size_t) t.index].id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + dir * (fine ? 0.002f : 0.01f)));
    p->endChangeGesture();
    repaint();
}

void LineCompEditor::openSizeMenu()
{
    juce::PopupMenu m;
    const int pcts[] = { 100, 125, 150, 175, 200 };
    const int cur = juce::roundToInt (uiScale() * 100.0f);
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, juce::String (pcts[i]) + "%", true, std::abs (cur - pcts[i]) <= 1);
    juce::Component::SafePointer<LineCompEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal ((bSize * uiScale()).toNearestInt())),
                     [safe, pcts] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         const float f = pcts[r - 1] / 100.0f;
                         safe->setSize (juce::roundToInt (W * f), juce::roundToInt (H * f));
                     });
}

//==============================================================================
void LineCompEditor::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour dot) const
{
    g.setColour (cPanel);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (cLabel);
    g.setFont (mono (12.0f));
    g.drawText (title, r.getX() + 14, r.getY() + 8, r.getWidth() - 40, 14, juce::Justification::centredLeft);
    g.setColour (dot);
    g.fillEllipse (r.getRight() - 19, r.getY() + 11, 8, 8);
}

void LineCompEditor::drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                               juce::Colour fill, juce::Colour textCol, float fontSize) const
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
    g.setColour (textCol);
    g.setFont (mono (fontSize, true));
    g.drawText (text, r, juce::Justification::centred);
}

void LineCompEditor::drawKnob (juce::Graphics& g) const
{
    const bool hot = (hover.kind == Knob && active.kind == None) || active.kind == Knob;
    g.setColour (cInset);
    g.fillRoundedRectangle (knobBox, 7.0f);
    if (hot)
    {
        g.setColour (cInset2);
        g.fillRoundedRectangle (knobBox.reduced (4), 5.0f);
    }

    const float amount = value ("amount");
    const float n = amount / 100.0f;
    const juce::Point<float> c (knobBox.getCentreX(), knobBox.getY() + 70.0f);
    const float r = 52.0f;

    juce::Path track, fill;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, knobA0, knobA1, true);
    g.setColour (cPanel);
    g.strokePath (track, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (n > 0.001f)
    {
        fill.addCentredArc (c.x, c.y, r, r, 0.0f, knobA0, knobA0 + (knobA1 - knobA0) * n, true);
        g.setColour (cOrange);
        g.strokePath (fill, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // pointer tick
    {
        const float a = knobA0 + (knobA1 - knobA0) * n;
        g.setColour (cText);
        g.drawLine ({ c.getPointOnCircumference (r - 16.0f, a), c.getPointOnCircumference (r - 6.0f, a) }, 2.5f);
    }

    g.setColour (cText);
    g.setFont (mono (34.0f, true));
    g.drawText (juce::String (juce::roundToInt (amount)), juce::Rectangle<float> (120, 40).withCentre (c.translated (0, 4)),
                juce::Justification::centred);
    g.setColour (cLabel);
    g.setFont (mono (11.0f, true));
    g.drawText ("COMPRESSION", knobBox.getX(), knobBox.getBottom() - 24, knobBox.getWidth(), 14, juce::Justification::centred);
}

void LineCompEditor::drawDisplay (juce::Graphics& g) const
{
    g.setColour (cInset);
    g.fillRoundedRectangle (display, 6.0f);

    const float cy = display.getCentreY();
    const float thr = proc.currentThreshold();
    const float ratio = proc.currentRatio();
    const float thrY = dbToY (thr), botThrY = 2.0f * cy - thrY;
    const float ratY = dbToY (LineCompProcessor::ratioLineDb (thr, ratio));

    g.saveState();
    g.reduceClipRegion (display.reduced (1).toNearestInt());

    // area above the threshold = where compression happens
    g.setColour (cOrange.withAlpha (0.07f));
    g.fillRect (display.getX(), display.getY(), display.getWidth(), thrY - display.getY());
    g.fillRect (display.getX(), botThrY, display.getWidth(), display.getBottom() - botThrY);

    // grid
    g.setFont (mono (9.5f));
    for (float db : { -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
    {
        const float y = dbToY (db), y2 = 2.0f * cy - y;
        g.setColour (cInset2);
        g.fillRect (display.getX(), y, display.getWidth(), 1.0f);
        g.fillRect (display.getX(), y2, display.getWidth(), 1.0f);
        g.setColour (cLabel.withAlpha (0.7f));
        g.drawText (juce::String ((int) db), display.getX() + 4, y - 11, 30, 10, juce::Justification::centredLeft);
    }
    g.setColour (cTrack);
    g.fillRect (display.getX(), cy - 0.5f, display.getWidth(), 1.0f);

    // waveforms: mirrored top/bottom, oldest on the left
    auto wavePath = [&] (const std::array<float, LineCompProcessor::numCols>& data)
    {
        juce::Path p;
        const int nC = LineCompProcessor::numCols;
        const float dx = display.getWidth() / (float) (nC - 1);
        auto yFor = [&] (float a) { return dbToY (a > 1.0e-6f ? juce::Decibels::gainToDecibels (a) : -120.0f); };
        p.startNewSubPath (display.getX(), cy);
        for (int i = 0; i < nC; ++i) p.lineTo (display.getX() + i * dx, yFor (data[(size_t) i]));
        for (int i = nC - 1; i >= 0; --i) p.lineTo (display.getX() + i * dx, 2.0f * cy - yFor (data[(size_t) i]));
        p.closeSubPath();
        return p;
    };
    const auto pin = wavePath (viewIn), pout = wavePath (viewOut);
    g.setColour (cWave);
    g.fillPath (pin);
    g.setColour (cBlue.withAlpha (0.45f));
    g.fillPath (pout);
    g.setColour (cBlue);
    g.strokePath (pout, juce::PathStrokeType (1.0f));

    // threshold line (+ faint mirror)
    const bool thrHot = (hover.kind == ThrLine && active.kind == None) || active.kind == ThrLine;
    const bool ratHot = (hover.kind == RatioLine && active.kind == None) || active.kind == RatioLine;
    {
        const float dl[] = { 4.0f, 4.0f };
        g.setColour (cOrange.withAlpha (0.6f));
        g.drawDashedLine ({ display.getX(), botThrY, display.getRight(), botThrY }, dl, 2, 1.0f);
    }
    g.setColour (thrHot ? cOrangeH : cOrange);
    g.fillRect (display.getX(), thrY - (thrHot ? 1.5f : 1.0f), display.getWidth(), thrHot ? 3.0f : 2.0f);

    // ratio line
    const auto span = ratioXSpan();
    g.setColour (cPurple);
    g.fillRect (span.getStart(), ratY - (ratHot ? 1.5f : 1.0f), span.getLength(), ratHot ? 3.0f : 2.0f);
    // little connector showing the drop from threshold to ratio line
    if (ratY - thrY > 3.0f)
    {
        g.setColour (cPurple.withAlpha (0.5f));
        g.fillRect (span.getEnd() - 1.0f, thrY, 1.0f, ratY - thrY);
    }

    drawPill (g, ratioPill(), "RATIO " + ratioText (ratio), ratHot ? cPurple.darker (0.2f) : cPurple, juce::Colours::white, 11.0f);
    drawPill (g, thrPill(), "THRESHOLD " + juce::String (thr, 1) + " dB", thrHot ? cOrangeH : cOrange, juce::Colours::white, 11.0f);

    if (value ("bypass") > 0.5f)
        drawPill (g, { display.getRight() - 92, display.getY() + 8, 84, 18 }, "BYPASSED", cText, juce::Colours::white, 10.5f);

    g.restoreState();
}

void LineCompEditor::drawGrMeter (juce::Graphics& g) const
{
    g.setColour (cInset);
    g.fillRoundedRectangle (grMeter, 6.0f);

    const auto bar = juce::Rectangle<float> (grMeter.getX() + 12, grMeter.getY() + 26, grMeter.getWidth() - 24, grMeter.getHeight() - 52);
    g.setColour (cInset2);
    g.fillRoundedRectangle (bar, 3.0f);

    // tick marks every 6 dB
    g.setColour (cLabel.withAlpha (0.5f));
    for (int db = 6; db < (int) grRange; db += 6)
    {
        const float y = bar.getY() + bar.getHeight() * db / grRange;
        g.fillRect (bar.getX() - 4, y, 3.0f, 1.0f);
        g.fillRect (bar.getRight() + 1, y, 3.0f, 1.0f);
    }

    const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, grDisp / grRange);
    if (h > 0.5f)
    {
        g.setColour (cOrange);
        g.fillRoundedRectangle (bar.withHeight (h), 3.0f);
    }
    const float hy = bar.getY() + bar.getHeight() * juce::jlimit (0.0f, 1.0f, grHold / grRange);
    if (grHold > 0.05f)
    {
        g.setColour (cText);
        g.fillRect (bar.getX(), hy - 1.0f, bar.getWidth(), 2.0f);
    }

    g.setColour (grDisp > 0.05f ? cOrange : cLabel);
    g.setFont (mono (11.0f, true));
    g.drawText (grDisp > 0.05f ? juce::String (-grDisp, 1) : juce::String ("0.0"),
                grMeter.getX(), grMeter.getY() + 6, grMeter.getWidth(), 14, juce::Justification::centred);
    g.setColour (cLabel);
    g.setFont (mono (10.0f, true));
    g.drawText ("GR", grMeter.getX(), grMeter.getBottom() - 20, grMeter.getWidth(), 14, juce::Justification::centred);
}

void LineCompEditor::paint (juce::Graphics& g)
{
    g.fillAll (cBg);
    g.addTransform (juce::AffineTransform::scale (uiScale()));
    auto hov = [this] (Kind k, int idx = -1) { return hover.kind == k && hover.index == idx && active.kind == None; };

    const int style = juce::jlimit (0, 2, (int) value ("style"));
    const bool bypassed = value ("bypass") > 0.5f;

    // ---------- header ----------
    g.setColour (cPanel);
    g.fillRoundedRectangle (pHeader, 7.0f);
    g.setFont (mono (25.0f, true));
    g.setColour (cText);
    g.drawText ("LINE", pHeader.getX() + 16, pHeader.getY(), 90, pHeader.getHeight(), juce::Justification::centredLeft);
    g.setColour (cOrange);
    g.drawText ("COMP", pHeader.getX() + 16 + 56, pHeader.getY(), 90, pHeader.getHeight(), juce::Justification::centredLeft);
    g.setFont (mono (11.0f));
    g.setColour (cLabel);
    g.drawText ("CLEAN COMPRESSOR", pHeader.getX() + 150, pHeader.getY() + 2, 150, pHeader.getHeight(), juce::Justification::centredLeft);

    drawPill (g, bSize, "SIZE " + juce::String (juce::roundToInt (uiScale() * 100.0f)) + "%", hov (SizeBtn) ? cInset2 : cInset, cText);
    {
        juce::Path tri;
        const float tx = bSize.getRight() - 12, ty = bSize.getCentreY();
        tri.addTriangle (tx - 3.5f, ty - 2, tx + 3.5f, ty - 2, tx, ty + 2.5f);
        g.setColour (cText);
        g.fillPath (tri);
    }
    {
        const juce::String st = bypassed ? juce::String ("BYPASSED")
                              : "THR " + juce::String (proc.currentThreshold(), 1) + " dB" + dotSep
                                + ratioText (proc.currentRatio()) + dotSep + styleNames[style];
        const float pw = st.length() * 6.4f + 22.0f;
        drawPill (g, { pHeader.getRight() - 14 - pw, pHeader.getY() + 13, pw, 20 }, st,
                  bypassed ? cText : cTeal, juce::Colours::white, 11.0f);
    }

    // ---------- left: compression ----------
    drawPanel (g, pLeft, "COMPRESSION", cOrange);
    drawKnob (g);

    for (int i = 0; i < 3; ++i)
    {
        const bool on = i == style;
        drawPill (g, bStyle[i], styleNames[i],
                  on ? (hov (Style, i) ? cTealHov : cTeal) : (hov (Style, i) ? cInset2 : cInset),
                  on ? juce::Colours::white : cLabel, 10.5f);
    }

    for (int i = 0; i < (int) sliders.size(); ++i)
    {
        const auto& s = sliders[(size_t) i];
        const auto tr = trackRect (s);
        const bool hot = hov (Slider, i) || (active.kind == Slider && active.index == i);
        g.setFont (mono (11.0f));
        g.setColour (cLabel);
        g.drawText (s.label, s.row.getX(), s.row.getY(), s.labelW - 4, s.row.getHeight(), juce::Justification::centredLeft);
        g.setFont (mono (12.0f, true));
        g.setColour (cText);
        g.drawText (formatGain (value (s.id)), tr.getRight() + 4, s.row.getY(), s.valueW - 4, s.row.getHeight(), juce::Justification::centredRight);

        const float n = param (s.id)->getValue();
        const float y = tr.getCentreY();
        const float cx = tr.getX() + 0.5f * tr.getWidth(), hx = tr.getX() + n * tr.getWidth();
        g.setColour (cTrack);
        g.fillRect (tr.getX(), y - 1, tr.getWidth(), 2.0f);
        g.setColour (s.col);
        g.fillRect (std::min (cx, hx), y - 1, std::abs (hx - cx), 2.0f);
        g.setColour (hot ? cText : s.col);
        g.fillRect (hx - 1.5f, y - 6, 3.0f, 12.0f);
    }

    drawPill (g, bBypass, bypassed ? "BYPASSED" : "BYPASS",
              bypassed ? (hov (BypassBtn) ? cOrangeH : cOrange) : (hov (BypassBtn) ? cInset2 : cInset),
              bypassed ? juce::Colours::white : cText, 11.0f);
    drawPill (g, bReset, "RESET", hov (ResetBtn) ? cInset2 : cInset, cText, 11.0f);

    // ---------- centre: display ----------
    drawPanel (g, pDisplay, "INPUT / OUTPUT", cBlue);
    {
        // legend
        const float ly = pDisplay.getY() + 10, lx = pDisplay.getRight() - 150;
        g.setFont (mono (10.0f, true));
        g.setColour (cWave);
        g.fillRoundedRectangle (lx, ly + 1, 10, 10, 2);
        g.setColour (cLabel);
        g.drawText ("IN", lx + 14, ly, 30, 12, juce::Justification::centredLeft);
        g.setColour (cBlue);
        g.fillRoundedRectangle (lx + 42, ly + 1, 10, 10, 2);
        g.setColour (cLabel);
        g.drawText ("OUT", lx + 56, ly, 30, 12, juce::Justification::centredLeft);
        g.setColour (cOrange);
        g.fillRoundedRectangle (lx + 90, ly + 1, 10, 10, 2);
        g.setColour (cLabel);
        g.drawText ("GR", lx + 104, ly, 30, 12, juce::Justification::centredLeft);
    }
    drawDisplay (g);
    drawGrMeter (g);
}
