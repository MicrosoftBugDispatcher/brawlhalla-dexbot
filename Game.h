#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Game {

struct Entity {
    uintptr_t address = 0;
    bool isLocal = false;
    uint32_t seat = 0;
    uint32_t team = 0;
    uint32_t entityWord = 0;
    double damage = 0.0;
    bool damageKnown = false;
    double x = 0.0;
    double y = 0.0;
    bool hasX = false;
    bool hasY = false;
    double velX = 0.0;
    double velY = 0.0;
    bool hasVel = false;
    bool hasVelX = false;        // YENİ
    bool hasVelY = false;        // YENİ
    uint32_t keyVxNode = 0;      // YENİ: raw u32 @ 0x15C
    uint32_t keyVyNode = 0;      // YENİ: raw u32 @ 0x234
    uint32_t state = 0;
    bool stateKnown = false;
    uint32_t deaths = 0;
    bool deathsKnown = false;
    bool airborne = false;
    bool fastFalling = false;
    bool facingLeft = false;
    bool dodge = false;
    bool dodgeKnown = false;
    bool wallSliding = false;
    bool wallKnown = false;
    bool stunned = false;
    bool stunKnown = false;
    bool intangible = false;
    bool intanKnown = false;
    uint32_t airJumps = 0;
    bool airJumpsKnown = false;
    int weaponId = 0;
    bool weaponKnown = false;
    bool weaponIsWeapon = false;
    std::string weaponName;

    // ═══════════════════════════════════════════════════════
    // ⚡ BUNNYHOP STATE ALANLARI (YENİ)
    // ═══════════════════════════════════════════════════════
    bool wasOnGroundThisFrame = false;   // E_WAS_ON_GROUND_THIS_FRAME (0x0A8)
    bool didLand              = false;   // E_DID_LAND (0x0F0)
    bool didDashJump          = false;   // E_DID_DASH_JUMP (0x0DC)
    bool didDashFlip          = false;   // E_DASH_FACING_FLIP (0x0D8)
    uint32_t dashId           = 0;       // E_DASH_ID (0x294)
    uint32_t airJumpCounter   = 0;       // E_AIR_JUMP_COUNTER (0x2A4)
    bool bunnyFieldsKnown     = false;   // Herhangi biri okunabildi mi
};

struct Item {
    uintptr_t address = 0;
    int id = 0;
    bool isWeapon = false;
    bool held = false;
    uint32_t owner = 0;
    double x = 0.0;
    double y = 0.0;
};

struct Camera {
    bool valid = false;
    double camX = 0.0;
    double camY = 0.0;
    double zoomX = 1.0;
    double zoomY = 1.0;
};

struct Debug {
    uintptr_t local = 0;
    uintptr_t roster = 0;
    uint32_t rosterLen = 0;
    uint32_t rosterOk = 0;
    uintptr_t itemMgr = 0;
    uintptr_t itemVec = 0;
    uint32_t itemLen = 0;
    uint32_t itemOk = 0;
    uintptr_t camera = 0;
    uint32_t ground = 0;
    uint32_t held = 0;
    uint32_t rot = 0;
    bool rotKnown = false;
    uint32_t keyVx = 0;          // YENİ
    uint32_t keyVy = 0;          // YENİ
    bool keyVxOk = false;        // YENİ
    bool keyVyOk = false;        // YENİ
    double velX = 0.0;           // YENİ
    double velY = 0.0;           // YENİ
    bool velOk = false;          // YENİ
    uintptr_t levelMgr = 0;      // MAP: Level Manager pointer
    int mapId    = 0;            // MAP: LOM_LEVEL_ID  (0xA0)
    int mapIdAlt = 0;            // MAP: LT_LEVEL_ID   ([0xD0]+0x80)
};

// ═══════════════════════════════════════════════════════════
// 🗺️ MAP / LEVEL
// ═══════════════════════════════════════════════════════════
struct ArenaInfo {
    bool   valid     = false;
    int    levelId   = 0;
    int    levelType = 0;
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    std::string levelName;
};

void InitHooks();
void Shutdown();

bool Ready();
uintptr_t GameType();
uintptr_t LocalEntity();
std::vector<Entity> Entities();
std::vector<Item> Items();
Camera ReadCamera(double screenW, double screenH);
Debug Snapshot();

// ═══════════════════════════════════════════════════════════
// 🎮 GINPUT — oyunun input mask'ine direkt yaz
// ═══════════════════════════════════════════════════════════
uintptr_t InputMaskAddr();          // [game+0x610]+0x388]+0x70]+0x48
uint32_t  ReadInputMask();
bool      WriteInputMask(uint32_t bits);

ArenaInfo ReadArena();

}
