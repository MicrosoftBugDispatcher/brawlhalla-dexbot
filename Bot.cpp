#include "Bot.h"
#include "Game.h"
#include "Globals.h"
#include <windows.h>
#include <timeapi.h>
#include <cstdio>
#include <cmath>
#include <atomic>

#pragma comment(lib, "winmm.lib")

namespace {

LARGE_INTEGER g_freq;
bool g_qpcReady = false;
void EnsureQPC() { if (!g_qpcReady) { QueryPerformanceFrequency(&g_freq); g_qpcReady = true; } }
uint64_t NowUs() {
    LARGE_INTEGER n;
    QueryPerformanceCounter(&n);
    return (uint64_t)((n.QuadPart * 1000000ULL) / (uint64_t)g_freq.QuadPart);
}

void SysKeyDown(int vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    SendInput(1, &in, sizeof(INPUT));
}
void SysKeyUp(int vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

bool g_shiftDown = false;
bool g_spaceDown = false;
bool g_sDown     = false;

void PressDodge()   { SysKeyDown(Globals::KeyDodge); g_shiftDown = true; }
void ReleaseDodge() { SysKeyUp  (Globals::KeyDodge); g_shiftDown = false; }
void PressJump()    { SysKeyDown(Globals::KeyJump);  g_spaceDown = true; }
void ReleaseJump()  { SysKeyUp  (Globals::KeyJump);  g_spaceDown = false; }
void PressDown()    { SysKeyDown(Globals::KeyDown);  g_sDown = true; }
void ReleaseDown()  { SysKeyUp  (Globals::KeyDown);  g_sDown = false; }

void ReleaseAll() {
    if (g_shiftDown) ReleaseDodge();
    if (g_spaceDown) ReleaseJump();
    if (g_sDown)     ReleaseDown();
}

// ═══════════════════════════════════════════════════════════
// Landing detection: game airborne flag + Y-stability
// ═══════════════════════════════════════════════════════════
struct GroundDetector {
    double lastY = 0.0;
    bool   hasLast = false;
    bool   wasAir = false;
    bool   onGround = false;
    uint64_t lastYChangeUs = 0;

    void Reset() {
        hasLast = false;
        wasAir = false;
        onGround = false;
        lastYChangeUs = 0;
    }

    // Returns true if just landed this tick
    bool Update(double y, bool gameAirborne, uint64_t now) {
        if (gameAirborne) {
            wasAir = true;
            onGround = false;
            lastY = y;
            hasLast = true;
            lastYChangeUs = now;
            return false;
        }

        if (!hasLast) {
            lastY = y; hasLast = true;
            lastYChangeUs = now;
            onGround = true;
            return !wasAir;
        }

        const double dy = std::fabs(y - lastY);
        lastY = y;
        if (dy > 0.5) lastYChangeUs = now;

        // Game flag says ground + Y stable for >= 3ms → truly landed
        if (!onGround && (now - lastYChangeUs) > 3000ULL) {
            onGround = true;
            bool justLanded = wasAir;
            wasAir = false;
            return justLanded;
        }
        return false;
    }

    bool IsOnGround() const { return onGround; }
};

GroundDetector g_ground;

// ═══════════════════════════════════════════════════════════
// Stage machine — timing-driven, matches manual pattern
// ═══════════════════════════════════════════════════════════
enum class Stage {
    IDLE,           // yerde, Dodge bas
    DODGE_DOWN,     // Dodge basıldı, 15ms bekle
    JUMP_DOWN,      // Jump basıldı, 60ms bekle
    JUMP_RELEASED,  // Jump bırakıldı, 30ms bekle
    FALLING         // S basılı, yere inmeyi bekle
};

struct Hop {
    Stage stage = Stage::IDLE;
    uint64_t tStage = 0;
    uint64_t tLastLanding = 0;
    int hopCount = 0;
};
Hop g_hop;

HANDLE g_worker = nullptr;
std::atomic<bool> g_run{false};

int GetUserDir() {
    const bool l = (GetAsyncKeyState(Globals::KeyLeft)  & 0x8000) != 0;
    const bool r = (GetAsyncKeyState(Globals::KeyRight) & 0x8000) != 0;
    if (l && !r) return -1;
    if (r && !l) return  1;
    return 0;
}

bool ReadLocal(Game::Entity& out) {
    auto ents = Game::Entities();
    for (const auto& e : ents) {
        if (e.isLocal && e.hasY) { out = e; return true; }
    }
    return false;
}

void Tick() {
    if (!Globals::EnableBunnyhop) {
        if (g_hop.stage != Stage::IDLE) { ReleaseAll(); g_hop.stage = Stage::IDLE; }
        g_ground.Reset();
        return;
    }

    const uint64_t now = NowUs();

    Game::Entity local;
    if (!ReadLocal(local)) return;

    bool justLanded = g_ground.Update(local.y, local.airborne, now);
    bool onGround   = g_ground.IsOnGround();

    const int dir = GetUserDir();
    if (dir == 0) {
        if (g_hop.stage != Stage::IDLE) { ReleaseAll(); g_hop.stage = Stage::IDLE; }
        return;
    }

    // Kullanıcının kayıtlarından çıkan timing'ler
    const uint64_t DODGE_TO_JUMP_US = (uint64_t)Globals::BunnyhopDodgeToJumpMs * 1000ULL;
    const uint64_t JUMP_HOLD_US     = (uint64_t)Globals::BunnyhopJumpHoldMs     * 1000ULL;
    const uint64_t JUMP_TO_FF_US    = (uint64_t)Globals::BunnyhopJumpToFallMs   * 1000ULL;

    switch (g_hop.stage) {

    // ─────────────────────────────────────────────────────
    // IDLE → Dodge bas (yere inince ANINDA, bekleme yok)
    // ─────────────────────────────────────────────────────
    case Stage::IDLE:
        if (!onGround && !justLanded) break;

        PressDodge();
        g_hop.tStage = now;
        g_hop.stage = Stage::DODGE_DOWN;
        break;

    // ─────────────────────────────────────────────────────
    // DODGE_DOWN → 15ms sonra Jump bas
    // ─────────────────────────────────────────────────────
    case Stage::DODGE_DOWN:
        if (now - g_hop.tStage >= DODGE_TO_JUMP_US) {
            PressJump();
            g_hop.tStage = now;
            g_hop.stage = Stage::JUMP_DOWN;
        }
        break;

    // ─────────────────────────────────────────────────────
    // JUMP_DOWN → 60ms sonra Jump + Dodge bırak
    // ─────────────────────────────────────────────────────
    case Stage::JUMP_DOWN:
        if (now - g_hop.tStage >= JUMP_HOLD_US) {
            ReleaseJump();
            ReleaseDodge();
            g_hop.tStage = now;
            g_hop.stage = Stage::JUMP_RELEASED;
        }
        break;

    // ─────────────────────────────────────────────────────
    // JUMP_RELEASED → 30ms sonra S bas (peak noktası)
    // ⚡ BURADA AIRBORNE FLAG BEKLENMİYOR — TIMING İLE
    // ─────────────────────────────────────────────────────
    case Stage::JUMP_RELEASED:
        if (now - g_hop.tStage >= JUMP_TO_FF_US) {
            PressDown();
            g_hop.tStage = now;
            g_hop.stage = Stage::FALLING;
        }
        break;

    // ─────────────────────────────────────────────────────
    // FALLING → S basılı tut, yere inince ANINDA bırak
    // ─────────────────────────────────────────────────────
    case Stage::FALLING:
        if (justLanded) {
            ReleaseDown();
            g_hop.tLastLanding = now;
            g_hop.hopCount++;
            g_hop.stage = Stage::IDLE;   // sonraki tick'te ANINDA yeni hop
        } else if (now - g_hop.tStage > 3000000ULL) {
            ReleaseAll();
            g_hop.tLastLanding = now;
            g_hop.stage = Stage::IDLE;
        }
        break;
    }
}

DWORD WINAPI WorkerProc(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    EnsureQPC();
    timeBeginPeriod(1);

    while (g_run.load(std::memory_order_relaxed)) {
        Tick();
        Sleep(1);
    }

    timeEndPeriod(1);
    ReleaseAll();
    return 0;
}

} // namespace

void Bot::Start() {
    if (g_worker) return;
    EnsureQPC();
    g_run.store(true, std::memory_order_relaxed);
    g_worker = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
}

void Bot::Stop() {
    if (!g_worker) return;
    g_run.store(false, std::memory_order_relaxed);
    WaitForSingleObject(g_worker, 2000);
    CloseHandle(g_worker);
    g_worker = nullptr;
}

void Bot::Update() {
    if (!Globals::BunnyhopDebug) return;
    static DWORD lastLog = 0;
    DWORD now = GetTickCount();
    if (now - lastLog < 200) return;
    lastLog = now;

    const char* stageName = "?";
    switch (g_hop.stage) {
        case Stage::IDLE:          stageName = "IDLE";       break;
        case Stage::DODGE_DOWN:    stageName = "DODGE_DN";   break;
        case Stage::JUMP_DOWN:     stageName = "JUMP_DN";    break;
        case Stage::JUMP_RELEASED: stageName = "JUMP_UP";    break;
        case Stage::FALLING:       stageName = "FALLING";    break;
    }
    const int dir = GetUserDir();
    const char* dirStr = (dir == 1) ? "R" : (dir == -1) ? "L" : "-";

    printf("[BUNNY] %-9s hops=%d dir=%s sh=%d sp=%d s=%d ground=%d\n",
           stageName, g_hop.hopCount, dirStr,
           g_shiftDown ? 1 : 0, g_spaceDown ? 1 : 0, g_sDown ? 1 : 0,
           g_ground.IsOnGround() ? 1 : 0);
}
