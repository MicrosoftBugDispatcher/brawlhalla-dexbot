#pragma once
#include <cstdint>
#include <string>

namespace Combo {

struct DebugInfo {
    bool     enabled;
    bool     hasInputAddr;
    uintptr_t inputAddr;
    uint32_t botDirBits;
    uint32_t botAtkBits;
    uint32_t userBits;
    uint32_t actualMask;

    int  stage;
    int  stageUs;              // şu anki state'te geçen µs
    int  targetAttack;         // basılacak (pending) saldırı
    int  lastAttack;
    int  comboDepth;
    int  hits;

    // ─── Local player ───
    bool  hasLocal;
    int   weaponId;
    std::string weaponName;
    int   kit;                     // 0=none 1=hammer 2=scythe 3=gauntlets
    double meX, meY;
    bool   meIsAir;
    double meVelX, meVelY;

    // ─── Enemy ───
    bool  hasEnemy;
    uintptr_t enemyAddress;
    double enemyX, enemyY;
    double enemyVelX, enemyVelY;   // smoothed
    bool   enemyHasVel;
    double enemyPredX, enemyPredY; // predicted
    float  enemyDist;              // current
    float  enemyPredDist;          // predicted
    bool   enemyAir, enemyStun, enemyDodge, enemyIntangible, enemyFastFall;
    uint32_t enemyAirJumps;

    // Hit tracker
    int consecutiveHits;
    int consecutiveMisses;
    bool pendingAttack;
    int  whiffMs;              // kalan whiff-block ms (0 = yok)
    int  nextLink;             // planlanan bir sonraki chain saldırısı

    // ─── Map ───
    int    levelId;
    int    levelType;
    bool   arenaValid;
    double mapX0, mapX1;
    std::string levelName;

    // Karar sebebi
    const char* decision;  // "NLight", "DLight", "SAir", "OUT OF RANGE", etc.
    bool facingOK;
    bool inRange;

    DebugInfo();
};

DebugInfo GetDebug();

uintptr_t FocusAddress();   // combo hedefinin entity adresi (0 = yok)

void Start();
void Stop();
void Update();

}
