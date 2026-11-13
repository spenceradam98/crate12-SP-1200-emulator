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

    midiIn.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.getRawDataSize() > 3 || m.getRawDataSize() < 1) continue;   // sysex (SDS) handled later
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
        engine.loadDisk (static_cast<const uint8_t*> (data), (size_t) size, kDiskAll);
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
