// Sequencer + voices + master effects. Everything in process() runs on the audio thread.
#pragma once
#include <JuceHeader.h>
#include "Model.h"
#include "Voice.h"
#include <deque>

namespace dt2
{
struct Command
{
    enum Type { trigger, play, pause, stop } type = trigger;
    int track = 0;
    float noteOffset = 0;
    int velocity = -1;
    int lockCount = 0;
    Lock locks[16] {};
};

struct HostInfo
{
    bool hostPlaying = false;
    bool hasPpq = false;
    double ppq = 0;
    double bpm = 0; // 0 = host gave none
};

/** values published to the panel (written on the audio thread, read by the editor timer) */
struct Status
{
    std::atomic<bool> playing { false }, paused { false };
    std::atomic<int> steps[16];
    std::atomic<int> flashes[16];
    std::atomic<int> patternIdx { 0 }, queued { -1 }, songRow { 0 }, songRep { 0 };
    std::atomic<float> bpm { 0 };
    Status() { for (int i = 0; i < 16; ++i) { steps[i] = -1; flashes[i] = 0; } }
};

class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);

    /** message thread: install new project / pool (lock-free hand-over) */
    void setProject (std::shared_ptr<const Project> p);
    void setPool (std::shared_ptr<const Pool> p);
    bool pushCommand (const Command& c);

    /** audio thread. out: channel pairs [main, track1..track16] (missing pairs are skipped) */
    void process (juce::AudioBuffer<float>& out, int numAuxPairs, juce::MidiBuffer& midi, const HostInfo& host);

    Status status;

private:
    // shared state hand-over
    juce::SpinLock handLock;
    std::shared_ptr<const Project> pendingProject;
    std::shared_ptr<const Pool> pendingPool;
    std::shared_ptr<const Project> project;
    std::shared_ptr<const Pool> pool;
    std::deque<std::pair<juce::int64, std::shared_ptr<const void>>> keepAlive; // message thread only
    juce::AbstractFifo fifo { 256 };
    std::array<Command, 256> commands;

    double sr = 48000;
    int maxBlock = 512;

    // transport / sequencer state
    bool running = false, paused = false, wasHostPlaying = false, followingHost = false;
    double pos16 = 0;          // current position in 16th notes
    double patternStart = 0;   // 16th position where the current pattern/row began
    int patternIdx = 0, queued = -1;
    int songRow = 0, songRep = 0;
    bool songActive = false;
    double bpmNow = 122;
    struct CondMemo { long long n = -1; bool result = true; };
    std::array<CondMemo, 16> condMemo;
    std::array<bool, 16> lastCond {};
    std::array<std::array<double, 3>, 16> freePhase {};

    // two voices per track: the sounding one and the one fading out after a re-trigger
    std::array<std::array<Voice, 2>, 16> voices;
    std::array<int, 16> curVoice {};

    // MIDI note-offs
    struct PendingOff { int ch, note; long long samplesLeft; };
    std::vector<PendingOff> pendingOffs;
    std::array<int, 16> lastProgram {};

    // buses
    juce::AudioBuffer<float> trackBus, sendBus; // trackBus: 32 ch; sendBus: del L/R, rev L/R, chr L/R

    // master effects
    DelayLine delay, chorus;
    Biquad delHp, delLp, revHp, revTone;
    juce::Reverb reverb;
    juce::dsp::Compressor<float> comp;
    double chorusPhase[2] {};

    // kind: 0 note trig, 1 lock trig, 2 preview (command / MIDI in)
    struct Event { int offset; int track; int kind; int ptn; int stepIdx; double stepSec; int velocity; float noteOffset; const Command* cmd; };
    std::vector<Event> events;
    int lastReadCount = 0;
    juce::MidiBuffer midiIn;

    void pullShared();
    void handleCommands (juce::MidiBuffer& midi);
    void collectEvents (double from16, double to16, int blockOffset, double samplesPer16);
    void advanceBoundary (double at16);
    double rowLength() const;
    bool evalCond (int track, const Params& p, long long n, int length);
    void fireAudio (int track, const Params& p, int offset, int stepIdx, double stepSec, float noteOffset, int velocity);
    void fireMidi (int track, const Params& p, int offset, double stepSec, float noteOffset, int velocity, juce::MidiBuffer& midi);
    void renderVoices (int from, int n);
    void renderMaster (juce::AudioBuffer<float>& out, int numAuxPairs, int n);
    Params resolve (int track, const Step* st) const;
};
} // namespace dt2
