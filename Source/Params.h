// Parameter table shared with the web panel (src/lib/dt-model.ts). IDs must match exactly.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

// X(id, default)
#define DT2_TRACK_PARAMS(X) \
    X(note, 0) X(vel, 100) X(len, 17) X(cond, 0) X(mtim, 0) X(rtrg, 0) X(rtim, 9) X(lfot, 1) \
    X(tune, 0) X(play, 0) X(br, 0) X(smp, 0) X(strt, 0) X(slen, 127) X(loop, 0) X(lev, 110) \
    X(fatk, 0) X(fdec, 64) X(fsus, 0) X(frel, 64) X(freq, 127) X(reso, 0) X(ftyp, 0) X(fenv, 0) \
    X(aatk, 0) X(ahld, 127) X(adec, 127) X(asus, 127) X(arel, 30) X(ovr, 0) X(pan, 0) X(vol, 100) \
    X(srr, 0) X(del, 0) X(rev, 0) X(chr, 0) \
    X(mchn, 0) X(mbnk, -1) X(mprg, -1) X(mpb, 0) X(mat, 0) X(mn2, -1) X(mn3, -1) X(mn4, -1) \
    X(cv1, -1) X(cv2, -1) X(cv3, -1) X(cv4, -1) X(cv5, -1) X(cv6, -1) X(cv7, -1) X(cv8, -1) \
    X(cv9, -1) X(cv10, -1) X(cv11, -1) X(cv12, -1) X(cv13, -1) X(cv14, -1) X(cv15, -1) X(cv16, -1) \
    X(cs1, 1) X(cs2, 2) X(cs3, 7) X(cs4, 10) X(cs5, 71) X(cs6, 72) X(cs7, 73) X(cs8, 74) \
    X(cs9, 75) X(cs10, 76) X(cs11, 77) X(cs12, 78) X(cs13, 79) X(cs14, 80) X(cs15, 81) X(cs16, 82) \
    X(srcm, 0) X(fltm, 0) X(fit, 16) X(slic, 0) X(slcs, 3) X(gofs, 0) \
    X(eql, 0) X(eqmf, 70) X(eqmg, 0) X(eqmq, 40) X(eqh, 0) X(cdmp, 60) X(lgty, 0) \
    X(lspd, 48) X(lmul, 2) X(lfad, 0) X(ldst, 0) X(lwav, 0) X(lsph, 0) X(lmod, 0) X(ldep, 0) \
    X(l2spd, 48) X(l2mul, 2) X(l2fad, 0) X(l2dst, 0) X(l2wav, 0) X(l2sph, 0) X(l2mod, 0) X(l2dep, 0) \
    X(l3spd, 48) X(l3mul, 2) X(l3fad, 0) X(l3dst, 0) X(l3wav, 0) X(l3sph, 0) X(l3mod, 0) X(l3dep, 0)

#define DT2_MASTER_PARAMS(X) \
    X(dtim, 7) X(dfb, 58) X(dlpf, 90) X(chdp, 64) X(rdec, 70) X(rton, 80) X(cthr, 100) X(crat, 30) \
    X(cvol, 100) X(dvol, 100) X(rvol, 100) X(movr, 0) X(ivol, 110)

namespace dt2
{
enum P : int
{
#define X(id, d) id,
    DT2_TRACK_PARAMS(X)
#undef X
    P_COUNT
};

enum M : int
{
#define X(id, d) m_##id,
    DT2_MASTER_PARAMS(X)
#undef X
    M_COUNT
};

inline const char* paramName(int i)
{
    static const char* names[] = {
#define X(id, d) #id,
        DT2_TRACK_PARAMS(X)
#undef X
    };
    return names[i];
}
inline float paramDefault(int i)
{
    static const float defs[] = {
#define X(id, d) (float) d,
        DT2_TRACK_PARAMS(X)
#undef X
    };
    return defs[i];
}
inline const char* masterName(int i)
{
    static const char* names[] = {
#define X(id, d) #id,
        DT2_MASTER_PARAMS(X)
#undef X
    };
    return names[i];
}
inline float masterDefault(int i)
{
    static const float defs[] = {
#define X(id, d) (float) d,
        DT2_MASTER_PARAMS(X)
#undef X
    };
    return defs[i];
}

using Params = std::array<float, P_COUNT>;

inline Params defaultParams()
{
    Params p {};
    for (int i = 0; i < P_COUNT; ++i)
        p[(size_t) i] = paramDefault(i);
    return p;
}

// ---- tables (same order as the panel) ----
inline constexpr float LEN_TABLE[] = { 0.125f, 0.188f, 0.25f, 0.375f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4, 6, 8, 12, 16, 32, 64, -1 }; // -1 = INF
inline constexpr int LEN_COUNT = 18;
inline constexpr int RTIM_TABLE[] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80 };
inline constexpr float SPEEDS[] = { 0.125f, 0.25f, 0.5f, 0.75f, 1, 1.5f, 2 };
inline constexpr int LFO_MULT[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048 };
inline constexpr float DEL_TIMES[] = { 0.125f, 1.f / 6, 0.25f, 1.f / 3, 0.375f, 0.5f, 2.f / 3, 0.75f, 1, 1.5f, 2 };
inline constexpr int SLICE_COUNTS[] = { 2, 4, 8, 16, 32, 64 };
inline constexpr int MIDI_MACHINE = 5;

// FILTER_TYPES: LP2 LP4 BP2 HP2 HP4 BR2 PK2
enum class CondKind { none, prob, ab, fill, nfill, pre, npre, nei, nnei, first, nfirst };
struct Cond { CondKind kind; int p = 0, a = 0, b = 0; };

inline const std::array<Cond, 65>& conditions()
{
    static std::array<Cond, 65> list = [] {
        std::array<Cond, 65> l {};
        int n = 0;
        l[(size_t) n++] = { CondKind::none };
        l[(size_t) n++] = { CondKind::fill };
        l[(size_t) n++] = { CondKind::nfill };
        l[(size_t) n++] = { CondKind::pre };
        l[(size_t) n++] = { CondKind::npre };
        l[(size_t) n++] = { CondKind::nei };
        l[(size_t) n++] = { CondKind::nnei };
        l[(size_t) n++] = { CondKind::first };
        l[(size_t) n++] = { CondKind::nfirst };
        for (int p : { 1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99 })
            l[(size_t) n++] = { CondKind::prob, p };
        for (int b = 2; b <= 8; ++b)
            for (int a = 1; a <= b; ++a)
                l[(size_t) n++] = { CondKind::ab, 0, a, b };
        return l;
    }();
    return list;
}

// ---- value mappings (identical to the web engine) ----
inline float timeOf(float v) { return 0.0015f * std::pow(6000.0f, v / 127.0f); }
inline float cutoffOf(float v) { return 20.0f * std::pow(1000.0f, v / 127.0f); }
inline float qOf(float v) { return 0.5f + std::pow(v / 127.0f, 2.0f) * 22.0f; }
inline float volGain(float v) { return std::pow(std::max(0.0f, v) / 127.0f, 1.6f); }
} // namespace dt2
