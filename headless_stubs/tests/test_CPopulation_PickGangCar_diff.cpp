// test_CPopulation_PickGangCar_diff.cpp -- behavioural test for
// CPopulation::PickGangCar @ 0x614490, and the record of why its caller
// PickRiotRoadBlockCar @ 0x6144B0 is NOT tested here.
//
// WHY PickGangCar
//
// It is a one-line delegation:
//
//     eModelID CPopulation::PickGangCar(eGangID forGang) {
//         return (eModelID)m_LoadedGangCars[(size_t)forGang].PickRandomCar(false, false);
//     }
//
// which is close to the weakest case for a differential test -- except for the
// index. m_LoadedGangCars is declared as exactly 10 entries (Population.h:43)
// and forGang is an unchecked enum, so the function's correctness is entirely a
// question of what indices reach it and what it does with them. That is exactly
// what a runtime comparison can answer and a signature check cannot.
//
// The two `false` arguments are the second reason. PickRandomCar(bool, bool)
// takes flags whose meaning is not visible at the call site; a reimplementation
// that transposed them, or defaulted one to true, compiles and hooks and passes
// the static gate. Only the returned model differs.
//
// WHY ITS CALLER IS NOT TESTED HERE -- A MEASURED FINDING
//
// PickRiotRoadBlockCar walks gangs starting from a random offset:
//
//     const auto baseIdx = CGeneral::GetRandomNumberInRange(0u, (size_t)TOTAL_GANGS);
//     for (size_t i{}; i < TOTAL_GANGS; i++) {
//         const auto model = PickGangCar((eGangID)(baseIdx + i));
//
// `baseIdx + i` is NOT reduced modulo TOTAL_GANGS, so the argument reaches
// 2*TOTAL_GANGS-2 against a 10-entry array. A differential test of that
// function was written first and CRASHED:
//
//   FAIL  CPopulation/Diff_PickRiotRoadBlockCar_Diff: EXCEPTION during test
//
// The initial assumption -- that reading past a StaticRef into the game's data
// segment yields a benign wrong value -- was wrong, for a specific reason:
// PickGangCar does not read the element, it CALLS PickRandomCar() on it, so an
// out-of-range index invokes a method on whatever follows the array and
// dereferences pointers found there.
//
// That is a real defect in the reimplementation, and it is recorded rather than
// asserted, because baseIdx is drawn INSIDE the function: a caller cannot
// constrain it, so there is no way to exercise PickRiotRoadBlockCar's
// comparison without also running its out-of-range walk. A crashed test reports
// nothing at all -- it cannot say whether the ORIGINAL wraps -- so it would
// convert a finding into an absence of evidence.
//
// Testing PickGangCar directly is the part that CAN be observed: the index is
// an argument here, so the in-range behaviour is comparable, and the
// out-of-range behaviour is documented without being executed.
//
// THE RNG MUST BE SEEDED ON BOTH SIDES
//
// PickRandomCar draws, and the original runs in the exe's separately-seeded CRT
// (rand state at 0x821B1E). Without SeedBothRng the two sides draw from
// different streams and mismatch intermittently -- the noise that gets real
// findings dismissed as flake.
//
// STATUS: RED ON THE VACUITY GUARD, AND THAT IS THE GUARD WORKING
//
// Measured 2026-08-16:
//
//   FAIL  CPopulation/Diff_PickGangCar_Diff: EXPECT_GT(realModel, 0) failed
//   INFO  compared=640 mismatch=0 realmodel=0 invalid=640 distinct=0
//         ganggroups=10
//
// 640 paired calls, ZERO divergence -- and zero evidence. Every call returned
// MODEL_INVALID on both sides, because m_LoadedGangCars is populated by
// gameplay (AddToLoadedVehicles as zones stream) rather than by RequestModel,
// and nothing populates it in a headless warmup.
//
// mismatch=0 across 640 calls is exactly the shape of result that would be
// reported as a pass by a test without this guard, and it would mean nothing:
// the two sides agreed that an empty pool is empty. That is the failure mode
// ADR 0002 exists to prevent, and it is worth having a live example in the
// tree -- the three ChoosePoliceCarModel tests in test_CCarCtrl.cpp and
// test_CCarCtrl2.cpp are green today for the same reason.
//
// So the test is left RED rather than relaxed. Making it pass requires loading
// gang vehicles, not lowering the bar; until then this file honestly reports
// that PickGangCar has NOT been validated.
//
// A UNANIMOUS MODEL_INVALID IS NOT A PASS
//
// With no gang cars loaded every call returns MODEL_INVALID and both sides
// agree while touching nothing. The guard at the bottom requires a real model.

#include "StdInc.h"
#include "TestFramework.h"
#include "Population.h"

namespace {

// Unity build: `namespace {}` does NOT isolate these across test_*.cpp.
constexpr const char* kGangCarHookPath = "Global/CPopulation/PickGangCar";

struct GangPaired {
    int32 orig;
    int32 rev;
};

GangPaired CallBothGang(uint32 seed, int32 gang) {
    GangPaired p{};
    {
        SeedBothRng(seed);
        HookDisableGuard guard(kGangCarHookPath);
        p.orig = (int32)CPopulation::PickGangCar((eGangID)gang);
    }
    SeedBothRng(seed);
    p.rev = (int32)CPopulation::PickGangCar((eGangID)gang);
    return p;
}

} // namespace

GAME_DIFF_TEST(CPopulation, PickGangCar_Diff) {
    int32 compared   = 0;
    int32 mismatches = 0;
    int32 realModel  = 0;
    int32 invalid    = 0;

    int32 seenIds[64];
    int32 seenCount = 0;

    // IN-RANGE ONLY. m_LoadedGangCars has 10 entries (Population.h:43) and the
    // index is not bounds-checked, so this stops at the array size rather than
    // at TOTAL_GANGS -- see the header for why going past it crashes instead of
    // producing a comparable answer.
    const int32 kNumGangGroups = (int32)CPopulation::m_LoadedGangCars.size();

    for (int32 gang = 0; gang < kNumGangGroups; gang++) {
        // Seeds SPREAD by a large odd stride rather than 1,2,3,...: consecutive
        // seeds of this LCG give correlated first draws, so a linear scan
        // samples a narrow slice of the output range. In the
        // ChoosePoliceCarModel test that correction took a branch from 0 hits
        // to 1080.
        for (uint32 i = 0; i < 64; i++) {
            const uint32 seed = 1u + i * 2654435761u;   // Knuth's multiplier

            GangPaired p = CallBothGang(seed, gang);

            EXPECT_EQ(p.orig, p.rev);
            if (p.orig != p.rev) {
                if (mismatches < 6) {
                    RECORD_INFO("MISMATCH gang=%d seed=%u orig=%d rev=%d",
                                gang, seed, p.orig, p.rev);
                }
                mismatches++;
            }
            compared++;

            // From the ORIGINAL side only: EXPECT_EQ shows agreement, not that
            // a car was ever found.
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
    }

    RECORD_INFO("compared=%d mismatch=%d realmodel=%d invalid=%d distinct=%d "
                "ganggroups=%d",
                compared, mismatches, realModel, invalid, seenCount,
                kNumGangGroups);

    // ---- Vacuous-pass guards (ADR 0002) ----------------------------------

    EXPECT_GT(compared, 0);

    // realModel > 0: a gang car was actually picked. Without this, an empty
    // gang-car pool returns MODEL_INVALID on every call and the sweep proves
    // only that two empty lookups agree.
    //
    // NOTE: this is the guard most likely to fire in a headless run, because
    // the gang-car pools are populated by gameplay (CPopulation::LoadGroup ->
    // AddToLoadedVehicles as zones stream), not by RequestModel. If it does
    // fire, the run says nothing about PickGangCar and the fix is to load gang
    // vehicles, not to relax the guard.
    EXPECT_GT(realModel, 0);
}
