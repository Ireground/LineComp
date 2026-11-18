# LineComp

A very simple, very clean everyday compressor. No saturation, no vintage emulation.
You compress by moving **lines** over a live picture of your audio.

## The display
- **Grey** = input waveform, **blue** = output waveform (after compression). The gap between them is the gain reduction.
- **GR meter** (right): current gain reduction in dB, with a short peak-hold marker.
- **Threshold line (orange)**: drag it **down** to lower the threshold, so more of the signal gets compressed.
  The shaded area above it is where compression happens.
- **Ratio line (purple)**: drag it **down, away from the threshold line**, to raise the ratio
  (on the threshold line = 1:1, at the bottom of its range = 20:1).

Lines: drag (Shift = fine), mouse wheel nudge, double-click to reset your adjustment.

## Controls
- **COMPRESSION** (0–100): one knob for "more or less compression". It moves both lines together
  (more = lower threshold and higher ratio). Your line drags are kept as an offset on top of it.
- **SOFT / NEUTRAL / HARD**: how the compression grabs.
  - Soft: wide knee (12 dB), slower attack (20 ms): gentle and transparent.
  - Neutral: medium knee (6 dB), 8 ms attack: all-rounder.
  - Hard: no knee, 2.5 ms attack, quicker release: firm control.
- **IN GAIN / OUT GAIN** (±24 dB). In gain drives the signal into the compressor; out gain is your make-up gain.
- **BYPASS** (click-free) and **RESET** (everything back to default).
- **SIZE**: scales the window (or drag the corner). The chosen size is remembered.

Attack and release are automatic: a fast release for short peaks and a slower one once compression has been
going for a while, so it stays smooth without pumping. Stereo-linked, zero latency.

## Build
GitHub Actions builds the Windows VST3 on every push (Actions tab → latest run → `LineComp-VST3-Windows`).
