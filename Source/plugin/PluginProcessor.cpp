#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace c12;

juce::AudioProcessor::BusesProperties Crate12Processor::makeBuses()
{
    auto b = BusesProperties()
                 .withInput ("Sample In", juce::AudioChannelSet::stereo(), true)
                 .withOutput ("Mix", juce::AudioChannelSet::stereo(), true);
    for (int i = 1; i <= kNumChannels; ++i)
        b = b.withOutput ("Ch " + juce::String (i), juce::AudioChannelSet::mono(), false);
    return b;
}

Crate12Processor::Crate12Processor() : AudioProcessor (makeBuses())
{
    formats.registerBasicFormats();
    engine.loadFactoryKit();   // replaced by saved state when a project is reopened

    using P = juce::AudioParameterFloat;
    auto add = [this] (int idx, juce::RangedAudioParameter* prm) { params[idx] = prm; addParameter (prm); };
    add (kParamMaster, new P (juce::ParameterID { "master", 1 }, "Master Volume", 0.0f, 1.0f, 0.8f));
    for (int i = 0; i < 8; ++i)
        add (kParamFader1 + i, new P (juce::ParameterID { "fader" + juce::String (i + 1), 1 }, "Fader " + juce::String (i + 1), 0.0f, 1.0f, 0.8f));
    add (kParamCutoff, new P (juce::ParameterID { "cutoff12", 1 }, "Ch 1-2 Filter Cutoff", 0.0f, 1.0f, 0.7f));
    add (kParamReso, new P (juce::ParameterID { "reso12", 1 }, "Ch 1-2 Filter Resonance", 0.0f, 1.0f, 0.15f));
    add (kParamDrive, new P (juce::ParameterID { "drive", 1 }, "Output Drive", 0.1f, 5.0f, 1.0f));
    add (kParamTempo, new P (juce::ParameterID { "tempo", 1 }, "Tempo (internal sync)", juce::NormalisableRange<float> ((float) kMinTempo, (float) kMaxTempo, 0.1f), 90.0f));
    add (kParamSwing, new juce::AudioParameterChoice (juce::ParameterID { "swing", 1 }, "Swing", { "50%", "54%", "58%", "63%", "67%", "71%" }, 0));
    pushAllParamsFromEngine();
}

void Crate12Processor::pushParam (int index, float v)
{
    if (index < 0 || index >= kNumParams || params[index] == nullptr) return;
    lastSeen[index] = v;
    const float norm = params[index]->convertTo0to1 (v);
    if (std::abs (params[index]->getValue() - norm) > 1.0e-6f)
        params[index]->setValueNotifyingHost (norm);
}

void Crate12Processor::pushAllParamsFromEngine()
{
    Globals g;
    { std::lock_guard<std::recursive_mutex> l (engine.mutex()); g = engine.project().globals; }
    pushParam (kParamMaster, g.masterLevel);
    for (int i = 0; i < 8; ++i) pushParam (kParamFader1 + i, engine.getSlider (i));
    pushParam (kParamCutoff, g.filter12Cutoff);
    pushParam (kParamReso, g.filter12Reso);
    pushParam (kParamDrive, g.outputDrive);
    pushParam (kParamTempo, (float) g.tempo);
    pushParam (kParamSwing, (float) g.swingIndex);
}

void Crate12Processor::applyParamChanges()
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (params[i] == nullptr) continue;
        const float v = params[i]->convertFrom0to1 (params[i]->getValue());
        if (std::abs (v - lastSeen[i]) < 1.0e-5f) continue;
        lastSeen[i] = v;
        if (i >= kParamFader1 && i < kParamFader1 + 8) { engine.setSlider (i - kParamFader1, v); continue; }
        std::lock_guard<std::recursive_mutex> l (engine.mutex());
        auto& g = engine.project().globals;
        switch (i)
        {
            case kParamMaster: g.masterLevel = v; break;
            case kParamCutoff: g.filter12Cutoff = v; break;
            case kParamReso:   g.filter12Reso = v; break;
            case kParamDrive:  g.outputDrive = v; break;
            case kParamTempo:  g.tempo = std::round (v * 10.0f) / 10.0; break;
            case kParamSwing:  g.swingIndex = juce::jlimit (0, 5, (int) std::lround (v)); break;
            default: break;
        }
    }
}

bool Crate12Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto main = layouts.getMainOutputChannelSet();
    if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto s = layouts.outputBuses[i];
        if (! s.isDisabled() && s != juce::AudioChannelSet::mono()) return false;
    }
    if (! layouts.inputBuses.isEmpty())
    {
        const auto in = layouts.getMainInputChannelSet();
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void Crate12Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    inL.assign ((size_t) samplesPerBlock, 0.0f);
    inR.assign ((size_t) samplesPerBlock, 0.0f);
    midiIn.reserve (1024);
    midiOut.reserve (2048);
}

void Crate12Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if ((int) inL.size() < n) { inL.resize ((size_t) n); inR.resize ((size_t) n); }

    // copy the sampling input before outputs overwrite the shared buffer
    const float* inLp = nullptr; const float* inRp = nullptr;
    if (getBusCount (true) > 0 && getBus (true, 0)->isEnabled())
    {
        auto inBus = getBusBuffer (buffer, true, 0);
        if (inBus.getNumChannels() > 0)
        {
            std::copy (inBus.getReadPointer (0), inBus.getReadPointer (0) + n, inL.begin());
            const int rc = inBus.getNumChannels() > 1 ? 1 : 0;
            std::copy (inBus.getReadPointer (rc), inBus.getReadPointer (rc) + n, inR.begin());
            inLp = inL.data(); inRp = inR.data();
        }
    }
    buffer.clear();

    applyParamChanges();

    midiIn.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isSysEx()) { engine.handleSysex (m.getRawData(), (size_t) m.getRawDataSize()); continue; }   // SDS
        if (m.getRawDataSize() > 3 || m.getRawDataSize() < 1) continue;
        MidiMsg mm; mm.offset = meta.samplePosition; mm.size = (uint8_t) m.getRawDataSize();
        for (int i = 0; i < mm.size; ++i) mm.bytes[i] = m.getRawData()[i];
        midiIn.push_back (mm);
    }
    midi.clear();

    HostTransport host;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            host.valid = ! juce::JUCEApplicationBase::isStandaloneApp();
            host.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) host.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) host.ppqPosition = *ppq;
            else host.valid = false;
        }
    }

    auto mainBus = getBusBuffer (buffer, false, 0);
    float* mainL = mainBus.getNumChannels() > 0 ? mainBus.getWritePointer (0) : nullptr;
    float* mainR = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer (1) : nullptr;
    std::vector<float> monoTmp;
    if (mainR == nullptr) { monoTmp.assign ((size_t) n, 0.0f); mainR = monoTmp.data(); }

    float* chOut[kNumChannels] {};
    for (int c = 0; c < kNumChannels; ++c)
    {
        const int busIndex = c + 1;
        if (busIndex < getBusCount (false) && getBus (false, busIndex)->isEnabled())
        {
            auto b = getBusBuffer (buffer, false, busIndex);
            if (b.getNumChannels() > 0) chOut[c] = b.getWritePointer (0);
        }
    }

    midiOut.clear();
    engine.process (inLp, inRp, mainL, mainR, chOut, n, midiIn, midiOut, host);

    if (mainBus.getNumChannels() == 1)   // mono main: sum
        for (int i = 0; i < n; ++i) mainL[i] = 0.5f * (mainL[i] + mainR[i]);

    for (auto& m : midiOut)
        midi.addEvent (juce::MidiMessage (m.bytes, m.size, 0.0), juce::jlimit (0, n - 1, m.offset));

    // MIDI Sample Dump packets, throttled so receivers keep up (about 4 packets per block)
    for (auto& sx : engine.takeSysexOut (4))
        midi.addEvent (juce::MidiMessage (sx.data(), (int) sx.size(), 0.0), n - 1);
}

juce::AudioProcessorEditor* Crate12Processor::createEditor() { return new Crate12Editor (*this); }

void Crate12Processor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto blob = engine.saveDisk (kDiskAll);
    dest.append (blob.data(), blob.size());
}

void Crate12Processor::setStateInformation (const void* data, int size)
{
    if (data != nullptr && size > 0)
    {
        engine.loadDisk (static_cast<const uint8_t*> (data), (size_t) size, kDiskAll);
        pushAllParamsFromEngine();
    }
}

bool Crate12Processor::loadSampleFile (const juce::File& file, int sound)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return false;
    const auto maxSamples = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, maxSamples);
    reader->read (&buf, 0, maxSamples, 0, true, true);
    std::vector<float> mono ((size_t) maxSamples, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < maxSamples; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
    return engine.loadSound (sound, mono.data(), maxSamples, reader->sampleRate,
                             file.getFileNameWithoutExtension().toUpperCase().substring (0, 8).toStdString());
}

bool Crate12Processor::exportSoundToWav (int sound, const juce::File& file)
{
    std::vector<int16_t> data;
    {
        std::lock_guard<std::recursive_mutex> l (engine.mutex());
        data = engine.project().sounds[(size_t) sound].data;
    }
    if (data.empty()) return false;
    juce::AudioBuffer<float> buf (1, (int) data.size());
    for (int i = 0; i < (int) data.size(); ++i) buf.setSample (0, i, (float) data[(size_t) i] / 2048.0f);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), kNativeRate, 1, 16, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Crate12Processor(); }
