// test_CCarCtrl_ChooseCarModelToLoad_diff.cpp -- behavioural test for
// CCarCtrl::ChooseCarModelToLoad @ 0x421900.
//
// WHY THIS FUNCTION
//
// It returns a bare int32, so the static gate (compile + hook_ok +
// signature_ok) admits every wrong answer in the space. And its body carries
// the specific defect class this project has already been bitten by -- a
// condition whose sense is easy to invert:
//
//     const auto model = m_CarGroups[groupID][GetRandomNumberInRange(n)];
//     if (!CStreaming::IsModelLoaded(model)) {
//         return model;                      // <-- NOT loaded is the hit
//     }
//
// The `!` is load-bearing and counter-intuitive: this function picks a model to
// LOAD, so it wants one that is not resident yet. Dropping the `!` inverts it
// into "return the first model already loaded", which still compiles, still
// hooks, still matches the signature, and is wrong in a way only a runtime
// comparison against the original can see. Upstream #1247 was exactly an
// inverted `!`, and run 7 of this project's own record passed the same gate
// with a body of `return train;`.
//
// The 16-try loop is the second reason. It gives up after 16 draws and returns
// -1, so an implementation that loops forever, loops a different number of
// times, or returns a different sentinel diverges only under RNG pressure --
// visible when many seeds are swept, invisible in a single call.
//
// WHAT DRIVES IT
//
// m_CarGroups / m_nNumCarsInGroup are loaded from CARGRP.DAT during init
// (Population.cpp:221), so they are populated headless without streaming
// anything. groupID indexes them directly, which makes every group reachable
// by argument alone -- no world state to arrange.
//
// THE RNG MUST BE SEEDED ON BOTH SIDES
//
// GetRandomNumberInRange drives the draw, and the original runs in the exe,
// whose statically-linked CRT has a separate rand() state at 0x821B1E. Seeding
// only the DLL leaves the two sides on different streams and the comparison
// becomes a coin toss. SeedBothRng before each half is what makes a mismatch
// mean a real divergence rather than a phase difference.
//
// A UNANIMOUS -1 IS NOT A PASS
//
// If a group is empty (numCarsInGroup == 0) the function returns -1 without
// drawing at all, and both sides agree vacuously. If EVERY group is empty the
// whole sweep is -1 == -1 and looks green while testing nothing. The guards at
// the bottom require that a real model id came back at least once.

#include "StdInc.h"
#include "TestFramework.h"
#include "Population.h"

namespace {

// Unity build: test_*.cpp are concatenated, so `namespace {}` does NOT isolate
// these. Every file-local name is suffixed with the function under test.
constexpr const char* kLoadHookPath = "Global/CCarCtrl/ChooseCarModelToLoad";

struct LoadPaired {
    int32 orig;
    int32 rev;
};

LoadPaired CallBothLoad(uint32 seed, int32 groupID) {
    LoadPaired p{};
    {
        SeedBothRng(seed);
        HookDisableGuard guard(kLoadHookPath);
        p.orig = CCarCtrl::ChooseCarModelToLoad(groupID);
    }
    SeedBothRng(seed);
    p.rev = CCarCtrl::ChooseCarModelToLoad(groupID);
    return p;
}

} // namespace

GAME_DIFF_TEST(CCarCtrl, ChooseCarModelToLoad_Seeded) {
    int32 compared    = 0;
    int32 mismatches  = 0;
    int32 realModel   = 0;   // a model id, not the -1 sentinel
    int32 sentinel    = 0;   // the -1 give-up path
    int32 groupsWithCars = 0;
    int32 distinctSeen = 0;

    // Distinct model ids observed, so a green cannot mean "both sides always
    // returned the same one thing".
    int32 seenIds[64];
    int32 seenCount = 0;

    const int32 kNumGroups = (int32)CPopulation::m_nNumCarsInGroup.size();

    for (int32 groupID = 0; groupID < kNumGroups; groupID++) {
        if (CPopulation::m_nNumCarsInGroup[groupID] > 0) {
            groupsWithCars++;
        }

        // Seeds are SPREAD by a large odd stride rather than taken as
        // 1,2,3,...: consecutive seeds of this LCG give correlated first draws,
        // so a linear scan samples a narrow slice of the output range. That is
        // not hypothetical -- in the ChoosePoliceCarModel test it hid an entire
        // branch (enforcer=0 over 576 calls) until the stride was introduced.
        for (uint32 i = 0; i < 64; i++) {
            const uint32 seed = 1u + i * 2654435761u;   // Knuth's multiplier

            LoadPaired p = CallBothLoad(seed, groupID);

            EXPECT_EQ(p.orig, p.rev);
            if (p.orig != p.rev) {
                mismatches++;
            }
            compared++;

            // Bookkeeping from the ORIGINAL side only. EXPECT_EQ proves the two
            // agree; it cannot tell "both returned a model" from "both gave up".
            if (p.orig == -1) {
                sentinel++;
            } else {
                realModel++;
                bool known = false;
                for (int32 k = 0; k < seenCount; k++) {
                    if (seenIds[k] == p.orig) { known = true; break; }
                }
                if (!known && seenCount < (int32)(sizeof(seenIds) / sizeof(seenIds[0]))) {
                    seenIds[seenCount++] = p.orig;
                }
            }
        }
    }
    distinctSeen = seenCount;

    RECORD_INFO("compared=%d mismatch=%d realmodel=%d sentinel=%d "
                "groupswithcars=%d distinct=%d numgroups=%d",
                compared, mismatches, realModel, sentinel,
                groupsWithCars, distinctSeen, kNumGroups);

    // ---- Vacuous-pass guards (ADR 0002) ----------------------------------

    // compared > 0: nothing called means nothing tested.
    EXPECT_GT(compared, 0);

    // groupsWithCars > 0: CARGRP.DAT actually loaded. If every group is empty
    // the function short-circuits to -1 before drawing, and the whole sweep
    // compares the give-up path against itself.
    EXPECT_GT(groupsWithCars, 0);

    // realModel > 0: the load-bearing one. The `!IsModelLoaded` return -- the
    // inversion-prone line this test exists for -- must actually have been
    // taken. Without this, a build where every candidate happened to be
    // resident would return -1 everywhere and pass while never executing it.
    EXPECT_GT(realModel, 0);

    // distinctSeen > 1: more than one model id came back across the sweep,
    // which is what distinguishes a working random draw from an implementation
    // returning a single constant regardless of group and seed.
    EXPECT_GT(distinctSeen, 1);
}
