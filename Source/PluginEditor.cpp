#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
juce::String mimeFor (const juce::String& file)
{
    const auto ext = file.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (ext == "html") return "text/html";
    if (ext == "js" || ext == "mjs") return "text/javascript";
    if (ext == "css") return "text/css";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "png") return "image/png";
    if (ext == "json") return "application/json";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "wasm") return "application/wasm";
    return "application/octet-stream";
}

juce::String pcmToBase64 (const std::vector<float>& x)
{
    std::vector<juce::int16> i16 (x.size());
    for (size_t i = 0; i < x.size(); ++i)
        i16[i] = (juce::int16) juce::jlimit (-32768, 32767, (int) std::round (x[i] * 32767.0f));
    return juce::Base64::toBase64 (i16.data(), i16.size() * sizeof (juce::int16));
}

std::vector<float> base64ToPcm (const juce::String& b64)
{
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, b64)) return {};
    const auto* d = static_cast<const juce::int16*> (mo.getData());
    const size_t n = mo.getDataSize() / sizeof (juce::int16);
    std::vector<float> out (n);
    for (size_t i = 0; i < n; ++i)
        out[i] = (float) d[i] / 32767.0f; // the panel sends little-endian 16-bit PCM (native on Apple silicon / Intel)
    return out;
}
} // namespace

Digitakt2Editor::Digitakt2Editor (Digitakt2Processor& p) : AudioProcessorEditor (p), proc (p)
{
    auto options = juce::WebBrowserComponent::Options {}
                       .withNativeIntegrationEnabled()
                       .withKeepPageLoadedWhenBrowserIsHidden()
                       .withResourceProvider ([] (const juce::String& url) { return getResource (url); })
                       .withEventListener ("dt2", [this] (juce::var v) { onPanelEvent (v); });
    web = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*web);
    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (640, 560, 2400, 2000);
    setSize (1040, 930);
    startTimerHz (30);
}

Digitakt2Editor::~Digitakt2Editor() { stopTimer(); }

void Digitakt2Editor::resized()
{
    if (web) web->setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> Digitakt2Editor::getResource (const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf ("?", false, false);
    auto file = path.fromLastOccurrenceOf ("/", false, false);
    if (file.isEmpty()) file = "index.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (file != BinaryData::originalFilenames[i]) continue;
        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        if (data == nullptr) break;
        juce::WebBrowserComponent::Resource r;
        r.data.resize ((size_t) size);
        std::memcpy (r.data.data(), data, (size_t) size);
        r.mimeType = mimeFor (file);
        return r;
    }
    return std::nullopt;
}

void Digitakt2Editor::onPanelEvent (const juce::var& v)
{
    const auto type = v["type"].toString();
    const auto& payload = v["payload"];

    if (type == "ready")
    {
        pageReady = true;
        sendHello();
    }
    else if (type == "state")
    {
        proc.setStateJson (payload.toString());
    }
    else if (type == "sample")
    {
        proc.addUserSample (payload["id"].toString(), payload["name"].toString(),
                            base64ToPcm (payload["L"].toString()), base64ToPcm (payload["R"].toString()));
    }
    else if (type == "trigger")
    {
        dt2::Command c;
        c.type = dt2::Command::trigger;
        c.track = juce::jlimit (0, 15, (int) payload["track"]);
        c.noteOffset = (float) (double) payload["noteOffset"];
        c.velocity = (int) payload["velocity"];
        if (auto* lo = payload["locks"].getDynamicObject())
            for (auto& prop : lo->getProperties())
            {
                const int id = dt2::paramIndexOf (prop.name.toString());
                if (id >= 0 && c.lockCount < 16)
                    c.locks[c.lockCount++] = { id, (float) (double) prop.value };
            }
        proc.getEngine().pushCommand (c);
    }
    else if (type == "transport")
    {
        dt2::Command c;
        const auto cmd = payload.toString();
        c.type = cmd == "play" ? dt2::Command::play : cmd == "pause" ? dt2::Command::pause : dt2::Command::stop;
        proc.getEngine().pushCommand (c);
    }
}

void Digitakt2Editor::sendHello()
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("state", proc.getStateJson());
    juce::Array<juce::var> samples;
    for (const auto& s : proc.getUserSamples())
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("id", s->id);
        so->setProperty ("name", s->name);
        so->setProperty ("L", pcmToBase64 (s->L));
        so->setProperty ("R", pcmToBase64 (s->R));
        samples.add (juce::var (so));
    }
    obj->setProperty ("samples", samples);
    web->emitEventIfBrowserIsVisible ("dt2_hello", juce::var (obj));
    seenStateVersion = proc.stateVersion.load();
}

void Digitakt2Editor::timerCallback()
{
    if (! pageReady) return;
    if (proc.stateVersion.load() != seenStateVersion) sendHello();

    auto& st = proc.getEngine().status;
    auto* o = new juce::DynamicObject();
    o->setProperty ("playing", st.playing.load());
    o->setProperty ("paused", st.paused.load());
    juce::Array<juce::var> steps, flashes;
    for (int i = 0; i < 16; ++i)
    {
        steps.add (st.steps[i].load());
        flashes.add (st.flashes[i].load());
    }
    o->setProperty ("steps", steps);
    o->setProperty ("flashes", flashes);
    o->setProperty ("patternIdx", st.patternIdx.load());
    o->setProperty ("queued", st.queued.load());
    o->setProperty ("songRow", st.songRow.load());
    o->setProperty ("songRep", st.songRep.load());
    o->setProperty ("bpm", st.bpm.load());
    web->emitEventIfBrowserIsVisible ("dt2_status", juce::var (o));
}
