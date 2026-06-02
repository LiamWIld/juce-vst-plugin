# fourtwofour

fourtwofour is a JUCE/C++ audio plugin by wildli, inspired by the input/preamp section of the TASCAM Portastudio 424 MkI.

This is not a full cassette or tape machine emulation right now. The current focus is narrower: preamp gain behavior, color, breakup, and gain staging.

## Current Status

This is an early alpha. It is being shaped into a first tester build, not presented as a finished commercial product.

Current targets:

- Windows VST3
- macOS AU
- Standalone

Current controls:

- Gain
- Output
- Auto Level linking between Gain and Output

The preamp transfer curve is an embedded 2048-point LUT generated from LTspice. The plugin does not need an external LUT or CSV file at runtime.

## Signal Path

Input -> AC-coupled input behavior -> U101A transfer LUT -> Output trim

## Why I Made This

I wanted a practical way to turn a small piece of the 424 MkI schematic into something I could actually play through.

The workflow is simple: look at the schematic, simulate the U101A input/preamp behavior in LTspice, export the transfer curve as a LUT, then load that LUT inside a JUCE plugin. That keeps the plugin focused on the part I care about first: how the input gain stage reacts as it moves from clean color into breakup.

It is not trying to model every part of the machine yet. No tape transport, no cassette noise model, no full mixer channel path, and no complete EQ section at this stage.

## Still Planned

- Custom UI
- Factory presets
- Input/output meters
- Tester packages for Windows and macOS
- More testing in real hosts
- Possible EQ
- Maybe more of the 424 signal path later

## Note

fourtwofour is inspired by the TASCAM 424 MkI. It is not official, affiliated with, or endorsed by TASCAM.
