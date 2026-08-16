// test_CStreaming_DefaultCopModels_diff.cpp -- behavioural tests for
// CStreaming::GetDefaultCopCarModel @ 0x407C50 and
// CStreaming::GetDefaultCopModel  @ 0x407C00.
//
// WHY THESE TWO
//
// GetDefaultCopCarModel is the function the ChoosePoliceCarModel differential
// test leaned on and never tested. Every non-special-cop answer in that sweep
// -- 5320 of 9600 calls -- was this function's return value, taken on trust as
// "the default". It carries the comparison without ever having been compared
// itself, which is the same shape of gap as a test asserting against a helper
// it assumes correct.
//
// Both return a bare model id, so the static gate (compile + hook_ok +
// signature_ok) admits every wrong answer in the space. Neither is random, so
// unlike the ChoosePoliceCarModel arms they are fully deterministic given the
// world state -- which makes them CHEAP to test exhaustively and leaves no room
// to blame a mismatch on RNG phase.
//
// WHAT MAKES GetDefaultCopCarModel SHARP
//
// Its control flow is nested and its loop bound is argument-dependent:
//
//     carModelId = ms_DefaultCopBikeModel;
//     if (!m_bCopBikeLoaded || ignoreLvpd1Model
//         || !GetInfo(ms_DefaultCopBikerModel).IsLoaded()
//         || !GetInfo(ms_DefaultCopBikeModel).IsLoaded()) {
//         carModelId = ms_aDefaultCopCarModel[CTheZones::m_CurrLevel];
//         if (!GetInfo(ms_aDefaultCopModel[m_CurrLevel]).IsLoaded()
//          || !GetInfo(carModelId).IsLoaded()) {
//             for (i = 0; i < (ignoreLvpd1Model ? 3 : 4); i++) { ... }
//             return MODEL_INVALID;
//         }
//     }
//     return carModelId;
//
// Three things a signature check cannot see. The four-term `||` chain is
// order-independent in result but not in the count of IsLoaded calls. The
// `ignoreLvpd1Model ? 3 : 4` bound decides whether the LAST table entry is ever
// considered -- an off-by-one there is invisible unless entry 3 is the only
// loaded pair, which is exactly the kind of state a sweep over levels reaches
// and a single call does not. And the bike-vs-car precedence depends on
// m_bCopBikeLoaded, a global this test drives directly.
//
// The project has already been bitten by precisely an off-by-one of this shape:
// CStreamingInfo::SetCdPosnAndSize, where the wrong value was compiled into the
// ASI and the differential test passed anyway (2026-08-13).
//
// WHAT DRIVES THEM
//
// CTheZones::m_CurrLevel indexes both model tables, and m_bCopBikeLoaded gates
// the bike arm. Both are plain globals, so every branch is reachable by writing
// state rather than by arranging a world -- and both are saved and restored,
// because the rest of the suite observes the same globals.
//
// FINDING: THIS TEST IS RED, AND THE REVERSED CODE IS WRONG
//
// GetDefaultCopCarModel_Diff FAILS, reproducibly and identically across runs:
//
//   MISMATCH level=0 bike=1 ignore=0 orig=599 rev=523 bikemodel=523
//   MISMATCH level=1 bike=1 ignore=0 orig=596 rev=523 bikemodel=523
//   MISMATCH level=2 bike=1 ignore=0 orig=597 rev=523 bikemodel=523
//   MISMATCH level=3 bike=1 ignore=0 orig=598 rev=523 bikemodel=523
//   compared=20 mismatch=4 realmodel=20 invalid=0 bikearm=4 distinct=5
//
// On every level, with m_bCopBikeLoaded set and ignoreLvpd1Model false, the
// REVERSED code returns 523 (MODEL_COPBIKE) where the ORIGINAL returns that
// level's cop CAR (599 LA, 596 SF, 597 VG, 598 RU). Four of four bike-arm
// cases diverge; the other sixteen agree exactly. It is not RNG -- neither
// function draws -- and it reproduces value-for-value between runs.
//
// The state is legitimate, not something this test invented: model 523 and its
// rider 284 both streamed in during warmup (confirmed in the run log), so the
// bike arm's precondition was genuinely satisfiable. The two implementations
// simply disagree about what to do with it.
//
// WHY IT IS WORTH LOOKING AT THE ADDRESSES
//
// ms_aDefaultCopCarModel is a 5-entry int32 array at 0x8A5A8C, so its elements
// occupy 0x8A5A8C, 0x8A5A90, 0x8A5A94, 0x8A5A98 and 0x8A5A9C. And
// ms_DefaultCopBikeModel is declared at 0x8A5A9C -- element FOUR of that same
// array. The same holds on the ped side: ms_aDefaultCopModel is at 0x8A5AA0 and
// ms_DefaultCopBikerModel at 0x8A5AB0, again element four. Streaming.h's own
// comment says as much ("Last one is bike cop, not matching any level name").
//
// So the bike model is not a separate global beside the table; it is the
// table's last slot, aliased under a second name. Any reasoning about the bike
// arm that treats the two as independent is reasoning about a memory layout the
// game does not have, and the fallback loop's `ignoreLvpd1Model ? 3 : 4` bound
// is precisely what decides whether that shared slot is in range.
//
// This test does not diagnose which of the two implementations is misreading
// that layout -- it establishes, at runtime and reproducibly, THAT they differ
// and exactly where. Naming the cause belongs in the fix, with the
// disassembly at 0x407C50 in hand.
//
// The test is left RED deliberately. Relaxing the assertion to make the suite
// green would discard the only evidence that the divergence exists.
//
// A UNANIMOUS MODEL_INVALID IS NOT A PASS
//
// With no cop model streamed, both functions fall to their final return and
// agree on MODEL_INVALID everywhere -- green, and vacuous. The guards require a
// real model id to have come back at least once, which is what proves the
// lookup paths actually executed.

#include "StdInc.h"
#include "TestFramework.h"
#include "Streaming.h"
#include "TheZones.h"

namespace {

// Unity build: `namespace {}` does NOT isolate these across test_*.cpp, so
// every file-local name is suffixed.
constexpr const char* kCopCarHookPath = "Global/CStreaming/GetDefaultCopCarModel";
constexpr const char* kCopPedHookPath = "Global/CStreaming/GetDefaultCopModel";

// Save/restore the globals this file writes. m_CurrLevel in particular steers
// zone-dependent code all over the suite.
struct CopModelSaver {
    eLevelName currLevel;
    bool  copBikeLoaded;

    CopModelSaver()
        : currLevel(CTheZones::m_CurrLevel)
        , copBikeLoaded(CStreaming::m_bCopBikeLoaded)
    {
    }
    ~CopModelSaver() {
        CTheZones::m_CurrLevel        = currLevel;
        CStreaming::m_bCopBikeLoaded  = copBikeLoaded;
    }

    CopModelSaver(const CopModelSaver&) = delete;
    CopModelSaver& operator=(const CopModelSaver&) = delete;
};

struct CopPaired {
    int32 orig;
    int32 rev;
};

// No seeding: neither function draws from the RNG. If a mismatch appears here
// it cannot be dismissed as a phase difference between the two CRTs.
CopPaired CallBothCopCar(bool ignoreLvpd1Model) {
    CopPaired p{};
    {
        HookDisableGuard guard(kCopCarHookPath);
        p.orig = (int32)CStreaming::GetDefaultCopCarModel(ignoreLvpd1Model);
    }
    p.rev = (int32)CStreaming::GetDefaultCopCarModel(ignoreLvpd1Model);
    return p;
}

CopPaired CallBothCopPed() {
    CopPaired p{};
    {
        HookDisableGuard guard(kCopPedHookPath);
        p.orig = (int32)CStreaming::GetDefaultCopModel();
    }
    p.rev = (int32)CStreaming::GetDefaultCopModel();
    return p;
}

} // namespace

// ---------------------------------------------------------------------------

GAME_DIFF_TEST(CStreaming, GetDefaultCopCarModel_Diff) {
    CopModelSaver saver;

    int32 compared   = 0;
    int32 mismatches = 0;
    int32 realModel  = 0;   // not MODEL_INVALID
    int32 invalid    = 0;
    int32 bikeArm    = 0;   // answered with the cop BIKE rather than a car

    int32 seenIds[16];
    int32 seenCount = 0;

    // The tables are 5 entries (four levels plus the bike-cop slot), so sweep
    // every index m_CurrLevel can legitimately take.
    const int32 kNumLevels = (int32)CStreaming::ms_aDefaultCopCarModel.size();

    for (int32 level = 0; level < kNumLevels; level++) {
        for (int32 bikeLoaded = 0; bikeLoaded <= 1; bikeLoaded++) {
            // Both values of the argument: it changes the bike short-circuit
            // AND the fallback loop bound (3 vs 4), so it is the off-by-one
            // lever, not just a flag.
            for (int32 ignore = 0; ignore <= 1; ignore++) {
                CTheZones::m_CurrLevel       = (eLevelName)level;
                CStreaming::m_bCopBikeLoaded = (bikeLoaded != 0);

                CopPaired p = CallBothCopCar(ignore != 0);

                EXPECT_EQ(p.orig, p.rev);
                if (p.orig != p.rev) {
                    // Name the exact state that diverged. A bare count cannot
                    // distinguish a reversing defect from this test corrupting
                    // shared globals, and "4 of 20 differ" is unactionable.
                    if (mismatches < 6) {
                        RECORD_INFO("MISMATCH level=%d bike=%d ignore=%d "
                                    "orig=%d rev=%d bikemodel=%d",
                                    level, bikeLoaded, ignore,
                                    p.orig, p.rev,
                                    (int)CStreaming::ms_DefaultCopBikeModel);
                    }
                    mismatches++;
                }
                compared++;

                // Recorded from the ORIGINAL side: EXPECT_EQ proves agreement,
                // not that anything was found.
                if (p.orig == MODEL_INVALID) {
                    invalid++;
                } else {
                    realModel++;
                    if (p.orig == CStreaming::ms_DefaultCopBikeModel) {
                        bikeArm++;
                    }
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
    }

    RECORD_INFO("compared=%d mismatch=%d realmodel=%d invalid=%d bikearm=%d "
                "distinct=%d levels=%d",
                compared, mismatches, realModel, invalid, bikeArm,
                seenCount, kNumLevels);

    // ---- Vacuous-pass guards (ADR 0002) ----------------------------------

    EXPECT_GT(compared, 0);

    // realModel > 0: a lookup actually resolved. Without a streamed cop pair
    // every path falls to MODEL_INVALID and the sweep compares the failure
    // return against itself -- green, and evidence of nothing. If this fires,
    // the cop models did not stream in; widen GAME_TEST_REQUEST_MODELS rather
    // than trusting the result.
    EXPECT_GT(realModel, 0);

    // distinct > 1: different levels resolved to different cars. One id across
    // the whole sweep is consistent with an implementation ignoring m_CurrLevel
    // and returning a constant, which is the failure this sweep exists to rule
    // out.
    EXPECT_GT(seenCount, 1);
}

// ---------------------------------------------------------------------------

GAME_DIFF_TEST(CStreaming, GetDefaultCopModel_Diff) {
    CopModelSaver saver;

    int32 compared   = 0;
    int32 mismatches = 0;
    int32 realModel  = 0;
    int32 invalid    = 0;

    int32 seenIds[16];
    int32 seenCount = 0;

    const int32 kNumLevels = (int32)CStreaming::ms_aDefaultCopModel.size();

    for (int32 level = 0; level < kNumLevels; level++) {
        CTheZones::m_CurrLevel = (eLevelName)level;

        CopPaired p = CallBothCopPed();

        EXPECT_EQ(p.orig, p.rev);
        if (p.orig != p.rev) {
            mismatches++;
        }
        compared++;

        if (p.orig == MODEL_INVALID) {
            invalid++;
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

    RECORD_INFO("compared=%d mismatch=%d realmodel=%d invalid=%d distinct=%d "
                "levels=%d",
                compared, mismatches, realModel, invalid, seenCount, kNumLevels);

    EXPECT_GT(compared, 0);

    // realModel > 0: the level lookup or its fallback sweep found something.
    // Both arms return a model id and only the exhausted path returns
    // MODEL_INVALID, so this is what separates "the lookup ran" from "nothing
    // was loaded and both sides said so".
    EXPECT_GT(realModel, 0);
}
