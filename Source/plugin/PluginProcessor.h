#pragma once

#include "Engine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

class Crate12Processor : public juce::AudioProcessor
{
public:
    Crate12Processor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Crate12"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    c12::Engine& getEngine() { return engine; }

    // Loads an audio file into a sound slot (resampled to 26.04 kHz / 12-bit by the engine).
    bool loadSampleFile (const juce::File& file, int sound);
    bool exportSoundToWav (int sound, const juce::File& file);

    // DAW automation. Index: 0 master, 1-8 faders, 9 ch1-2 cutoff, 10 ch1-2 resonance, 11 drive, 12 tempo, 13 swing
    enum { kParamMaster = 0, kParamFader1 = 1, kParamCutoff = 9, kParamReso, kParamDrive, kParamTempo, kParamSwing, kNumParams };
    // Called when the UI (or loaded state) changes a value, so the host sees it without re-applying it.
    void pushParam (int index, float plainValue);
    void pushAllParamsFromEngine();

private:
    static BusesProperties makeBuses();
    c12::Engine engine;
    juce::AudioFormatManager formats;
    std::vector<float> inL, inR;
    std::vector<c12::MidiMsg> midiIn, midiOut;
    juce::RangedAudioParameter* params[kNumParams] {};
    float lastSeen[kNumParams] {};
    void applyParamChanges();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Crate12Processor)
};
