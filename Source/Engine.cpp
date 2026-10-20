#include "Engine.h"

namespace dt2
{
Engine::Engine()
{
    events.reserve (4096);
    pendingOffs.reserve (1024);
    lastProgram.fill (-1);
}

void Engine::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = block;
    trackBus.setSize (32, block);
    sendBus.setSize (6, block);
    delay.prepare ((int) (sr * 4.2));
    chorus.prepare ((int) (sr * 0.06));
    for (auto& tv : voices)
        for (auto& v : tv)
            v.prepare (sr);
    reverb.setSampleRate (sr);
    midiIn.ensureSize (4096);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
    comp.prepare (spec);
    comp.setAttack (5.0f);
    comp.setRelease (150.0f);
}

// ---------------------------------------------------------------- hand-over

void Engine::setProject (std::shared_ptr<const Project> p)
{
    const auto now = juce::Time::getMillisecondCounter();
    keepAlive.emplace_back (now, p);
    while (keepAlive.size() > 2 && now - keepAlive.front().first > 3000)
        keepAlive.pop_front();
    const juce::SpinLock::ScopedLockType sl (handLock);
    pendingProject = std::move (p);
}

void Engine::setPool (std::shared_ptr<const Pool> p)
{
    const auto now = juce::Time::getMillisecondCounter();
    keepAlive.emplace_back (now, p);
    const juce::SpinLock::ScopedLockType sl (handLock);
    pendingPool = std::move (p);
}

bool Engine::pushCommand (const Command& c)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 < 1) return false;
    commands[(size_t) (n1 > 0 ? s1 : s2)] = c;
    fifo.finishedWrite (1);
    return true;
}

void Engine::pullShared()
{
    const juce::SpinLock::ScopedTryLockType tl (handLock);
    if (! tl.isLocked()) return;
    if (pendingPool) pool = std::move (pendingPool);
    if (pendingProject)
    {
        const bool songWasOn = songActive;
        project = std::move (pendingProject);
        if (! running && ! paused)
        {
            patternIdx = project->patternIdx;
            bpmNow = project->bpm;
        }
        queued = (project->queuedPattern >= 0 && project->queuedPattern != patternIdx) ? project->queuedPattern : -1;
        songActive = project->songMode && ! project->songs[(size_t) project->songIdx].empty();
        if (songActive && ! songWasOn) { songRow = 0; songRep = 0; }
    }
}

// ---------------------------------------------------------------- sequencer

double Engine::rowLength() const
{
    if (songActive)
    {
        const auto& rows = project->songs[(size_t) project->songIdx];
        if (songRow < (int) rows.size() && rows[(size_t) songRow].len > 0)
            return rows[(size_t) songRow].len;
    }
    return project->pattern (patternIdx).masterLength;
}

void Engine::advanceBoundary (double at16)
{
    bool switched = false;
    if (songActive)
    {
        const auto& rows = project->songs[(size_t) project->songIdx];
        if (! rows.empty())
        {
            if (++songRep >= rows[(size_t) std::min (songRow, (int) rows.size() - 1)].rep)
            {
                songRow = (songRow + 1) % (int) rows.size();
                songRep = 0;
            }
            const auto& r = rows[(size_t) songRow];
            patternIdx = r.ptn;
            if (r.bpm >= 30) bpmNow = r.bpm;
            switched = true;
        }
    }
    else if (queued >= 0)
    {
        patternIdx = queued;
        queued = -1;
        switched = true;
    }
    if (switched || songActive)
    {
        patternStart = at16;
        for (auto& c : condMemo) c.n = -1;
    }
}

Params Engine::resolve (int track, const Step* st) const
{
    Params p = project->trackParams (patternIdx, track);
    if (st != nullptr)
        for (const auto& l : st->locks)
            p[(size_t) l.id] = l.value;
    return p;
}

bool Engine::evalCond (int track, const Params& p, long long n, int length)
{
    auto& memo = condMemo[(size_t) track];
    if (memo.n == n) return memo.result;
    const auto& conds = conditions();
    const auto& c = conds[(size_t) std::clamp ((int) p[cond], 0, (int) conds.size() - 1)];
    const long long loops = n / std::max (1, length);
    bool ok = true;
    bool updatesPre = true;
    switch (c.kind)
    {
        case CondKind::none: ok = true; updatesPre = false; break;
        case CondKind::prob: ok = juce::Random::getSystemRandom().nextFloat() * 100.0f < (float) c.p; break;
        case CondKind::ab: ok = (int) (loops % c.b) == c.a - 1; break;
        case CondKind::fill: ok = project->fill; break;
        case CondKind::nfill: ok = ! project->fill; break;
        case CondKind::pre: ok = lastCond[(size_t) track]; updatesPre = false; break;
        case CondKind::npre: ok = ! lastCond[(size_t) track]; updatesPre = false; break;
        case CondKind::nei: ok = track > 0 && lastCond[(size_t) track - 1]; updatesPre = false; break;
        case CondKind::nnei: ok = track == 0 || ! lastCond[(size_t) track - 1]; updatesPre = false; break;
        case CondKind::first: ok = loops == 0; break;
        case CondKind::nfirst: ok = loops != 0; break;
    }
    if (updatesPre) lastCond[(size_t) track] = ok;
    memo = { n, ok };
    return ok;
}

void Engine::collectEvents (double from16, double to16, int blockOffset, double samplesPer16)
{
    const auto& pat = project->pattern (patternIdx);
    const double blockStart16 = from16 - blockOffset / samplesPer16;
    for (int t = 0; t < 16; ++t)
    {
        const auto& tr = pat.tracks[(size_t) t];
        const double sp = SPEEDS[std::clamp (tr.speed, 0, 6)];
        const int L = std::max (1, tr.length);
        const double ua = (from16 - patternStart) * sp, ub = (to16 - patternStart) * sp;
        const double stepSec = 60.0 / (bpmNow * 4.0) / sp;
        for (long long n = (long long) std::floor (ua) - 1; n <= (long long) std::floor (ub) + 1; ++n)
        {
            if (n < 0) continue;
            const int idx = (int) (n % L);
            const auto& st = tr.steps[(size_t) idx];
            int trig = st.trig;
            if (tr.euc.on && (int) tr.eucHits.size() == L)
                trig = tr.eucHits[(size_t) idx] ? 1 : (st.trig == 2 ? 2 : 0);
            if (trig == 0) continue;
            const Params p = resolve (t, &st);
            const double swingOff = (idx % 2 == 1) ? 2.0 * ((project->swing - 50.0) / 100.0) : 0.0;
            const double tn = (double) n + swingOff + p[mtim] / 24.0;

            auto emit = [&] (double te, int velocity, int kind) {
                if (te < ua || te >= ub) return;
                if (! evalCond (t, p, n, L)) return;
                if (project->mutes[(size_t) t]) return;
                const double at16 = te / sp + patternStart;
                const int off = juce::jlimit (0, maxBlock - 1, (int) ((at16 - blockStart16) * samplesPer16));
                if (events.size() < events.capacity())
                    events.push_back ({ off, t, kind, patternIdx, idx, stepSec, velocity, 0.0f, nullptr });
            };

            if (trig == 2) { emit (tn, -1, 1); continue; }
            if (p[rtrg] > 0)
            {
                const double rate = 4.0 / RTIM_TABLE[std::clamp ((int) p[rtim], 0, 16)];
                const float lenSteps = LEN_TABLE[std::clamp ((int) p[len], 0, LEN_COUNT - 1)];
                const double span = lenSteps > 0 ? lenSteps : 1.0;
                const int count = std::max (1, (int) std::floor (span / rate));
                for (int k = 0; k < count; ++k)
                {
                    const double vf = 1.0 - (double) k / count * 0.6;
                    emit (tn + k * rate, std::max (1, (int) std::round (p[vel] * vf)), 0);
                }
            }
            else
                emit (tn, -1, 0);
        }
        status.steps[t] = (int) (((long long) std::floor (ub - 1e-6) % L + L) % L);
    }
}

// ---------------------------------------------------------------- voices / MIDI

void Engine::fireAudio (int track, const Params& p, int offset, int stepIdx, double stepSec, float noteOffset, int velocity)
{
    if (! pool) return;
    const int smpIdx = (int) p[smp];
    if (smpIdx < 0 || smpIdx >= (int) pool->size()) return;
    auto& cur = curVoice[(size_t) track];
    voices[(size_t) track][(size_t) cur].fadeOut();
    cur ^= 1;
    Voice::Start s { &p, (*pool)[(size_t) smpIdx], sr, stepSec, stepIdx, noteOffset, velocity, freePhase[(size_t) track].data(), bpmNow };
    voices[(size_t) track][(size_t) cur].start (s);
    juce::ignoreUnused (offset);
    status.flashes[track].fetch_add (1);
}

void Engine::fireMidi (int track, const Params& p, int offset, double stepSec, float noteOffset, int velocity, juce::MidiBuffer& midi)
{
    const int ch = ((int) p[mchn] & 15) + 1;
    if (p[mprg] >= 0)
    {
        const int pv = (p[mbnk] >= 0 ? (int) p[mbnk] * 128 : 0) + (int) p[mprg];
        if (lastProgram[(size_t) ch - 1] != pv)
        {
            if (p[mbnk] >= 0) midi.addEvent (juce::MidiMessage::controllerEvent (ch, 0, (int) p[mbnk]), offset);
            midi.addEvent (juce::MidiMessage::programChange (ch, (int) p[mprg]), offset);
            lastProgram[(size_t) ch - 1] = pv;
        }
    }
    for (int n = 0; n < 16; ++n)
    {
        const float v = p[(size_t) (cv1 + n)];
        if (v >= 0) midi.addEvent (juce::MidiMessage::controllerEvent (ch, (int) p[(size_t) (cs1 + n)] & 127, (int) v & 127), offset);
    }
    const int pb = juce::jlimit (0, 16383, 8192 + (int) std::round (p[mpb] / 64.0f * 8192.0f));
    midi.addEvent (juce::MidiMessage::pitchWheel (ch, pb), offset);
    midi.addEvent (juce::MidiMessage::channelPressureChange (ch, (int) p[mat] & 127), offset);
    const int root = 60 + (int) (p[tune] + p[note] + noteOffset);
    const int vel = juce::jlimit (1, 127, velocity >= 0 ? velocity : (int) p[P::vel]);
    const float lenSteps = LEN_TABLE[std::clamp ((int) p[len], 0, LEN_COUNT - 1)];
    const double gate = lenSteps > 0 ? lenSteps * stepSec : stepSec * 4;
    int notes[4] = { root, p[mn2] >= 0 ? root + (int) p[mn2] : -1, p[mn3] >= 0 ? root + (int) p[mn3] : -1, p[mn4] >= 0 ? root + (int) p[mn4] : -1 };
    for (int nt : notes)
    {
        if (nt < 0 || nt > 127) continue;
        midi.addEvent (juce::MidiMessage::noteOn (ch, nt, (juce::uint8) vel), offset);
        if (pendingOffs.size() < pendingOffs.capacity())
            pendingOffs.push_back ({ ch, nt, offset + (long long) (gate * sr) });
    }
    status.flashes[track].fetch_add (1);
}

void Engine::renderVoices (int from, int n)
{
    if (n <= 0) return;
    for (int t = 0; t < 16; ++t)
    {
        VoiceBuses b { trackBus.getWritePointer (t * 2), trackBus.getWritePointer (t * 2 + 1),
                       sendBus.getWritePointer (0), sendBus.getWritePointer (1),
                       sendBus.getWritePointer (2), sendBus.getWritePointer (3),
                       sendBus.getWritePointer (4), sendBus.getWritePointer (5) };
        for (auto& v : voices[(size_t) t])
            v.render (b, from, n);
    }
}

// ---------------------------------------------------------------- master

void Engine::renderMaster (juce::AudioBuffer<float>& out, int numAuxPairs, int n)
{
    const auto& m = project->master;
    auto mv = [&] (M id) { return m[(size_t) id]; };
    float* mainL = out.getWritePointer (0);
    float* mainR = out.getWritePointer (1);

    // track buses → main + aux outputs
    for (int t = 0; t < 16; ++t)
    {
        const float g = project->mutes[(size_t) t] ? 0.0f : volGain (project->trackLevel (patternIdx, t));
        const float* l = trackBus.getReadPointer (t * 2);
        const float* r = trackBus.getReadPointer (t * 2 + 1);
        const bool aux = t < numAuxPairs && out.getNumChannels() >= 4 + t * 2;
        float* al = aux ? out.getWritePointer (2 + t * 2) : nullptr;
        float* ar = aux ? out.getWritePointer (3 + t * 2) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            mainL[i] += l[i] * g;
            mainR[i] += r[i] * g;
            if (aux) { al[i] = l[i] * g; ar[i] = r[i] * g; }
        }
    }

    // delay (ping-pong) return
    const float dTime = (float) std::min (3.9, 60.0 / bpmNow * DEL_TIMES[std::clamp ((int) mv (m_dtim), 0, 10)]) * (float) sr;
    const float fb = mv (m_dfb) / 127.0f * 0.92f;
    delHp.set (Biquad::Type::highpass, (float) sr, 120, 0.7f);
    delLp.set (Biquad::Type::lowpass, (float) sr, cutoffOf (mv (m_dlpf)), 0.7f);
    const float dRet = volGain (mv (m_dvol)) * 0.85f;
    // chorus
    const float chDepth = mv (m_chdp) / 127.0f * 0.006f * (float) sr;
    const float cRet = volGain (mv (m_cvol));
    // reverb
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.35f + mv (m_rdec) / 127.0f * 0.63f;
    rp.damping = 1.0f - mv (m_rton) / 127.0f * 0.9f;
    rp.wetLevel = 1.0f;
    rp.dryLevel = 0.0f;
    rp.width = 1.0f;
    reverb.setParameters (rp);
    revHp.set (Biquad::Type::highpass, (float) sr, 180, 0.7f);
    revTone.set (Biquad::Type::lowpass, (float) sr, cutoffOf (mv (m_rton)), 0.7f);
    const float rRet = volGain (mv (m_rvol)) * 0.95f * 0.5f;

    float* dl = sendBus.getWritePointer (0); float* dr = sendBus.getWritePointer (1);
    float* rl = sendBus.getWritePointer (2); float* rr = sendBus.getWritePointer (3);
    float* cl = sendBus.getWritePointer (4); float* cr = sendBus.getWritePointer (5);

    for (int i = 0; i < n; ++i)
    {
        float x = (dl[i] + dr[i]) * 0.5f, xr = x;
        delHp.process (x, xr);
        const float a = delay.readL (dTime), b = delay.readR (dTime);
        float lpA = a, lpB = a;
        delLp.process (lpA, lpB);
        delay.push (x + fb * b, lpA);
        mainL[i] += a * dRet;
        mainR[i] += b * dRet;

        const float cx = (cl[i] + cr[i]) * 0.5f;
        chorus.push (cx, cx);
        chorusPhase[0] += 0.5 / sr;
        chorusPhase[1] += 0.63 / sr;
        const float d0 = (0.014f * (float) sr) + chDepth * (float) std::sin (2 * 3.14159265358979 * chorusPhase[0]);
        const float d1 = (0.018f * (float) sr) + chDepth * (float) std::sin (2 * 3.14159265358979 * chorusPhase[1]);
        mainL[i] += chorus.readL (d0) * cRet;
        mainR[i] += chorus.readR (d1) * cRet;

        revHp.process (rl[i], rr[i]);
    }
    chorusPhase[0] -= std::floor (chorusPhase[0]);
    chorusPhase[1] -= std::floor (chorusPhase[1]);
    reverb.processStereo (rl, rr, n);
    for (int i = 0; i < n; ++i)
    {
        revTone.process (rl[i], rr[i]);
        mainL[i] += rl[i] * rRet;
        mainR[i] += rr[i] * rRet;
    }

    // internal level → compressor → master overdrive → main volume
    const float iv = volGain (mv (m_ivol)) * 1.15f;
    for (int i = 0; i < n; ++i) { mainL[i] *= iv; mainR[i] *= iv; }
    comp.setThreshold (-40.0f + mv (m_cthr) / 127.0f * 40.0f);
    comp.setRatio (1.0f + mv (m_crat) / 127.0f * 19.0f);
    float* chans[2] = { mainL, mainR };
    juce::dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
    comp.process (juce::dsp::ProcessContextReplacing<float> (blk));
    const float movr = mv (m_movr);
    const float outG = volGain (project->masterVol) * 1.1f;
    for (int i = 0; i < n; ++i)
    {
        mainL[i] = driveShape (mainL[i], movr) * outG;
        mainR[i] = driveShape (mainR[i], movr) * outG;
    }
}

// ---------------------------------------------------------------- process

void Engine::handleCommands (juce::MidiBuffer& midi)
{
    int s1, n1, s2, n2;
    const int ready = fifo.getNumReady();
    fifo.prepareToRead (ready, s1, n1, s2, n2);
    auto run = [&] (const Command& c) {
        switch (c.type)
        {
            case Command::trigger:
                if (events.size() < events.capacity())
                    events.push_back ({ 0, c.track, 2, patternIdx, 0, 60.0 / (bpmNow * 4.0), c.velocity, c.noteOffset, &c });
                break;
            case Command::play:
                if (followingHost) break;
                if (! paused)
                {
                    pos16 = 0;
                    patternStart = 0;
                    for (auto& cm : condMemo) cm.n = -1;
                    if (songActive)
                    {
                        songRow = 0;
                        songRep = 0;
                        const auto& r = project->songs[(size_t) project->songIdx][0];
                        patternIdx = r.ptn;
                        if (r.bpm >= 30) bpmNow = r.bpm;
                    }
                }
                running = true;
                paused = false;
                break;
            case Command::pause:
                if (running && ! followingHost) { running = false; paused = true; }
                break;
            case Command::stop:
            {
                const bool was = running || paused;
                running = false;
                paused = false;
                pos16 = 0;
                if (queued >= 0) { patternIdx = queued; queued = -1; }
                for (auto& tv : voices)
                    for (auto& v : tv)
                        was ? v.releaseNow() : v.fadeOut();
                for (int ch = 1; ch <= 16; ++ch) midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
                pendingOffs.clear();
                for (int t = 0; t < 16; ++t) status.steps[t] = -1;
                break;
            }
        }
    };
    for (int i = 0; i < n1; ++i) run (commands[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) run (commands[(size_t) (s2 + i)]);
    // commands are read later through Event::cmd, so the FIFO slots are released after the block
    lastReadCount = n1 + n2;
}

void Engine::process (juce::AudioBuffer<float>& out, int numAuxPairs, juce::MidiBuffer& midi, const HostInfo& host)
{
    const int n = out.getNumSamples();
    out.clear();
    pullShared();
    if (! project || ! pool || n > maxBlock) { midi.clear(); return; }

    trackBus.clear (0, n);
    sendBus.clear (0, n);
    events.clear();

    // host MIDI input: notes 36–51 play tracks 1–16
    midiIn.clear();
    midiIn.swapWith (midi);
    for (const auto meta : midiIn)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getNoteNumber() >= 36 && msg.getNoteNumber() < 52 && events.size() < events.capacity())
            events.push_back ({ meta.samplePosition, msg.getNoteNumber() - 36, 2, patternIdx, 0, 60.0 / (bpmNow * 4.0), msg.getVelocity(), 0.0f, nullptr });
    }

    // host transport
    const bool hostRun = host.hostPlaying && host.hasPpq;
    if (hostRun && ! wasHostPlaying)
    {
        followingHost = true;
        running = true;
        paused = false;
        patternStart = 0;
        for (auto& c : condMemo) c.n = -1;
        if (songActive) { songRow = 0; songRep = 0; patternIdx = project->songs[(size_t) project->songIdx][0].ptn; }
    }
    if (! hostRun && wasHostPlaying && followingHost)
    {
        followingHost = false;
        running = false;
        for (auto& tv : voices)
            for (auto& v : tv) v.releaseNow();
        for (int ch = 1; ch <= 16; ++ch) midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        pendingOffs.clear();
    }
    wasHostPlaying = hostRun;

    handleCommands (midi);

    if (followingHost && host.bpm > 0) bpmNow = host.bpm;
    else if (! songActive || ! running) bpmNow = project->bpm;
    bpmNow = juce::jlimit (20.0, 400.0, bpmNow);
    const double samplesPer16 = sr * 60.0 / (bpmNow * 4.0);

    if (running)
    {
        if (followingHost)
        {
            pos16 = host.ppq * 4.0;
            if (pos16 < patternStart - 1e-6) patternStart = 0;
        }
        const double from = pos16, to = pos16 + n / samplesPer16;
        double a = from;
        int guard = 0;
        while (a < to && guard++ < 64)
        {
            const double lenRow = std::max (1.0, rowLength());
            const double k = std::floor ((a - patternStart) / lenRow + 1e-9) + 1;
            const double nb = patternStart + k * lenRow;
            const double segEnd = std::min (nb, to);
            collectEvents (a, segEnd, (int) ((a - from) * samplesPer16), samplesPer16);
            if (nb <= to) advanceBoundary (nb);
            a = segEnd;
        }
        pos16 = to;
    }

    // render voices between events (sample accurate)
    // insertion sort by offset (stable, allocation-free)
    for (size_t i = 1; i < events.size(); ++i)
        for (size_t j = i; j > 0 && events[j - 1].offset > events[j].offset; --j)
            std::swap (events[j - 1], events[j]);
    int cursor = 0;
    for (const auto& e : events)
    {
        const int off = juce::jlimit (0, n - 1, e.offset);
        if (off > cursor) { renderVoices (cursor, off - cursor); cursor = off; }
        const auto& tr = project->pattern (e.ptn).tracks[(size_t) e.track];
        const Step* st = e.kind == 2 ? nullptr : &tr.steps[(size_t) e.stepIdx];
        Params p = resolve (e.track, st);
        if (e.cmd != nullptr)
            for (int i = 0; i < e.cmd->lockCount; ++i) p[(size_t) e.cmd->locks[i].id] = e.cmd->locks[i].value;
        if (e.kind == 1)
        {
            auto& tv = voices[(size_t) e.track];
            tv[(size_t) curVoice[(size_t) e.track]].applyLock (p);
            continue;
        }
        if ((int) p[srcm] == MIDI_MACHINE)
            fireMidi (e.track, p, off, e.stepSec, e.noteOffset, e.velocity, midi);
        else
            fireAudio (e.track, p, off, e.stepIdx, e.stepSec, e.noteOffset, e.velocity);
    }
    renderVoices (cursor, n - cursor);
    if (lastReadCount > 0) { fifo.finishedRead (lastReadCount); lastReadCount = 0; }

    // pending MIDI note-offs
    for (size_t i = 0; i < pendingOffs.size();)
    {
        auto& po = pendingOffs[i];
        if (po.samplesLeft < n)
        {
            midi.addEvent (juce::MidiMessage::noteOff (po.ch, po.note), (int) std::max (0LL, po.samplesLeft));
            pendingOffs[i] = pendingOffs.back();
            pendingOffs.pop_back();
        }
        else
        {
            po.samplesLeft -= n;
            ++i;
        }
    }

    // free-running LFO phases
    {
        const auto& pat = project->pattern (patternIdx);
        static const int spdIds[3] = { lspd, l2spd, l3spd };
        static const int mulIds[3] = { lmul, l2mul, l3mul };
        for (int t = 0; t < 16; ++t)
            for (int l = 0; l < 3; ++l)
            {
                const auto& tp = pat.tracks[(size_t) t].params;
                const float spd = tp[(size_t) spdIds[l]];
                const double hz = std::abs (spd) * LFO_MULT[std::clamp ((int) tp[(size_t) mulIds[l]], 0, 11)] / 128.0 * bpmNow / 240.0;
                double& ph = freePhase[(size_t) t][(size_t) l];
                ph += (spd < 0 ? -hz : hz) * n / sr;
                ph -= std::floor (ph);
            }
    }

    renderMaster (out, numAuxPairs, n);

    status.playing = running;
    status.paused = paused;
    status.patternIdx = patternIdx;
    status.queued = queued;
    status.songRow = songRow;
    status.songRep = songRep;
    status.bpm = (float) bpmNow;
    if (! running && ! paused)
        for (int t = 0; t < 16; ++t) status.steps[t] = -1;
}
} // namespace dt2
