#include "Voice.h"

namespace dt2
{
namespace
{
    constexpr int CTRL = 16; // control-rate interval (samples)

    float lfoHz (float spd, int mul, double bpm)
    {
        const float cyclesPerBar = std::abs (spd) * (float) LFO_MULT[std::clamp (mul, 0, 11)] / 128.0f;
        return std::max (0.001f, cyclesPerBar * (float) bpm / 240.0f) * (spd < 0 ? -1.0f : 1.0f);
    }

    float waveAt (int wave, double u)
    {
        const double f = u - std::floor (u);
        switch (wave)
        {
            case 0: return (float) (f < 0.25 ? f * 4 : f < 0.75 ? 2 - f * 4 : f * 4 - 4);
            case 1: return (float) std::sin (2 * 3.14159265358979 * f);
            case 2: return f < 0.5 ? 1.0f : -1.0f;
            case 3: return (float) (f < 0.5 ? f * 2 : f * 2 - 2);
            default: return (float) -(f < 0.5 ? f * 2 : f * 2 - 2);
        }
    }
} // namespace

inline void Voice::readFrame (double x, float& l, float& r) const
{
    if (x < 0 || x >= nFrames - 1) { l = r = 0; return; }
    double fx = rev ? (double) (nFrames - 1) - x : x;
    int i0 = (int) fx;
    i0 = std::clamp (i0, 0, nFrames - 2);
    const float fr = (float) (fx - i0);
    const auto& L = sample->L;
    const auto& R = sample->R;
    l = L[(size_t) i0] + (L[(size_t) i0 + 1] - L[(size_t) i0]) * fr;
    r = R[(size_t) i0] + (R[(size_t) i0 + 1] - R[(size_t) i0]) * fr;
}

void Voice::start (const Start& s)
{
    p = *s.p;
    sample = s.sample;
    sr = s.hostRate;
    freePhase = s.freeLfoPhase;
    active = sample != nullptr && sample->size() > 2;
    if (! active) return;
    nFrames = sample->size();
    const double ratio = SAMPLE_RATE / sr;
    const double dur = (double) nFrames;

    rev = p[play] == 1 || p[play] == 3;
    const int srcm = (int) p[P::srcm];
    loop = p[play] >= 2 && srcm != 2;
    stretch = srcm == 2;
    const float semis = p[tune] + p[note] + s.noteOffset;
    pitch = std::pow (2.0, semis / 12.0);

    if (srcm == 3 || srcm == 4)
    {
        const int n = SLICE_COUNTS[std::clamp ((int) p[slcs], 0, 5)];
        const int sl = srcm == 3 ? ((int) p[slic]) % n : ((s.stepIdx + (int) p[gofs]) % n + n) % n;
        const double a = (double) sl / n * dur;
        const double b = a + dur / n * std::max (0.02, p[slen] / 127.0);
        s0 = rev ? dur - b : a;
        s1 = rev ? dur - a : b;
    }
    else
    {
        s0 = p[strt] / 128.0 * dur;
        s1 = s0 + std::max (0.002 * SAMPLE_RATE, (dur - s0) * (p[slen] / 127.0));
    }
    s1 = std::min (s1, dur - 1);
    regionLen = std::max (1.0, s1 - s0);
    const double fitSec = p[fit] * s.stepSeconds;
    rate = (srcm == 1 ? (regionLen / SAMPLE_RATE / fitSec) * pitch : pitch) * ratio;
    loopStart = s0 + regionLen * (p[P::loop] / 128.0);
    pos = s0;

    if (stretch)
    {
        grainLen = std::max (16, (int) (0.09 * sr));
        hop = grainLen / 2;
        outLen = (int) (std::min (fitSec, 30.0) * sr);
        outCount = 0;
        nextGrain = 0;
        grainIdx = 0;
        grains[0] = grains[1] = {};
    }

    // bit reduction / sample rate reduction
    const float bits = p[br] == 0 ? 0 : 16 - p[br] / 127.0f * 14.5f;
    brQ = bits > 0 ? std::pow (2.0f, bits - 1) : 0;
    srrHold = p[srr] == 0 ? 1 : std::max (1, (int) std::round (std::pow (48.0f, p[srr] / 127.0f)));
    srrCount = 0;

    // filter machine
    fltm = (int) p[P::fltm];
    f1.reset(); f2.reset(); f3.reset();
    f1.set (Biquad::Type::bypass, (float) sr, 1000, 1);
    f2.set (Biquad::Type::bypass, (float) sr, 1000, 1);
    f3.set (Biquad::Type::bypass, (float) sr, 1000, 1);
    baseF = cutoffOf (p[freq]);
    q = qOf (p[reso]);
    envOct = p[fenv] / 64.0f * (fltm == 3 || fltm == 4 ? 3.0f : 7.0f);
    fEnv = 0; fStage = 0;
    fA = timeOf (p[fatk]); fD = timeOf (p[fdec]); fS = p[fsus] / 127.0f; fR = timeOf (p[frel]);
    if (fltm == 2)
    {
        f1.set (Biquad::Type::lowshelf, (float) sr, 180, 0.7f, p[eql] / 64.0f * 18.0f);
        f2.set (Biquad::Type::peaking, (float) sr, cutoffOf (p[eqmf]), 0.3f + p[eqmq] / 127.0f * 8.0f, p[eqmg] / 64.0f * 18.0f);
        f3.set (Biquad::Type::highshelf, (float) sr, 5000, 0.7f, p[eqh] / 64.0f * 18.0f);
    }
    if (fltm == 3 || fltm == 4)
    {
        comb.clear(); // allocated in prepare()
        combBaseF = 30.0f + std::pow (p[freq] / 127.0f, 2.0f) * 4000.0f;
        combFb = (fltm == 3 ? -1.0f : 1.0f) * p[reso] / 127.0f * 0.97f;
        const float dampHz = cutoffOf (127.0f - p[cdmp] * 0.8f);
        combLpCoef = std::exp (-2.0f * kPi * dampHz / (float) sr);
        combLpL = combLpR = 0;
    }

    // amp envelope
    const float lenSteps = LEN_TABLE[std::clamp ((int) p[len], 0, LEN_COUNT - 1)];
    gateSamples = lenSteps < 0 ? -1 : (long long) (lenSteps * s.stepSeconds * sr);
    const float v = (s.velocity >= 0 ? (float) s.velocity : p[vel]) / 127.0f;
    peakA = v * v;
    aA = std::max (0.0015f, p[aatk] == 0 ? 0.0f : timeOf (p[aatk]));
    holdUntilGate = p[ahld] >= 127;
    holdLeft = holdUntilGate ? 0 : (p[ahld] == 0 ? 0 : (int) (timeOf (p[ahld]) * 0.5f * sr));
    decayInf = p[adec] >= 127;
    decT = timeOf (p[adec]);
    susL = peakA * p[asus] / 127.0f;
    relT = timeOf (p[arel]);
    amp = Amp::attack;
    ampV = 0;
    age = 0;
    fade = 1;
    fading = false;
    ovr = p[P::ovr];

    volT = vol = volGain (p[P::vol]) * volGain (p[lev]) * 1.4f;
    panT = pan = std::clamp (p[P::pan] / 64.0f, -1.0f, 1.0f);
    sDelT = sDel = std::pow (p[del] / 127.0f, 1.5f);
    sRevT = sRev = std::pow (p[P::rev] / 127.0f, 1.5f);
    sChrT = sChr = std::pow (p[chr] / 127.0f, 1.5f);

    // LFOs
    static const int pre[3][8] = {
        { lspd, lmul, lfad, ldst, lwav, lsph, lmod, ldep },
        { l2spd, l2mul, l2fad, l2dst, l2wav, l2sph, l2mod, l2dep },
        { l3spd, l3mul, l3fad, l3dst, l3wav, l3sph, l3mod, l3dep },
    };
    for (int i = 0; i < 3; ++i)
    {
        auto& l = lfos[i];
        const int* id = pre[i];
        l.idx = i;
        l.dst = (int) p[(size_t) id[3]];
        l.depth = p[(size_t) id[7]] / 64.0f;
        l.hz = lfoHz (p[(size_t) id[0]], (int) p[(size_t) id[1]], s.bpm);
        l.wave = (int) p[(size_t) id[4]];
        l.sph = p[(size_t) id[5]] / 128.0f;
        l.mode = (int) p[(size_t) id[6]];
        l.free = l.mode == 0 || p[lfot] == 0;
        l.phase = 0;
        l.stopped = false;
        const float fad = p[(size_t) id[2]];
        l.fadeDir = fad > 0 ? 1 : fad < 0 ? -1 : 0;
        l.fadeT = l.fadeDir != 0 ? timeOf (std::abs (fad) * 2 - 1) : 0;
    }
    ctrlCounter = 0;
    updateControl();
}

void Voice::setFilter (float cutoffHz, float qv)
{
    const float fs = (float) sr;
    switch (fltm)
    {
        case 0:
        {
            const int t = (int) p[ftyp]; // LP2 LP4 BP2 HP2 HP4 BR2 PK2
            using T = Biquad::Type;
            const T t1 = t <= 1 ? T::lowpass : t == 2 ? T::bandpass : t <= 4 ? T::highpass : t == 5 ? T::notch : T::peaking;
            if (t1 == T::peaking) f1.set (t1, fs, cutoffHz, 1.0f, p[reso] / 127.0f * 18.0f);
            else f1.set (t1, fs, cutoffHz, qv);
            if (t == 1) f2.set (T::lowpass, fs, cutoffHz, qv * 0.5f);
            else if (t == 4) f2.set (T::highpass, fs, cutoffHz, qv * 0.5f);
            else f2.set (T::bypass, fs, cutoffHz, 1);
            break;
        }
        case 1:
            f1.set (Biquad::Type::lowpass, fs, cutoffHz, 0.54f);
            f2.set (Biquad::Type::lowpass, fs, cutoffHz, qv * 1.3f);
            break;
        case 5:
        {
            const int t = (int) p[lgty];
            const auto ty = t == 0 ? Biquad::Type::lowpass : t == 1 ? Biquad::Type::bandpass : Biquad::Type::highpass;
            f1.set (ty, fs, cutoffHz, 0.7f + std::pow (p[reso] / 127.0f, 1.5f) * 14.0f + (qv - q));
            break;
        }
        default: break; // EQ is static, comb handled in render
    }
}

float Voice::lfoValue (Lfo& l, int sampleInBlock)
{
    if (l.stopped) return 0;
    double u;
    if (l.free && freePhase != nullptr)
        u = freePhase[l.idx] + l.hz * (double) sampleInBlock / sr;
    else
    {
        u = l.phase;
        const double cycles = std::abs (l.phase);
        if ((l.mode == 2 && cycles >= 1.0) || (l.mode == 3 && cycles >= 0.5)) { l.stopped = true; return 0; }
    }
    float d = l.depth;
    if (l.fadeDir != 0 && l.fadeT > 0)
    {
        const float t = (float) age / (float) sr;
        const float k = std::min (1.0f, t / l.fadeT);
        d *= l.fadeDir > 0 ? 1 - k : k;
    }
    return waveAt (l.wave, u + l.sph) * d;
}

void Voice::updateControl()
{
    modPitch = modFreq = modReso = modVol = modPan = modDel = modRev = 0;
    for (auto& l : lfos)
    {
        if (l.dst == 0 || l.depth == 0) continue;
        const float v = lfoValue (l, 0);
        switch (l.dst)
        {
            case 1: modPitch += v * 1200; break;
            case 2: modFreq += v * 4; break;
            case 3: modReso += v * 12; break;
            case 4: modVol += v; break;
            case 5: modPan += v; break;
            case 6: modDel += v * 0.8f; break;
            default: modRev += v * 0.8f; break;
        }
    }
    if (fltm == 0 || fltm == 1 || fltm == 5)
    {
        const float fc = std::clamp (baseF * std::pow (2.0f, envOct * fEnv + modFreq), 20.0f, 20000.0f);
        setFilter (fc, std::max (0.1f, q + modReso));
    }
}

void Voice::fadeOut()
{
    if (! active) return;
    fading = true;
}

void Voice::releaseNow()
{
    if (! active) return;
    amp = Amp::release;
    relT = std::min (relT, 0.32f);
}

void Voice::applyLock (const Params& lp)
{
    if (! active) return;
    baseF = cutoffOf (lp[freq]);
    q = qOf (lp[reso]);
    volT = volGain (lp[P::vol]) * volGain (lp[lev]) * 1.4f;
    panT = std::clamp (lp[P::pan] / 64.0f, -1.0f, 1.0f);
    sDelT = std::pow (lp[del] / 127.0f, 1.5f);
    sRevT = std::pow (lp[P::rev] / 127.0f, 1.5f);
}

void Voice::render (const VoiceBuses& b, int offset, int numSamples)
{
    if (! active) return;
    const float fs = (float) sr;
    const float smooth = 1.0f - std::exp (-1.0f / (0.01f * fs));
    const float fadeStep = 1.0f / (0.004f * fs);

    for (int i = offset; i < offset + numSamples; ++i)
    {
        if (++ctrlCounter >= CTRL)
        {
            ctrlCounter = 0;
            for (auto& l : lfos)
                if (! l.free) l.phase += l.hz * CTRL / sr;
            // filter envelope
            if (envOct != 0)
            {
                const float dt = CTRL / fs;
                if (fStage == 0) { fEnv += fA > 0 ? dt / fA : 1; if (fEnv >= 1) { fEnv = 1; fStage = 1; } }
                else if (fStage == 1) fEnv = approach (fEnv, fS, fD / 4, fs / CTRL);
                else fEnv = approach (fEnv, 0, fR / 4, fs / CTRL);
                if (fStage < 2 && gateSamples >= 0 && age >= gateSamples) fStage = 2;
            }
            updateControl();
        }

        // ---- source ----
        float l = 0, r = 0;
        bool srcDone = false;
        const double pr = rate * std::pow (2.0, modPitch / 1200.0);
        if (stretch)
        {
            if (outCount >= outLen + grainLen) srcDone = true;
            else
            {
                if (outCount >= nextGrain && outCount < outLen)
                {
                    auto& g = grains[grainIdx];
                    g.on = true;
                    g.age = 0;
                    g.pos = s0 + (double) outCount / std::max (1, outLen) * regionLen;
                    grainIdx ^= 1;
                    nextGrain += hop;
                }
                for (auto& g : grains)
                {
                    if (! g.on) continue;
                    float gl, gr;
                    readFrame (g.pos, gl, gr);
                    const float w = g.age < grainLen / 2 ? (float) g.age / (grainLen / 2.0f) : (float) (grainLen - g.age) / (grainLen / 2.0f);
                    l += gl * w;
                    r += gr * w;
                    g.pos += pr;
                    if (++g.age >= grainLen) g.on = false;
                }
                ++outCount;
            }
        }
        else
        {
            if (pos >= s1)
            {
                if (loop) pos = loopStart + std::fmod (pos - s1, std::max (1.0, s1 - loopStart));
                else srcDone = true;
            }
            if (! srcDone)
            {
                readFrame (pos, l, r);
                pos += pr;
            }
        }
        if (srcDone && ! fading) { fading = true; fade = std::min (fade, 1.0f); }

        // bit + sample-rate reduction
        if (srrHold > 1)
        {
            if (srrCount == 0) { heldL = l; heldR = r; }
            l = heldL; r = heldR;
            if (++srrCount >= srrHold) srrCount = 0;
        }
        if (brQ > 0) { l = std::round (l * brQ) / brQ; r = std::round (r * brQ) / brQ; }

        // ---- filter machine ----
        if (fltm == 3 || fltm == 4)
        {
            const float cf = std::clamp (combBaseF * std::pow (2.0f, envOct * fEnv), 20.0f, 8000.0f);
            const float d = fs / cf;
            const float dl = comb.readL (d), dr = comb.readR (d);
            combLpL = dl + (combLpL - dl) * combLpCoef;
            combLpR = dr + (combLpR - dr) * combLpCoef;
            comb.push (l + combLpL * combFb, r + combLpR * combFb);
            l += combLpL;
            r += combLpR;
        }
        else
        {
            f1.process (l, r);
            f2.process (l, r);
            f3.process (l, r);
        }

        // ---- amp envelope ----
        const bool gateOff = gateSamples >= 0 && age >= gateSamples;
        switch (amp)
        {
            case Amp::attack:
                ampV += peakA / std::max (1.0f, aA * fs);
                if (ampV >= peakA) { ampV = peakA; amp = Amp::hold; }
                break;
            case Amp::hold:
                if (holdUntilGate) { if (gateOff) amp = Amp::release; }
                else if (holdLeft-- <= 0) amp = decayInf ? Amp::sustain : Amp::decay;
                break;
            case Amp::decay:
                ampV = approach (ampV, susL, decT / 4, fs);
                if (gateOff) amp = Amp::release;
                else if (susL <= 0 && ampV < 1e-4f) amp = Amp::done;
                break;
            case Amp::sustain:
                if (gateOff && ! holdUntilGate) amp = Amp::release;
                break;
            case Amp::release:
                ampV = approach (ampV, 0, relT / 4, fs);
                if (ampV < 1e-4f) amp = Amp::done;
                break;
            case Amp::done: break;
        }
        if (amp == Amp::done) { active = false; return; }
        if (fading) { fade -= fadeStep; if (fade <= 0) { active = false; return; } }

        float g = ampV * fade;
        l = driveShape (l * g, ovr);
        r = driveShape (r * g, ovr);

        vol += (volT - vol) * smooth;
        pan += (panT - pan) * smooth;
        sDel += (sDelT - sDel) * smooth;
        sRev += (sRevT - sRev) * smooth;
        sChr += (sChrT - sChr) * smooth;
        const float vg = vol * (1.0f + modVol);
        l *= vg;
        r *= vg;

        // Web Audio StereoPanner (stereo input) behaviour
        const float pv = std::clamp (pan + modPan, -1.0f, 1.0f);
        float ol, orr;
        if (pv <= 0)
        {
            const float x = (pv + 1) * kPi / 2;
            ol = l + r * std::cos (x);
            orr = r * std::sin (x);
        }
        else
        {
            const float x = pv * kPi / 2;
            ol = l * std::cos (x);
            orr = r + l * std::sin (x);
        }

        b.outL[i] += ol;
        b.outR[i] += orr;
        const float dsend = std::max (0.0f, sDel + modDel), rsend = std::max (0.0f, sRev + modRev);
        b.delL[i] += ol * dsend; b.delR[i] += orr * dsend;
        b.revL[i] += ol * rsend; b.revR[i] += orr * rsend;
        b.chrL[i] += ol * sChr;  b.chrR[i] += orr * sChr;
        ++age;
    }
}
} // namespace dt2
