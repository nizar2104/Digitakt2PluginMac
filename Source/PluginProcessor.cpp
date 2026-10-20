#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr juce::int32 kMagic = 0x44543253; // 'DT2S'
} // namespace

Digitakt2Processor::Digitakt2Processor() : AudioProcessor (makeBuses())
{
    factory = dt2::makeFactoryPool();
    publishPool();
}

void Digitakt2Processor::publishPool()
{
    auto p = std::make_shared<dt2::Pool> (factory);
    if (pool)
        for (size_t i = dt2::FACTORY_COUNT; i < pool->size(); ++i)
            p->push_back ((*pool)[i]);
    pool = p;
    engine.setPool (pool);
}

bool Digitakt2Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
}

void Digitakt2Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    blockSize = juce::jmax (64, samplesPerBlock);
    engine.prepare (sampleRate, blockSize);
    scratch.setSize (34, blockSize);
}

void Digitakt2Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();

    dt2::HostInfo host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.hostPlaying = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) host.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { host.ppq = *ppq; host.hasPpq = true; }
        }

    juce::MidiBuffer chunkMidi, outMidi;
    for (int start = 0; start < total; start += blockSize)
    {
        const int n = juce::jmin (blockSize, total - start);
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), 34, n);

        chunkMidi.clear();
        for (const auto meta : midi)
            if (meta.samplePosition >= start && meta.samplePosition < start + n)
                chunkMidi.addEvent (meta.getMessage(), meta.samplePosition - start);

        dt2::HostInfo h = host;
        if (h.hasPpq && h.bpm > 0)
            h.ppq += (double) start / getSampleRate() * h.bpm / 60.0;

        engine.process (view, 16, chunkMidi, h);

        for (const auto meta : chunkMidi)
            outMidi.addEvent (meta.getMessage(), meta.samplePosition + start);

        // main + enabled track outputs
        for (int busIdx = 0; busIdx < getBusCount (false); ++busIdx)
        {
            auto* bus = getBus (false, busIdx);
            if (bus == nullptr || ! bus->isEnabled()) continue;
            auto out = getBusBuffer (buffer, false, busIdx);
            for (int c = 0; c < juce::jmin (2, out.getNumChannels()); ++c)
                out.copyFrom (c, start, scratch, busIdx * 2 + c, 0, n);
        }
    }
    midi.swapWith (outMidi);
}

// ---------------------------------------------------------------- panel ↔ engine

void Digitakt2Processor::setStateJson (const juce::String& json)
{
    if (auto pr = dt2::Project::fromJson (json))
    {
        stateJson = json;
        engine.setProject (pr);
    }
}

void Digitakt2Processor::addUserSample (const juce::String& id, const juce::String& name, std::vector<float> L, std::vector<float> R)
{
    for (size_t i = dt2::FACTORY_COUNT; pool && i < pool->size(); ++i)
        if ((*pool)[i]->id == id) return;
    auto s = std::make_shared<dt2::Sample>();
    s->id = id;
    s->name = name;
    s->L = std::move (L);
    s->R = std::move (R);
    auto p = std::make_shared<dt2::Pool> (*pool);
    p->push_back (s);
    pool = p;
    engine.setPool (pool);
}

std::vector<dt2::SamplePtr> Digitakt2Processor::getUserSamples() const
{
    std::vector<dt2::SamplePtr> out;
    for (size_t i = dt2::FACTORY_COUNT; pool && i < pool->size(); ++i)
        out.push_back ((*pool)[i]);
    return out;
}

// ---------------------------------------------------------------- DAW project state

void Digitakt2Processor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream os (destData, false);
    os.writeInt (kMagic);
    os.writeInt (1);
    os.writeString (stateJson);
    const auto user = getUserSamples();
    os.writeInt ((int) user.size());
    for (const auto& s : user)
    {
        os.writeString (s->id);
        os.writeString (s->name);
        os.writeInt (s->size());
        for (const auto* ch : { &s->L, &s->R })
            for (float v : *ch)
                os.writeShort ((short) juce::jlimit (-32768, 32767, (int) std::round (v * 32767.0f)));
    }
}

void Digitakt2Processor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream is (data, (size_t) sizeInBytes, false);
    if (is.readInt() != kMagic || is.readInt() != 1) return;
    const auto json = is.readString();
    const int count = is.readInt();
    factory = factory.empty() ? dt2::makeFactoryPool() : factory;
    pool = std::make_shared<dt2::Pool> (factory);
    for (int i = 0; i < count && ! is.isExhausted(); ++i)
    {
        const auto id = is.readString();
        const auto name = is.readString();
        const int frames = is.readInt();
        std::vector<float> L ((size_t) frames), R ((size_t) frames);
        for (auto* ch : { &L, &R })
            for (auto& v : *ch)
                v = (float) is.readShort() / 32767.0f;
        addUserSample (id, name, std::move (L), std::move (R));
    }
    engine.setPool (pool);
    setStateJson (json);
    stateVersion++;
}

juce::AudioProcessorEditor* Digitakt2Processor::createEditor()
{
    return new Digitakt2Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Digitakt2Processor();
}
