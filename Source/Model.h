// Project data received from the panel as JSON (see DigitaktMachine.pushState in the web app).
#pragma once
#include <JuceHeader.h>
#include "Params.h"
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace dt2
{
struct Lock { int id; float value; };

struct Step
{
    int trig = 0; // 0 empty, 1 note trig, 2 lock trig
    std::vector<Lock> locks;
};

struct Euclid { bool on = false; int pl1 = 4, pl2 = 0, ro1 = 0, ro2 = 0, tro = 0, op = 0; };

struct Track
{
    Params params = defaultParams();
    std::vector<Step> steps = std::vector<Step>(128);
    int length = 16;
    int speed = 4;
    float level = 100;
    Euclid euc;
    std::vector<bool> eucHits; // precomputed for `length` steps when euc.on
};

struct Pattern
{
    std::array<Track, 16> tracks;
    int masterLength = 16;
};

struct SongRow { int ptn = 0, rep = 1, len = 0, bpm = 0; };

struct KitSlot { Params params = defaultParams(); float level = 100; };

struct Project
{
    std::map<int, Pattern> patterns;
    int patternIdx = 0;
    int queuedPattern = -1;
    std::array<float, M_COUNT> master {};
    float bpm = 122, swing = 50, masterVol = 100;
    std::array<bool, 16> mutes {};
    bool fill = false;
    std::array<std::vector<SongRow>, 16> songs;
    bool songMode = false;
    int songIdx = 0;
    std::optional<std::array<KitSlot, 16>> performKit;

    const Pattern& pattern (int idx) const;
    /** track params with Perform Kit applied (TRIG-page params stay with the pattern) */
    Params trackParams (int patternIdx, int track) const;
    float trackLevel (int patternIdx, int track) const;

    static std::shared_ptr<Project> fromJson (const juce::String& json);
};

std::vector<bool> euclidPattern (const Euclid& e, int n);
/** index of a track parameter by its panel ID, or -1 */
int paramIndexOf (const juce::String& name);
} // namespace dt2
