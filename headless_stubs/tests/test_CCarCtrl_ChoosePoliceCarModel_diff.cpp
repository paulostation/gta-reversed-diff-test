// test_CCarCtrl_ChoosePoliceCarModel_diff.cpp -- behavioural test for
// CCarCtrl::ChoosePoliceCarModel @ 0x421980.
//
// A SEPARATE FILE, per the policy that produced
// test_CCarCtrl_GetNewVehicle_diff.cpp: test_CCarCtrl.cpp holds working tests
// and an upstream update must not silently drop ours, nor our mistake break
// theirs. build-tests.sh globs test_*.cpp so the name is enough.
//
// WHY THIS FUNCTION
//
// The static gate this reimplementation passed is compile + hook_ok +
// signature_ok. For a function returning a bare int32 that gate is close to
// vacuous: every wrong answer in the space is also an int32. The project's own
// record shows that gate accepting a body of `return train;` (run 7) and an
// inverted `!` (upstream #1247), and a `CStreamingInfo::SetCdPosnAndSize`
// off-by-one that a differential test passed anyway because the unhook was
// never verified (2026-08-13 -- the reason HookDisableGuard now checks).
//
// It is a sharp case for a specific structural reason. The reimplementation is
// an if/ELSE:
//
//     if (AreSwatRequired() && ENFORCER loaded && SWAT loaded) {
//         if (GetRandomNumberInRange(0, 3) == 2) return MODEL_ENFORCER;
//     } else {
//         if (AreFbiRequired()  && ...) return MODEL_FBIRANCH;
//         if (AreArmyRequired() && ...) return MODEL_RHINO + coinflip;
//     }
//     return CStreaming::GetDefaultCopCarModel(ignoreLvpd1Model);
//
// The SWAT arm therefore SUPPRESSES the FBI and army checks entirely: at
// wanted level 4 with SWAT models loaded, a failing 1-in-3 roll falls straight
// through to GetDefaultCopCarModel and the FBI/army branches are never
// consulted. Whether the original at 0x421980 has that same else-coupling, or
// tests the three independently, is exactly the kind of branch-topology
// question a signature check cannot see and a reader of the C++ cannot settle.
// This test asks the original itself.
//
// The wanted-level thresholds make the arms REACHABLE ON DEMAND rather than by
// waiting: AreSwatRequired/AreFbiRequired/AreArmyRequired are
// `m_nWantedLevel == 4|5|6 || m_b*Required` (Wanted.cpp 0x561F40/60/80). So
// sweeping levels 0..6 and, separately, forcing each override bit drives every
// arm deterministically. No Totalitarian waiting is needed here.
//
// THE RNG IS THE TRAP, AND IT IS THE REASON SeedBothRng EXISTS
//
// Two arms are random: the SWAT arm's GetRandomNumberInRange(0, 3) and the
// army arm's `(GetRandomNumber() < 0x3FFF) + MODEL_RHINO` coin flip. The
// original runs in the exe, whose statically-linked CRT has its OWN rand()
// state at 0x821B1E, separate from the DLL's. Seeding only one leaves the two
// sides drawing from different streams, and the comparison becomes a coin
// toss that fails intermittently and gets dismissed as flake.
//
// So every paired call is bracketed by SeedBothRng(seed) -- once before the
// original, once with the SAME seed before the reversed -- which is the whole
// reason TestFramework.h carries ExeSrand. A mismatch here is then a real
// divergence, not a phase difference between two RNGs.
//
// WHY IT IS NOT ENOUGH TO COMPARE ONE CALL PER LEVEL
//
// A single call per level samples one RNG draw. The SWAT arm returns
// MODEL_ENFORCER on roughly one roll in three, so a single sample at level 4
// would usually observe the fallthrough and would not distinguish an
// implementation that never returns ENFORCER at all. Each level is therefore
// swept over many seeds, and the test records how many DISTINCT models each
// side produced so a green cannot mean "both sides always said the same one
// thing".
//
// A UNANIMOUS DEFAULT IS NOT A PASS
//
// If no cop model is loaded, every branch guard is false, both sides return
// GetDefaultCopCarModel's answer, and they agree vacuously -- the same failure
// mode as the first version of the GetNewVehicle test, which passed with four
// assertions because the only model streamed in was an automobile. The guards
// at the bottom require that a non-default model was actually returned by the
// original at least once, i.e. that some special-cop branch really executed.

#include "StdInc.h"
#include "TestFramework.h"
#include "Wanted.h"
#include "Streaming.h"

namespace {

// Name deliberately qualified with the function. The build is a UNITY build:
// test_*.cpp are concatenated into one translation unit, so an anonymous
// namespace does NOT isolate this from the sibling diff test -- a plain
// `kPoliceHookPath` collides with the one in test_CCarCtrl_GetNewVehicle_diff.cpp
// (measured: C2374/C2086 in unity_64). Same reason the helpers below are
// suffixed rather than sharing generic names.
constexpr const char* kPoliceHookPath = "Global/CCarCtrl/ChoosePoliceCarModel";

// Save/restore the player's wanted state. This test WRITES m_nWantedLevel and
// the three override bits to drive the branches, and every other test in the
// suite observes the same singleton, so restoring is not optional.
// Modelled on WantedSaver in test_Scenario_WantedLevel.cpp -- renamed here
// rather than reused, because that struct is itself in an anonymous namespace
// the unity build does not isolate.
struct PoliceWantedSaver {
    CWanted* w;
    uint32   wantedLevel;
    uint8    swat, fbi, army;

    PoliceWantedSaver() : w(FindPlayerWanted()), wantedLevel(0), swat(0), fbi(0), army(0) {
        if (!w) {
            return;
        }
        wantedLevel = w->m_nWantedLevel;
        swat = w->m_bSwatRequired;
        fbi  = w->m_bFbiRequired;
        army = w->m_bArmyRequired;
    }
    ~PoliceWantedSaver() {
        if (!w) {
            return;
        }
        w->m_nWantedLevel  = wantedLevel;
        w->m_bSwatRequired = swat;
        w->m_bFbiRequired  = fbi;
        w->m_bArmyRequired = army;
    }

    PoliceWantedSaver(const PoliceWantedSaver&) = delete;
    PoliceWantedSaver& operator=(const PoliceWantedSaver&) = delete;
};

// One paired observation: the same seed and the same wanted state fed to the
// original and to the reversed code.
//
// Seeding happens INSIDE each half rather than once outside, because the two
// halves must each start from the identical RNG state -- the original's call
// advances both CRTs by an unknown amount, so re-seeding before the reversed
// call is what makes the second draw comparable to the first.
struct PolicePaired {
    int32 orig;
    int32 rev;
};

PolicePaired CallBothPolice(uint32 seed, uint32 ignoreLvpd1Model) {
    PolicePaired p{};
    {
        SeedBothRng(seed);
        HookDisableGuard guard(kPoliceHookPath);
        p.orig = CCarCtrl::ChoosePoliceCarModel(ignoreLvpd1Model);
    }
    SeedBothRng(seed);
    p.rev = CCarCtrl::ChoosePoliceCarModel(ignoreLvpd1Model);
    return p;
}

// What GetDefaultCopCarModel would return with no special-cop branch taken.
// Used ONLY to tell "a branch fired" from "both sides fell through", never as
// an expected value -- the test compares original against reversed, not
// against a value this file predicts.
int32 PoliceDefaultAnswer(uint32 ignoreLvpd1Model) {
    return static_cast<int32>(CStreaming::GetDefaultCopCarModel(ignoreLvpd1Model != 0));
}

// Cop models the branches need. Read from the game's own tables rather than
// hardcoded, so a wrong constant here cannot silently degrade coverage:
//   ms_aDefaultCopCarModel / ms_aDefaultCopModel (Streaming.h:192-193)
// plus the four the special arms name directly. The run script requests these
// via GAME_TEST_REQUEST_MODELS; this file does NOT request them itself,
// because tests run inside SuspendOtherThreads, which parks CdStreamThread and
// deadlocks any streaming call (measured 2026-08-12). The request happens in
// WARMUP, where the frame loop pumps the streamer.
//
// Note the guards pair a VEHICLE with its PED crew -- ENFORCER+SWAT,
// FBIRANCH+FBI, RHINO+BARRACKS+ARMY -- and SWAT/FBI/ARMY are ped models in the
// 285-287 range, not vehicles. Requesting only the vehicles would leave every
// special arm unreachable and the sweep would agree on the default forever, so
// both halves are listed:
//
//   GAME_TEST_REQUEST_MODELS=285,286,287,427,432,433,490,523,280,596,597,598,599
//     285 swat(ped)   286 fbi(ped)    287 army(ped)   280 cop(ped)
//     427 enforcer    432 rhino       433 barracks    490 fbiranch  523 copbike
//     596 copcarla    597 copcarsf    598 copcarvg    599 copcarru
//
// Loadedness is VERIFIED at runtime below rather than trusted.

} // namespace

// Named _WantedDriven, not plain ChoosePoliceCarModel: test_CCarCtrl.cpp
// ALREADY declares GAME_DIFF_TEST(CCarCtrl, ChoosePoliceCarModel), so the plain
// name produces two same-named entries in the results file and the run cannot
// be attributed (observed: two separate "Diff_ChoosePoliceCarModel" lines, one
// with 580 assertions and one with 4).
//
// The suffix also states the actual difference between this test and those
// three. They sweep the FUNCTION ARGUMENT, which is `ignoreLvpd1Model` -- a
// flag, not a wanted level and not a city id, despite what their loop
// variables and the "_AllLevels"/"_Extended" names claim. None of them ever
// writes m_nWantedLevel, so AreSwatRequired/AreFbiRequired/AreArmyRequired are
// all false in every one of their iterations and the special-cop arms are
// unreachable. They are green because they only ever compare the
// GetDefaultCopCarModel fallthrough against itself. This test drives the
// wanted state that actually selects the arms.
GAME_DIFF_TEST(CCarCtrl, ChoosePoliceCarModel_WantedDriven) {
    CWanted* w = FindPlayerWanted();
    // Not an early `return`: a silent skip is indistinguishable from a pass.
    EXPECT_TRUE(w != nullptr);
    if (!w) {
        return;
    }
    PoliceWantedSaver saver;

    int32 compared        = 0;   // paired calls that actually ran
    int32 mismatches      = 0;   // counted as well as asserted, for the summary
    int32 nonDefaultOrig  = 0;   // ORIGINAL took a special-cop branch
    int32 enforcerOrig    = 0;   // SWAT arm observed on the original
    int32 fbiOrig         = 0;   // FBI arm observed
    int32 armyOrig        = 0;   // army arm observed (RHINO or BARRACKS)
    int32 distinctOrig    = 0;

    bool seenModel[3] = { false, false, false };  // enforcer / fbiranch / army

    // Sweep both the natural thresholds (wanted level) and the override bits,
    // because the two are OR'd in AreXRequired and a reimplementation could
    // honour one and drop the other.
    struct PoliceSetup { uint32 level; uint8 swat, fbi, army; const char* what; };
    const PoliceSetup kSetups[] = {
        { 0, 0, 0, 0, "level0" },
        { 1, 0, 0, 0, "level1" },
        { 2, 0, 0, 0, "level2" },
        { 3, 0, 0, 0, "level3" },
        { 4, 0, 0, 0, "level4_swat" },
        { 5, 0, 0, 0, "level5_fbi" },
        { 6, 0, 0, 0, "level6_army" },
        // Override bits at a level that triggers none of the thresholds, so a
        // hit proves the bit itself was honoured.
        { 1, 1, 0, 0, "override_swat" },
        { 1, 0, 1, 0, "override_fbi" },
        { 1, 0, 0, 1, "override_army" },
        // Both halves of the else-coupling live at once: if the original treats
        // the three checks independently rather than as if/else, this is where
        // the reimplementation's suppression of FBI/army shows up.
        { 4, 0, 1, 1, "swat_plus_fbi_army" },
        { 1, 1, 1, 1, "all_overrides" },
    };

    for (const auto& s : kSetups) {
        // Both values of the parameter: it changes GetDefaultCopCarModel's
        // fallback AND the loop bound inside it (3 vs 4).
        for (uint32 ignoreLvpd1 = 0; ignoreLvpd1 <= 1; ignoreLvpd1++) {
            // Many seeds per setup. The army arm is a coin flip and needs only
            // a handful, but the SWAT arm is far rarer than it looks and drives
            // this bound.
            //
            // GetRandomNumberInRange(0, 3) is the INTEGER overload, which
            // forwards to the FLOAT one as lerp(0, 3-1, rnd/RAND_MAX) and
            // truncates (General.h:120). So it yields [0.0, 2.0] truncated to
            // {0, 1, 2}, and the `== 2` the SWAT arm tests is reached only when
            // the lerp lands on exactly 2.0 -- i.e. when GetRandomNumber()
            // returns its maximum. That is ~1 in 32768 per call, NOT the 1 in 3
            // a reader of `GetRandomNumberInRange(0, 3) == 2` would assume.
            //
            // Measured: 576 paired calls produced enforcer=0. That is expected
            // at this rate, so a low seed count cannot distinguish "the arm is
            // rare" from "the arm is dead". The count below is raised to sample
            // it, and the enforcer counter is reported rather than asserted --
            // see the guards at the bottom for why it is not an EXPECT.
            // Seeds are SPREAD rather than taken as 1,2,3,...: consecutive
            // seeds of this LCG produce correlated first draws, so a linear
            // scan of small seeds samples a narrow slice of the output range
            // and would keep missing the top-of-range value the SWAT arm needs.
            // The stride is a large odd constant so the sequence walks the
            // whole 32-bit seed space instead.
            for (uint32 i = 0; i < 400; i++) {
                const uint32 seed = 1u + i * 2654435761u;   // Knuth's multiplier
                w->m_nWantedLevel  = s.level;
                w->m_bSwatRequired = s.swat;
                w->m_bFbiRequired  = s.fbi;
                w->m_bArmyRequired = s.army;

                PolicePaired p = CallBothPolice(seed, ignoreLvpd1);

                // The comparison this file exists for.
                EXPECT_EQ(p.orig, p.rev);
                if (p.orig != p.rev) {
                    mismatches++;
                }
                compared++;

                // Coverage bookkeeping, recorded from the ORIGINAL side only.
                // EXPECT_EQ proves the two agree; it cannot tell "both took the
                // SWAT arm" from "neither did". These counters are what make a
                // green mean the branches were actually entered.
                const int32 dflt = PoliceDefaultAnswer(ignoreLvpd1);
                if (p.orig != dflt) {
                    nonDefaultOrig++;
                }
                if (p.orig == MODEL_ENFORCER) { enforcerOrig++; seenModel[0] = true; }
                if (p.orig == MODEL_FBIRANCH) { fbiOrig++;      seenModel[1] = true; }
                if (p.orig == MODEL_RHINO || p.orig == MODEL_BARRACKS) {
                    armyOrig++; seenModel[2] = true;
                }
            }
        }
    }

    for (bool m : seenModel) {
        if (m) {
            distinctOrig++;
        }
    }

    // ---- Vacuous-pass guards (ADR 0002) ----------------------------------
    //
    // compared > 0: nothing called means nothing tested.
    EXPECT_GT(compared, 0);

    // nonDefaultOrig > 0: the load-bearing one. Without a loaded ENFORCER/SWAT,
    // FBIRANCH/FBI or RHINO/BARRACKS/ARMY set, every guard in the function is
    // false, both sides return GetDefaultCopCarModel's answer, and the whole
    // sweep agrees while never entering a single branch under test. That is a
    // green that says nothing -- exactly the four-assertion "pass" the first
    // GetNewVehicle test produced. If this fires, the requested cop models did
    // not stream in; widen GAME_TEST_REQUEST_MODELS rather than trusting the
    // result.
    EXPECT_GT(nonDefaultOrig, 0);

    // distinctOrig > 1: at least two DIFFERENT special-cop models came back
    // across the sweep. One is consistent with a stuck implementation that
    // returns a single constant regardless of wanted level; two means the
    // level/override dispatch genuinely selects among arms.
    //
    // distinct=3 was measured, so this could be tightened to > 2; it is left at
    // > 1 because the SWAT arm's reachability is asserted separately below and
    // this guard is about dispatch selecting among arms at all.
    EXPECT_GT(distinctOrig, 1);

    // enforcerOrig > 0: the SWAT arm specifically. Worth its own guard because
    // it is the arm that was silently missing: the first run recorded
    // enforcer=0 over 576 calls and still passed every other guard, since FBI
    // and army alone satisfied distinctOrig > 1.
    //
    // The cause was NOT that the branch is dead. GetRandomNumberInRange(0, 3)
    // reaches its `== 2` case only at the very top of the RNG range, and
    // consecutive small seeds produce correlated first draws that never land
    // there. Spreading the seeds by a large odd stride took it from 0 to 1080
    // hits in 9600 calls with no other change.
    //
    // So this assertion is safe -- the arm fires reliably under the seed
    // schedule above -- and it is the thing that would catch a future edit to
    // that schedule quietly returning the test to sampling only the
    // fallthrough, which is the state it was in when it first looked green.
    EXPECT_GT(enforcerOrig, 0);

    // (Census scaffold removed: the coverage question is answered. The run of
    // 2026-08-15 recorded compared=9600 mismatch=0 nondefault=4280
    // enforcer=1080 fbi=1600 army=1600 distinct=3 with all seven cop models
    // loaded -- all three special-cop arms exercised, no divergence. The
    // EXPECT_GT guards below are what keep that true on future runs.)

    // Diagnostic, not an assertion. When nonDefaultOrig fires, the question is
    // always "did the models arrive?", and a bare assertion failure cannot say.
    // The bike run lost an iteration to exactly this before the census existed.
    printf("[ChoosePoliceCarModel] compared=%d mismatch=%d nondefault=%d "
           "enforcer=%d fbi=%d army=%d distinct=%d\n",
           compared, mismatches, nonDefaultOrig,
           enforcerOrig, fbiOrig, armyOrig, distinctOrig);
    printf("[ChoosePoliceCarModel] loaded: swat=%d fbiped=%d armyped=%d "
           "enforcer=%d rhino=%d barracks=%d fbiranch=%d\n",
           (int)CStreaming::IsModelLoaded(MODEL_SWAT),
           (int)CStreaming::IsModelLoaded(MODEL_FBI),
           (int)CStreaming::IsModelLoaded(MODEL_ARMY),
           (int)CStreaming::IsModelLoaded(MODEL_ENFORCER),
           (int)CStreaming::IsModelLoaded(MODEL_RHINO),
           (int)CStreaming::IsModelLoaded(MODEL_BARRACKS),
           (int)CStreaming::IsModelLoaded(MODEL_FBIRANCH));
}
