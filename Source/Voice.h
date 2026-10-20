// One sounding voice of an audio track: SRC machine → FLTR machine → amp envelope → drive → pan → sends.
#pragma once
#include "Dsp.h"
#include "Params.h"
#include "Samples.h"

namespace dt2
{
struct VoiceBuses
{
    float* outL; float* outR;   // track bus
    float* delL; float* delR;   // send buses
    float* revL; float* revR;
    float* chrL; float* chrR;
};

class Voice
{
public:
    struct Start
    {
        const Params* p;
        SamplePtr sample;
        double hostRate;
        double stepSeconds;   // duration of one step of this track
        int stepIdx;          // for the GRID machine
        float noteOffset;
        int velocity;         // -1 = use VEL
        const double* freeLfoPhase; // 3 phases owned by the engine (free-running LFOs)
        double bpm;
    };

    void prepare (double sampleRate) { comb.prepare ((int) (sampleRate * 0.1) + 4); }
    void start (const Start& s);
    void render (const VoiceBuses& b, int offset, int numSamples);
    void fadeOut();              // fast fade (monophonic re-trigger / stop)
    void releaseNow();           // musical release (first STOP)
    void applyLock (const Params& p);
    bool isActive() const { return active; }

private:
    enum class Amp { attack, hold, decay, sustain, release, done };
    bool active = false;
    Params p {};
    SamplePtr sample;
    double sr = 48000;

    // source
    bool rev = false, loop = false;
    double s0 = 0, s1 = 0, loopStart = 0; // playback-coordinate region (frames)
    double pos = 0, rate = 1;
    int nFrames = 0;
    // stretch (granular)
    bool stretch = false;
    struct Grain { bool on = false; double pos = 0; int age = 0; };
    Grain grains[2];
    int grainLen = 0, hop = 0, outLen = 0, outCount = 0, nextGrain = 0, grainIdx = 0;
    double pitch = 1, regionLen = 0;
    // bit / sample-rate reduction
    float brQ = 0; int srrHold = 1, srrCount = 0; float heldL = 0, heldR = 0;

    // filter
    Biquad f1, f2, f3;
    int fltm = 0;
    float baseF = 20000, q = 0.7f, envOct = 0;
    float fEnv = 0, fA = 0, fD = 0, fS = 0, fR = 0;
    int fStage = 0; // 0 attack 1 decay/sustain 2 release
    DelayLine comb;
    float combFb = 0, combLpCoef = 0, combLpL = 0, combLpR = 0, combBaseF = 100;
    int ctrlCounter = 0;

    // amp
    Amp amp = Amp::done;
    float ampV = 0, peakA = 1, susL = 1, aA = 0.001f, decT = 0, relT = 0.01f;
    int holdLeft = 0;
    bool holdUntilGate = false, decayInf = false;
    long long gateSamples = -1, age = 0;
    float fade = 1; bool fading = false;

    // output
    float volT = 1, vol = 1, panT = 0, pan = 0, sDelT = 0, sDel = 0, sRevT = 0, sRev = 0, sChrT = 0, sChr = 0;
    float ovr = 0;

    // LFOs
    struct Lfo { int dst = 0; float depth = 0, hz = 0; int wave = 0; float sph = 0; int mode = 0; bool free = true;
                 double phase = 0; float fadeT = 0; int fadeDir = 0; bool stopped = false; int idx = 0; };
    Lfo lfos[3];
    const double* freePhase = nullptr;
    float modPitch = 0, modFreq = 0, modReso = 0, modVol = 0, modPan = 0, modDel = 0, modRev = 0;

    void updateControl();
    void setFilter (float cutoffHz, float qv);
    float lfoValue (Lfo& l, int sampleInBlock);
    inline void readFrame (double x, float& l, float& r) const;
};
} // namespace dt2
