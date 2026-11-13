# Crate12

A 12-bit sampling drum machine plugin (VST3 + standalone) modelled on the SP-1200 workflow. See `docs/FEATURES.md` for the feature list.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release            # fetches JUCE 8.0.4
# or: cmake -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
ctest --test-dir build                                # engine tests
```

macOS builds target macOS 11+ universal (Intel + Apple Silicon). Windows builds use the static runtime.

Layout: `Source/engine` is plain C++ (sampler, analog stage, sequencer, song mode, sync, MIDI, disk format) with tests in `tests/`. `Source/plugin` is the JUCE wrapper and UI.
