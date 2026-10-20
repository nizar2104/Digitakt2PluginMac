#include "Model.h"
#include <unordered_map>

namespace dt2
{
namespace
{
    const std::unordered_map<std::string, int>& paramIndex()
    {
        static std::unordered_map<std::string, int> m = [] {
            std::unordered_map<std::string, int> r;
            for (int i = 0; i < P_COUNT; ++i)
                r[paramName (i)] = i;
            return r;
        }();
        return m;
    }

    float num (const juce::var& v, float def)
    {
        if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool())
            return (float) (double) v;
        return def;
    }

    void readParams (const juce::var& obj, Params& out)
    {
        if (auto* o = obj.getDynamicObject())
            for (auto& prop : o->getProperties())
            {
                auto it = paramIndex().find (prop.name.toString().toStdString());
                if (it != paramIndex().end())
                    out[(size_t) it->second] = num (prop.value, out[(size_t) it->second]);
            }
    }

    void readTrack (const juce::var& tv, Track& t)
    {
        readParams (tv["params"], t.params);
        t.length = juce::jlimit (1, 128, (int) num (tv["length"], 16));
        t.speed = juce::jlimit (0, 6, (int) num (tv["speed"], 4));
        t.level = num (tv["level"], 100);
        if (auto* steps = tv["steps"].getArray())
        {
            for (int i = 0; i < juce::jmin (128, steps->size()); ++i)
            {
                const auto& sv = steps->getReference (i);
                auto& st = t.steps[(size_t) i];
                st.trig = (int) num (sv["trig"], 0);
                st.locks.clear();
                if (auto* lo = sv["locks"].getDynamicObject())
                    for (auto& prop : lo->getProperties())
                    {
                        auto it = paramIndex().find (prop.name.toString().toStdString());
                        if (it != paramIndex().end())
                            st.locks.push_back ({ it->second, num (prop.value, 0) });
                    }
            }
        }
        const auto& ev = tv["euc"];
        if (ev.isObject())
        {
            t.euc.on = num (ev["on"], 0) > 0;
            t.euc.pl1 = (int) num (ev["pl1"], 4);
            t.euc.pl2 = (int) num (ev["pl2"], 0);
            t.euc.ro1 = (int) num (ev["ro1"], 0);
            t.euc.ro2 = (int) num (ev["ro2"], 0);
            t.euc.tro = (int) num (ev["tro"], 0);
            t.euc.op = (int) num (ev["op"], 0);
        }
        if (t.euc.on)
            t.eucHits = euclidPattern (t.euc, t.length);
    }
} // namespace

int paramIndexOf (const juce::String& name)
{
    auto it = paramIndex().find (name.toStdString());
    return it == paramIndex().end() ? -1 : it->second;
}

std::vector<bool> euclidPattern (const Euclid& e, int n)
{
    auto pulses = [n] (int k, int r) {
        std::vector<bool> out ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const int j = (((i - r) % n) + n) % n;
            out[(size_t) i] = k > 0 && (j * k) % n < k;
        }
        return out;
    };
    auto a = pulses (juce::jmin (e.pl1, n), e.ro1);
    auto b = pulses (juce::jmin (e.pl2, n), e.ro2);
    std::vector<bool> c ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const bool x = a[(size_t) i], y = b[(size_t) i];
        if (e.pl2 == 0) c[(size_t) i] = x;
        else if (e.op == 1) c[(size_t) i] = x && y;
        else if (e.op == 2) c[(size_t) i] = x != y;
        else if (e.op == 3) c[(size_t) i] = x && ! y;
        else c[(size_t) i] = x || y;
    }
    std::vector<bool> out ((size_t) n);
    for (int i = 0; i < n; ++i)
        out[(size_t) i] = c[(size_t) ((((i - e.tro) % n) + n) % n)];
    return out;
}

const Pattern& Project::pattern (int idx) const
{
    static const Pattern empty {};
    auto it = patterns.find (idx);
    return it != patterns.end() ? it->second : empty;
}

Params Project::trackParams (int patternIdx, int track) const
{
    const auto& own = pattern (patternIdx).tracks[(size_t) track].params;
    if (! performKit)
        return own;
    Params p = (*performKit)[(size_t) track].params;
    for (int id : { note, vel, len, cond, mtim, rtrg, rtim, lfot })
        p[(size_t) id] = own[(size_t) id];
    return p;
}

float Project::trackLevel (int patternIdx, int track) const
{
    return performKit ? (*performKit)[(size_t) track].level : pattern (patternIdx).tracks[(size_t) track].level;
}

std::shared_ptr<Project> Project::fromJson (const juce::String& json)
{
    auto root = juce::JSON::parse (json);
    if (! root.isObject())
        return nullptr;
    auto pr = std::make_shared<Project>();
    for (int i = 0; i < M_COUNT; ++i)
        pr->master[(size_t) i] = masterDefault (i);

    if (auto* pats = root["patterns"].getArray())
        for (auto& entry : *pats)
            if (auto* pair = entry.getArray(); pair != nullptr && pair->size() == 2)
            {
                const int idx = (int) num (pair->getReference (0), 0);
                const auto& pv = pair->getReference (1);
                Pattern pat;
                pat.masterLength = juce::jlimit (1, 128, (int) num (pv["masterLength"], 16));
                if (auto* tracks = pv["tracks"].getArray())
                    for (int t = 0; t < juce::jmin (16, tracks->size()); ++t)
                        readTrack (tracks->getReference (t), pat.tracks[(size_t) t]);
                pr->patterns[idx] = std::move (pat);
            }

    pr->patternIdx = (int) num (root["patternIdx"], 0);
    pr->queuedPattern = root["queuedPattern"].isVoid() || root["queuedPattern"].isUndefined() ? -1 : (int) num (root["queuedPattern"], -1);
    if (auto* mo = root["master"].getDynamicObject())
        for (int i = 0; i < M_COUNT; ++i)
            if (mo->hasProperty (masterName (i)))
                pr->master[(size_t) i] = num (mo->getProperty (masterName (i)), masterDefault (i));
    pr->bpm = num (root["bpm"], 122);
    pr->swing = num (root["swing"], 50);
    pr->masterVol = num (root["masterVol"], 100);
    if (auto* mu = root["mutes"].getArray())
        for (int i = 0; i < juce::jmin (16, mu->size()); ++i)
            pr->mutes[(size_t) i] = (bool) mu->getReference (i);
    pr->fill = (bool) root["fill"];
    pr->songMode = (bool) root["songMode"];
    pr->songIdx = juce::jlimit (0, 15, (int) num (root["songIdx"], 0));
    if (auto* songs = root["songs"].getArray())
        for (int s = 0; s < juce::jmin (16, songs->size()); ++s)
            if (auto* rows = songs->getReference (s).getArray())
                for (auto& r : *rows)
                    pr->songs[(size_t) s].push_back ({ (int) num (r["ptn"], 0), juce::jmax (1, (int) num (r["rep"], 1)),
                                                       (int) num (r["len"], 0), (int) num (r["bpm"], 0) });
    if (auto* pk = root["performKit"].getArray(); pk != nullptr && pk->size() == 16)
    {
        std::array<KitSlot, 16> kit;
        for (int i = 0; i < 16; ++i)
        {
            readParams (pk->getReference (i)["params"], kit[(size_t) i].params);
            kit[(size_t) i].level = num (pk->getReference (i)["level"], 100);
        }
        pr->performKit = kit;
    }
    return pr;
}
} // namespace dt2
