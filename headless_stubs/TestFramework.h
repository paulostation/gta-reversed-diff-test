// TestFramework.h — Lightweight in-process test framework for gta-reversed.
//
// Tests run inside the game process at state 9 with full access to game state.
// Triggered by GAME_TEST_ENABLE=1 environment variable.
//
// Usage:
//   GAME_TEST(CVector, Normalise_UnitLength) {
//       CVector v(3.0f, 4.0f, 0.0f);
//       v.Normalise();
//       EXPECT_NEAR(v.Magnitude(), 1.0f, 1e-5f);
//   }

#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <tlhelp32.h>

// ---------------------------------------------------------------------------
// Test registry
// ---------------------------------------------------------------------------

struct GameTestEntry {
    const char* className;
    const char* testName;
    void (*fn)();
};

struct GameTestRegistry {
    static std::vector<GameTestEntry>& Tests() {
        static std::vector<GameTestEntry> tests;
        return tests;
    }
};

struct GameTestRegistrar {
    GameTestRegistrar(const char* cls, const char* name, void (*fn)()) {
        GameTestRegistry::Tests().push_back({ cls, name, fn });
    }
};

// ---------------------------------------------------------------------------
// Test macros
// ---------------------------------------------------------------------------

#define GAME_TEST(Class, Name) \
    static void GameTest_##Class##_##Name(); \
    static GameTestRegistrar g_reg_##Class##_##Name(#Class, #Name, GameTest_##Class##_##Name); \
    static void GameTest_##Class##_##Name()

// ---------------------------------------------------------------------------
// Test result tracking (per-test)
// ---------------------------------------------------------------------------

struct GameTestContext {
    const char* className;
    const char* testName;
    int assertions;
    int failures;
    char firstFailure[512];

    // Non-failing diagnostic channel. printf does NOT reach
    // game_test_results.txt, so before this existed the only way to report a
    // coverage counter was to fake a failure with RecordFailure and delete the
    // scaffold afterwards (the bike run and the ChoosePoliceCarModel run both
    // did exactly that). A test that cannot say what it covered cannot be shown
    // to be non-vacuous, which is the whole point of ADR 0002 -- so the channel
    // is permanent rather than per-test scaffolding.
    //
    // Appended to, not overwritten: a test may report several facts, and the
    // one that gets truncated should be the last, not the first.
    char info[1024];
    int  infoLen;

    void Reset(const char* cls, const char* name) {
        className = cls;
        testName = name;
        assertions = 0;
        failures = 0;
        firstFailure[0] = '\0';
        info[0] = '\0';
        infoLen = 0;
    }

    // Record a diagnostic that reaches the results file WITHOUT failing the
    // test. Use for coverage counters and census data -- the evidence that a
    // green means something.
    void RecordInfo(const char* msg) {
        if (infoLen < 0 || infoLen >= (int)sizeof(info) - 1) {
            return;   // full; drop rather than truncate mid-token
        }
        const int n = _snprintf(info + infoLen, sizeof(info) - infoLen - 1,
                                "%s%s", infoLen ? " | " : "", msg);
        if (n > 0) {
            infoLen += n;
        }
        info[sizeof(info) - 1] = '\0';
    }

    void RecordFailure(const char* file, int line, const char* msg) {
        failures++;
        if (firstFailure[0] == '\0') {
            _snprintf(firstFailure, sizeof(firstFailure), "%s:%d: %s", file, line, msg);
            firstFailure[sizeof(firstFailure) - 1] = '\0';
        }
    }
};

// Global context — set by the test runner before each test
inline GameTestContext& GetTestContext() {
    static GameTestContext ctx{};
    return ctx;
}

// ---------------------------------------------------------------------------
// Assertion macros
// ---------------------------------------------------------------------------

// Report a diagnostic that reaches game_test_results.txt without failing the
// test. Printf-style, so counters can be formatted inline:
//   RECORD_INFO("compared=%d bikes=%d", compared, bikes);
#define RECORD_INFO(...) do { \
    char _info[512]; \
    _snprintf(_info, sizeof(_info), __VA_ARGS__); \
    _info[sizeof(_info) - 1] = '\0'; \
    GetTestContext().RecordInfo(_info); \
} while(0)

#define EXPECT_TRUE(expr) do { \
    GetTestContext().assertions++; \
    if (!(expr)) { \
        char _msg[256]; \
        _snprintf(_msg, sizeof(_msg), "EXPECT_TRUE(%s) failed", #expr); \
        GetTestContext().RecordFailure(__FILE__, __LINE__, _msg); \
    } \
} while(0)

#define EXPECT_FALSE(expr) do { \
    GetTestContext().assertions++; \
    if ((expr)) { \
        char _msg[256]; \
        _snprintf(_msg, sizeof(_msg), "EXPECT_FALSE(%s) failed", #expr); \
        GetTestContext().RecordFailure(__FILE__, __LINE__, _msg); \
    } \
} while(0)

#define EXPECT_EQ(a, b) do { \
    GetTestContext().assertions++; \
    if ((a) != (b)) { \
        char _msg[256]; \
        _snprintf(_msg, sizeof(_msg), "EXPECT_EQ(%s, %s) failed", #a, #b); \
        GetTestContext().RecordFailure(__FILE__, __LINE__, _msg); \
    } \
} while(0)

#define EXPECT_NE(a, b) do { \
    GetTestContext().assertions++; \
    if ((a) == (b)) { \
        char _msg[256]; \
        _snprintf(_msg, sizeof(_msg), "EXPECT_NE(%s, %s) failed", #a, #b); \
        GetTestContext().RecordFailure(__FILE__, __LINE__, _msg); \
    } \
} while(0)

#define EXPECT_LT(a, b) do { \
    GetTestContext().assertions++; \
    auto _a = (a); auto _b = (b); \
    if (!(_a < _b)) { \
        char _msg[256]; \
        _snprintf(_msg, sizeof(_msg), "EXPECT_LT(%s, %s) failed: %g >= %g", #a, #b, (double)_a, (double)_b); \
        GetTestContext().RecordFailure(__FILE__, __LINE__, _msg); \
    } \
} while(0)

#define EXPECT_GT(a, b) do { \
    GetTestContext().assertions++; \
    auto _a = (a); auto _b = (b); \
    if (!(_a > _b)) { \
        char _msg[256]; \
        _snprintf(_msg, sizeof(_msg), "EXPECT_GT(%s, %s) failed: %g <= %g", #a, #b, (double)_a, (double)_b); \
        GetTestContext().RecordFailure(__FILE__, __LINE__, _msg); \
    } \
} while(0)

#define EXPECT_NEAR(a, b, eps) do { \
    GetTestContext().assertions++; \
    auto _a = (a); auto _b = (b); auto _e = (eps); \
    if (std::fabs((double)_a - (double)_b) > (double)_e) { \
        char _msg[256]; \
        _snprintf(_msg, sizeof(_msg), "EXPECT_NEAR(%s, %s, %s) failed: |%g - %g| = %g > %g", \
            #a, #b, #eps, (double)_a, (double)_b, std::fabs((double)_a - (double)_b), (double)_e); \
        GetTestContext().RecordFailure(__FILE__, __LINE__, _msg); \
    } \
} while(0)

// Vector comparison helper
#define EXPECT_VEC_NEAR(vec, expected, eps) do { \
    EXPECT_NEAR((vec).x, (expected).x, eps); \
    EXPECT_NEAR((vec).y, (expected).y, eps); \
    EXPECT_NEAR((vec).z, (expected).z, eps); \
} while(0)

// ---------------------------------------------------------------------------
// Background thread suspension — ensures stable state during diff tests
// ---------------------------------------------------------------------------

// Suspend all threads in this process except the current one.
// Prevents CdStream worker and other background threads from modifying
// game state between original and reversed calls.
struct SuspendOtherThreads {
    std::vector<HANDLE> suspended;

    SuspendOtherThreads() {
        DWORD pid = GetCurrentProcessId();
        DWORD tid = GetCurrentThreadId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;

        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != tid) {
                    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                    if (h) {
                        SuspendThread(h);
                        suspended.push_back(h);
                    }
                }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }

    ~SuspendOtherThreads() {
        for (HANDLE h : suspended) {
            ResumeThread(h);
            CloseHandle(h);
        }
    }

    SuspendOtherThreads(const SuspendOtherThreads&) = delete;
    SuspendOtherThreads& operator=(const SuspendOtherThreads&) = delete;
};

// Inverse of SuspendOtherThreads — resumes all threads so that CdStream I/O
// can complete. Re-suspends on destruction. Used by streaming tests that need
// the CdStream worker thread alive inside the SuspendOtherThreads scope.
struct ResumeOtherThreads {
    std::vector<HANDLE> threads;

    ResumeOtherThreads() {
        DWORD pid = GetCurrentProcessId();
        DWORD tid = GetCurrentThreadId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid && te.th32ThreadID != tid) {
                    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                    if (h) { ResumeThread(h); threads.push_back(h); }
                }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }

    ~ResumeOtherThreads() {
        for (HANDLE h : threads) { SuspendThread(h); CloseHandle(h); }
    }

    ResumeOtherThreads(const ResumeOtherThreads&) = delete;
    ResumeOtherThreads& operator=(const ResumeOtherThreads&) = delete;
};

// ---------------------------------------------------------------------------
// Differential test helpers — compare reversed vs original code
// ---------------------------------------------------------------------------

#include "reversiblehooks/ReversibleHooks.h"
#include "reversiblehooks/HookCategory.h"      // HookCategory::Item, FindItem
#include "reversiblehooks/RootHookCategory.h"  // GetRootCategory() returns this; needs the full type
#include "reversiblehooks/ReversibleHook/Base.h" // Hooked(), Locked(), State()
#include "toolsmenu/Utility.h"                 // SplitStringView

// Toggle a single hook on/off by its full path (e.g., "Global/CGeneral/LimitAngle").
// Returns true if the toggle succeeded.
inline bool SetHookEnabled(const char* path, bool enabled) {
    auto r = ReversibleHooks::SetCategoryOrItemStateByPath(path, enabled);
    return r == ReversibleHooks::SetCatOrItemStateResult::Done;
}

// Resolve a hook path to the item itself.
//
// SetCategoryOrItemStateByPath collapses several distinct outcomes into the
// single result `Locked`, which made a real failure unreadable: run on
// 2026-08-12 reported "hook 'Global/CPostEffects/SpeedFX' is locked" when the
// install line carries no `.locked` at all. Going through the item directly
// lets the guard state what is actually true -- locked, missing, or already in
// the wanted state -- instead of guessing.
inline ReversibleHooks::HookCategory::Item FindItemByPath(const char* path) {
    std::string_view p{path};
    if (p.ends_with("/")) {
        p.remove_suffix(1);
    }
    const auto parts = SplitStringView(p, "/") | rng::to<std::vector>();
    if (parts.empty()) {
        return nullptr;
    }
    ReversibleHooks::HookCategory* cat = &ReversibleHooks::GetRootCategory();
    for (auto name : std::span(parts).first(parts.size() - 1)) {
        cat = cat->FindSubcategory(name);
        if (!cat) {
            return nullptr;
        }
    }
    return cat->FindItem(parts.back());
}

// RAII guard: runs the ORIGINAL code for the duration of its scope, then puts
// the hook back exactly as it found it.
//
// Why it does not just call SetCategoryOrItemStateByPath: the test harness
// launches under `--unhook-except=Global/CLoadingScreen`, which dllmain.cpp
// implements as SetAllItemsEnabled(false) -- every hook off. A differential
// test therefore starts with its own hook DISABLED, and must enable it to have
// any reversed code to compare against. Enabling a whole category instead is
// not an option: the sibling hooks are disabled precisely because they crash
// during init under Wine (see run-headless.sh).
//
// So: toggle exactly one item, restore exactly that item.
struct HookDisableGuard {
    const char* path;
    bool needsRestore; // true if we need to re-enable on destruction
    bool valid;        // true if the hook path was found

    bool wasHooked;    // state on entry, so we restore rather than assume

    explicit HookDisableGuard(const char* hookPath)
        : path(hookPath), needsRestore(false), valid(false), wasHooked(false)
    {
        auto item = FindItemByPath(path);
        if (!item) {
            char msg[256];
            _snprintf(msg, sizeof(msg), "HookDisableGuard: hook '%s' not found", path);
            GetTestContext().RecordFailure(__FILE__, __LINE__, msg);
            return;
        }
        if (item->Locked()) {
            // Report what is actually true rather than naming a cause. The
            // install line for SpeedFX carries no `.locked`, so if this fires
            // the lock is coming from somewhere else and the flags say where.
            // name= confirms WHICH item resolved: FindItem matches on name
            // alone, so a collision would silently return the wrong hook.
            char msg[256];
            _snprintf(msg, sizeof(msg),
                      "HookDisableGuard: '%s' -> name='%s' sym=%s locked=%d hooked=%d",
                      path, item->Name().c_str(), item->Symbol(),
                      (int)item->Locked(), (int)item->Hooked());
            GetTestContext().RecordFailure(__FILE__, __LINE__, msg);
            return;
        }

        wasHooked = item->Hooked();

        // The reversed code must be installed, or there is nothing to compare
        // against -- both sides of the diff would run the original and agree
        // vacuously. Enable it if --unhook-except turned it off.
        if (!item->Hooked()) {
            item->State(true);
            if (!item->Hooked()) {
                char msg[256];
                _snprintf(msg, sizeof(msg),
                          "HookDisableGuard: hook '%s' would not enable (Switch() did not take)", path);
                GetTestContext().RecordFailure(__FILE__, __LINE__, msg);
                return;
            }
        }
        valid = true;

        // Now switch to the ORIGINAL for the body of the scope.
        item->State(false);

        // VERIFY IT TOOK. Enabling is checked above; this switch was not, and
        // an unverified unhook is how a differential test compares the reversed
        // code against ITSELF and passes. Measured 2026-08-13: an off-by-one in
        // CStreamingInfo::SetCdPosnAndSize was compiled into the ASI and
        // Diff_SetAndGetCdPosnAndSize passed anyway, with 8 assertions and two
        // independent checks that should each have caught it.
        //
        // valid stays false on failure, so callers that honour it skip rather
        // than record a meaningless comparison.
        if (item->Hooked()) {
            char msg[256];
            _snprintf(msg, sizeof(msg),
                      "HookDisableGuard: '%s' unhook did not take -- still hooked "
                      "(locked=%d wasHooked=%d). The 'original' call would run "
                      "REVERSED code and the comparison would be vacuous.",
                      path, (int)item->Locked(), (int)wasHooked);
            GetTestContext().RecordFailure(__FILE__, __LINE__, msg);
            valid = false;
            // Still restore: we changed state, so the destructor must undo it.
            needsRestore = true;
            return;
        }

        needsRestore = true;
    }

    ~HookDisableGuard() {
        if (needsRestore) {
            if (auto item = FindItemByPath(path)) {
                item->State(wasHooked);   // restore what we found, not what we assume
            }
        }
    }

    HookDisableGuard(const HookDisableGuard&) = delete;
    HookDisableGuard& operator=(const HookDisableGuard&) = delete;
};

// Call a function with its hook disabled (original game code), then re-enable.
// Usage: float orig = CallOriginal("Global/CGeneral/LimitAngle", CGeneral::LimitAngle, 90.0f);
template<typename Fn, typename... Args>
auto CallOriginal(const char* hookPath, Fn fn, Args&&... args) -> decltype(fn(std::forward<Args>(args)...)) {
    HookDisableGuard guard(hookPath);
    return fn(std::forward<Args>(args)...);
}

// Compare original vs reversed for a function that returns a value.
// Asserts they match within epsilon.
#define EXPECT_MATCHES_ORIGINAL(hookPath, fn, eps, ...) do { \
    auto _orig = CallOriginal(hookPath, fn, __VA_ARGS__); \
    auto _rev  = fn(__VA_ARGS__); \
    EXPECT_NEAR(_orig, _rev, eps); \
} while(0)

// Differential test macro — same as GAME_TEST but documents intent
#define GAME_DIFF_TEST(Class, Name) GAME_TEST(Class, Diff_##Name)

// ---------------------------------------------------------------------------
// Dual-CRT rand() seeding
// ---------------------------------------------------------------------------
//
// The exe has its own statically-linked CRT with a separate rand() state at
// 0x821B1E.  The DLL's srand() only seeds the DLL's CRT.  For differential
// tests that compare original (exe) code against reversed (DLL) code, we must
// seed BOTH CRTs so the same random sequence is produced.
//
// Exe's srand is at 0x821B11:
//   call __getptd
//   mov  ecx, [esp+4]
//   mov  [eax+0x14], ecx
//   ret

inline void ExeSrand(unsigned int seed) {
    using SrandFn = void(__cdecl*)(unsigned int);
    static auto fn = reinterpret_cast<SrandFn>(0x821B11);
    fn(seed);
}

// Seed both exe and DLL CRT rand states.
inline void SeedBothRng(unsigned int seed) {
    srand(seed);
    ExeSrand(seed);
}

// ---------------------------------------------------------------------------
// Direct-call helpers for struct-returning functions (sret workaround)
// ---------------------------------------------------------------------------
//
// HookDisableGuard doesn't work for functions returning structs > 4 bytes
// (CVector, CVector2D, etc.) due to MSVC x86 sret calling convention issues.
// Instead, call the original function directly at its game address using
// inline asm with explicit sret pointer setup.
//
// These helpers encapsulate the asm patterns so test files don't need to
// hand-roll inline assembly.

// Call a __cdecl function at `addr` that returns CVector2D via sret.
// Pattern: push args right-to-left, push sret pointer, call, add esp.
inline CVector2D CallOriginal_CVector2D(uint32 addr, const CVector2D& arg1) {
    CVector2D result = {};
    const CVector2D* pIn = &arg1;
    CVector2D* pOut = &result;
    __asm {
        mov eax, pIn
        push eax
        mov eax, pOut
        push eax
        mov eax, addr
        call eax
        add esp, 8
    }
    return result;
}

inline CVector2D CallOriginal_CVector2D(uint32 addr, const CVector2D& arg1, int32 arg2, int32 arg3) {
    CVector2D result = {};
    const CVector2D* pIn = &arg1;
    CVector2D* pOut = &result;
    __asm {
        push arg3
        push arg2
        mov eax, pIn
        push eax
        mov eax, pOut
        push eax
        mov eax, addr
        call eax
        add esp, 16
    }
    return result;
}

// Call a __thiscall function at `addr` that returns CVector via sret.
// MSVC thiscall sret: ecx = this, first stack arg = sret pointer, then visible args.
inline CVector CallOriginal_CVector_Thiscall(uint32 addr, void* thisPtr, int32 arg1) {
    CVector result = {};
    CVector* pOut = &result;
    __asm {
        push arg1
        mov eax, pOut
        push eax
        mov ecx, thisPtr
        mov eax, addr
        call eax
    }
    return result;
}

// Call a __thiscall function with CVector* output param (not sret — regular thiscall).
// void __thiscall Fn(this, CVector* out, int32 arg1, bool arg2)
inline void CallOriginal_OutVec_Thiscall(uint32 addr, void* thisPtr, CVector* out, int32 arg1, bool arg2) {
    int32 bArg = arg2 ? 1 : 0;
    __asm {
        push bArg
        push arg1
        push out
        mov ecx, thisPtr
        mov eax, addr
        call eax
    }
}
