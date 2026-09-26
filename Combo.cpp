#include "Combo.h"
#include "Game.h"
#include "Globals.h"
#include <windows.h>
#include <timeapi.h>
#include <cstdio>
#include <cstring>
#include <cctype>
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

// ═══════════════════════════════════════════════════════════
// Bits
// ═══════════════════════════════════════════════════════════
constexpr uint32_t BIT_S        = 0x002;
constexpr uint32_t BIT_A        = 0x004;
constexpr uint32_t BIT_D        = 0x008;
constexpr uint32_t BIT_W        = 0x011;
constexpr uint32_t BIT_X        = 0x040;
constexpr uint32_t BIT_Z        = 0x100;
constexpr uint32_t BIT_C        = 0x280;
constexpr uint32_t CONTROL_MASK = 0x3DF;

constexpr uint32_t DIR_MASK = BIT_A | BIT_D | BIT_W | BIT_S;
constexpr uint32_t ATK_MASK = BIT_C | BIT_X;   // ⚡ artık heavy de bot'un

// ═══════════════════════════════════════════════════════════
// State
// ═══════════════════════════════════════════════════════════
uint32_t g_botDirBits = 0;
uint32_t g_botAtkBits = 0;
uint32_t g_userBits   = 0;
uint32_t g_lastWritten = 0;

uint32_t ReadUserBits() {
    uint32_t b = 0;
    if (GetAsyncKeyState(Globals::KeyLeft)  & 0x8000) b |= BIT_A;
    if (GetAsyncKeyState(Globals::KeyRight) & 0x8000) b |= BIT_D;
    if (GetAsyncKeyState(Globals::KeyUp)    & 0x8000) b |= BIT_W;
    if (GetAsyncKeyState(Globals::KeyDown)  & 0x8000) b |= BIT_S;
    if (GetAsyncKeyState(Globals::KeyLight) & 0x8000) b |= BIT_C;
    if (GetAsyncKeyState(Globals::KeyHeavy) & 0x8000) b |= BIT_X;
    return b & CONTROL_MASK;
}

void ApplyBits() {
    uint32_t userBits = ReadUserBits();
    g_userBits = userBits;
    uint32_t userDir = userBits & DIR_MASK;
    uint32_t userAtk = userBits & ATK_MASK;

    uint32_t atkFinal = userAtk ? userAtk : g_botAtkBits;
    uint32_t dirFinal = (userDir != 0) ? userDir : g_botDirBits;
    uint32_t final = (dirFinal | atkFinal) & CONTROL_MASK;

    uint32_t cur = Game::ReadInputMask();
    cur &= ~CONTROL_MASK;
    cur |= final;
    Game::WriteInputMask(cur);
    g_lastWritten = final;
}

void ClearBotBits() { g_botDirBits = 0; g_botAtkBits = 0; }
void ClearAll()     { ClearBotBits(); ApplyBits(); }

// ═══════════════════════════════════════════════════════════
// Velocity tracker — daha hızlı EMA
// ═══════════════════════════════════════════════════════════
struct EntTrack {
    uintptr_t addr = 0;
    double lastX = 0, lastY = 0;
    uint64_t lastT = 0;
    double smoothVx = 0, smoothVy = 0;
    double prevVy = 0;
    bool has = false;
    int stable = 0;
    uint64_t dodgeAt = 0;        // son rakip dodge başlangıcı (0 = yok)
};
EntTrack g_track[8];

EntTrack* trackFor(uintptr_t addr) {
    for (auto& t : g_track) if (t.addr == addr) return &t;
    for (auto& t : g_track) if (t.addr == 0) { t.addr = addr; return &t; }
    EntTrack* oldest = &g_track[0];
    for (auto& t : g_track) if (t.lastT < oldest->lastT) oldest = &t;
    *oldest = EntTrack();
    oldest->addr = addr;
    return oldest;
}

void UpdateTrack(EntTrack& t, double x, double y, uint64_t now) {
    if (!t.has) {
        t.lastX = x; t.lastY = y; t.lastT = now;
        t.has = true; t.stable = 1;
        return;
    }
    const uint64_t dt_us = now - t.lastT;
    if (dt_us < 2000) return;   // ⚡ 3ms → 2ms, daha sık sample
    const double dt = dt_us / 1e6;

    const double instVx = (x - t.lastX) / dt;
    const double instVy = (y - t.lastY) / dt;

    if (std::fabs(instVx) > 3000 || std::fabs(instVy) > 3000) {
        t.lastX = x; t.lastY = y; t.lastT = now;
        return;
    }

    // ⚡ Daha responsive EMA (0.5 eski + 0.5 yeni)
    const double a = 0.5;
    if (t.stable < 2) {
        t.smoothVx = instVx; t.smoothVy = instVy;
        t.stable++;
    } else {
        t.smoothVx = (1 - a) * t.smoothVx + a * instVx;
        t.prevVy   = t.smoothVy;
        t.smoothVy = (1 - a) * t.smoothVy + a * instVy;
    }

    t.lastX = x; t.lastY = y; t.lastT = now;
}

// ═══════════════════════════════════════════════════════════
// Hit tracker
// ═══════════════════════════════════════════════════════════
struct HitTracker {
    double lastDamage = 0.0;
    uint64_t lastAttackT = 0;
    uint64_t whiffUntil = 0;
    int consecutiveHits = 0;
    int consecutiveMisses = 0;
    bool pendingAttack = false;
};
HitTracker g_hit;

// ═══════════════════════════════════════════════════════════
// Attack enum
// ═══════════════════════════════════════════════════════════
enum class Attack { NONE, NLight, SLight, DLight, NAir, SAir, DAir, Recovery };

const char* AtkName(Attack a) {
    switch (a) {
        case Attack::NLight:   return "NLight";
        case Attack::SLight:   return "SLight";
        case Attack::DLight:   return "DLight";
        case Attack::NAir:     return "NAir";
        case Attack::SAir:     return "SAir";
        case Attack::DAir:     return "DAir";
        case Attack::Recovery: return "Recovery";
        default: return "-";
    }
}

struct AttackCmd {
    Attack   type    = Attack::NONE;
    int      dir     = 0;
    uint32_t atkBits = 0;
    uint32_t dirBits = 0;
    const char* why  = "-";
};

// ═══════════════════════════════════════════════════════════
// Yer/hava tespit
// ═══════════════════════════════════════════════════════════
bool IsAirborne(const Game::Entity& e) {
    if (e.didLand) return false;
    if (e.wasOnGroundThisFrame && !e.airborne) return false;
    if (e.airborne) return true;
    if (e.hasVelY && std::fabs(e.velY) > 30.0) return true;
    if (e.airJumpCounter > 0) return true;
    return false;
}

// ═══════════════════════════════════════════════════════════
// ⚡ Hitbox-aware karar
//    Rakip hurtbox ≈ 145 x 160 (merkez etrafında ±72, ±80)
//    Bizim attack hitbox menzilleri:
//      NAir: ~100   DAir: ~110   SAir: ~140
//      NLight yer: ~130   Recovery: ~130 (yukarı)
// ═══════════════════════════════════════════════════════════
constexpr double HURT_HALF_W   = 72.0;
constexpr double HURT_HALF_H   = 80.0;

// ═══════════════════════════════════════════════════════════
// 🌐 WEAPON KITS — her silahın kendi reach + chain tablosu
//    Hitbox-aware menziller: (reach + hurtbox yarısı → etkin)
//    Index'ler Attack enum ile aynı: 0=NONE 1=NLight 2=SLight
//    3=DLight 4=NAir 5=SAir 6=DAir 7=Recovery
// ═══════════════════════════════════════════════════════════
enum class KitId { NONE = 0, HAMMER = 1, SCYTHE = 2, GAUNTLETS = 3 };

struct WeaponKit {
    KitId        id;
    const char*  disp;
    double       reach[8];        // saldırı başına menzil
    const Attack* chain[8];       // prev attack -> aday link seti
    Attack       repeat;          // bu attack'ın hızlı tekrarına izin ver
};

constexpr size_t KAI(Attack a) { return (size_t)a; }

// ─── HAMMER (mevcut davranış korunur) ───
static constexpr Attack hN[]  = { Attack::DLight, Attack::NLight, Attack::SLight, Attack::NONE };
static constexpr Attack hS[]  = { Attack::DLight, Attack::SAir,   Attack::NLight, Attack::NONE };
static constexpr Attack hD[]  = { Attack::SAir,   Attack::NAir,   Attack::DAir,  Attack::DLight, Attack::NONE };
static constexpr Attack hnA[] = { Attack::DAir,   Attack::SAir,   Attack::NAir,  Attack::NONE };
static constexpr Attack hsA[] = { Attack::DAir,   Attack::SAir,   Attack::Recovery, Attack::NONE };
static constexpr Attack hdA[] = { Attack::SAir,   Attack::DAir,   Attack::NONE };
static constexpr Attack hR[]  = { Attack::NAir,   Attack::SAir,   Attack::NONE };

// ─── SCYTHE (NLight zincir delici + DLight kurulum) ───
static constexpr Attack sN[]  = { Attack::NLight, Attack::DLight, Attack::SAir,   Attack::NAir,   Attack::NONE };
static constexpr Attack sS[]  = { Attack::SAir,   Attack::DLight, Attack::NAir,   Attack::NONE };
static constexpr Attack sD[]  = { Attack::SAir,   Attack::NAir,   Attack::DAir,  Attack::Recovery, Attack::NONE };
static constexpr Attack snA[] = { Attack::SAir,   Attack::DAir,   Attack::NAir,  Attack::NONE };
static constexpr Attack ssA[] = { Attack::DAir,   Attack::Recovery, Attack::NONE };
static constexpr Attack sdA[] = { Attack::NAir,   Attack::SAir,   Attack::NONE };
static constexpr Attack sR[]  = { Attack::NAir,   Attack::SAir,   Attack::NONE };

// ─── GAUNTLETS (NLight jab + SLight/NAir loop) ───
static constexpr Attack gN[]  = { Attack::NLight, Attack::SLight, Attack::DLight, Attack::NAir, Attack::NONE };
static constexpr Attack gS[]  = { Attack::SAir,   Attack::DLight, Attack::NAir,   Attack::NONE };
static constexpr Attack gD[]  = { Attack::NAir,   Attack::SAir,   Attack::NONE };
static constexpr Attack gnA[] = { Attack::SAir,   Attack::NAir,   Attack::DAir,  Attack::NONE };
static constexpr Attack gsA[] = { Attack::DAir,   Attack::NONE };
static constexpr Attack gdA[] = { Attack::SAir,   Attack::NONE };
static constexpr Attack gR[]  = { Attack::NAir,   Attack::NONE };

static const WeaponKit kHammer = {
    KitId::HAMMER, "Hammer",
    { 0, 130, 150, 95, 100, 140, 110, 130 },
    { nullptr, hN, hS, hD, hnA, hsA, hdA, hR },
    Attack::NONE
};

static const WeaponKit kScythe = {
    KitId::SCYTHE, "Scythe",
    { 0, 120, 160, 140, 130, 165, 120, 160 },
    { nullptr, sN, sS, sD, snA, ssA, sdA, sR },
    Attack::NLight
};

static const WeaponKit kGauntlets = {
    KitId::GAUNTLETS, "Gauntlets",
    { 0, 100, 120, 110, 110, 135, 100, 120 },
    { nullptr, gN, gS, gD, gnA, gsA, gdA, gR },
    Attack::NLight
};

const WeaponKit* DefaultKit() { return &kHammer; }

double KitReach(const WeaponKit* k, Attack a) {
    if (!k) return 0.0;
    const size_t i = KAI(a);
    return (i < 8) ? k->reach[i] : 0.0;
}

const WeaponKit* g_kit = nullptr;

// Silahı ID **ve/veya** isimle tanı (isim, id bilinmese de çalışır)
const WeaponKit* KitFor(int weaponId, const std::string& name) {
    std::string up;
    up.reserve(name.size());
    for (char c : name) up.push_back((char)toupper((unsigned char)c));

    const WeaponKit* cand = nullptr;
    int best = 0;
    const auto test = [&](const WeaponKit* k, int idVal, const char* tag) {
        const int score = (idVal != 0 && weaponId == idVal) ? 2
                        : (!up.empty() && strstr(up.c_str(), tag)) ? 1 : 0;
        if (score > best) { best = score; cand = k; }
    };
    test(&kScythe,    Globals::ScytheWeaponId,   "SCYTHE");
    test(&kGauntlets, Globals::GauntletWeaponId, "GAUNTLET");
    test(&kHammer,    Globals::HammerWeaponId,   "HAMMER");
    return cand;
}

AttackCmd DecideAttack(const Game::Entity& me, const Game::Entity& enemy,
                       double ex, double ey,
                       const EntTrack& track, bool hasTrack)
{
    AttackCmd cmd;
    const WeaponKit* k = g_kit ? g_kit : DefaultKit();

    // Prediction — dodge sırasında kısa, havada stunned'ken yarı güç
    const double pT = Globals::HammerPredictionMs / 1000.0;
    const bool enemyDodging   = (enemy.dodgeKnown   && enemy.dodge);
    const bool enemyAirStunned = IsAirborne(enemy) && enemy.stunned;
    const double predT = enemyDodging   ? pT * 0.3
                       : enemyAirStunned ? pT * 0.75
                                         : pT;
    const double predX = ex + (hasTrack ? track.smoothVx * predT : 0.0);
    const double predY = ey + (hasTrack ? track.smoothVy * predT : 0.0);

    const double dx  = predX - me.x;
    const double dy  = predY - me.y;
    const double adx = std::fabs(dx);
    const double ady = std::fabs(dy);
    cmd.dir = (dx < 0) ? -1 : +1;

    const bool isAir = IsAirborne(me);
    const bool falling = me.hasVelY && me.velY < (double)Globals::HammerFallVelThresh;
    const bool rising  = me.hasVelY && me.velY > (double)Globals::HammerJumpVelThresh;

    const double aV  = Globals::HammerAirVertical;
    const double aS  = Globals::HammerAirSide;
    const double gR  = Globals::HammerGroundRange;
    const int    recovMin = Globals::HammerRecoveryMinDist;
    const int    recovMax = Globals::HammerRecoveryMaxDist;

    // Effective range = our reach + enemy hurtbox half width
    const double nAirR = KitReach(k, Attack::NAir) + HURT_HALF_W;
    const double dAirR = KitReach(k, Attack::DAir) + HURT_HALF_W;
    const double sAirR = KitReach(k, Attack::SAir) + HURT_HALF_W;
    const double nLgtR = KitReach(k, Attack::NLight) + HURT_HALF_W;
    const double recovR= KitReach(k, Attack::Recovery) + HURT_HALF_W;

    // ═════════════════════════════════════════════════════
    // HAVADA
    // ═════════════════════════════════════════════════════
    if (isAir) {

        // ⚡ PRIORITY 1: Rakip YUKARIDA veya AYNI HİZADA → NAir (düz C)
        //    Yatay menzildeyse; yükseklik bandı ±aV (fazla aşağıdakine
        //    NAir whiff olur — DAir'e bırak)
        if (dy > -aV && dy < aV && adx < nAirR) {
            cmd.type    = Attack::NAir;
            cmd.atkBits = BIT_C;
            cmd.dirBits = 0;
            cmd.why     = "air-NAir(above)";
            return cmd;
        }

        // ⚡ PRIORITY 2: Düşüyoruz + rakip yukarıda orta-uzak → Recovery (W+K)
        if (Globals::HammerUseRecovery && falling &&
            dy > recovMin && dy < recovMax && adx < recovR) {
            cmd.type    = Attack::Recovery;
            cmd.atkBits = BIT_X;
            cmd.dirBits = BIT_W;
            cmd.why     = "air-Recovery(falling)";
            return cmd;
        }

        // ⚡ PRIORITY 3: Rakip AŞAĞIDA → DAir (S+C)   [asenkron hata: dy < -aV
        //    "üstümüzde" demekti → aşağıya whiff basıyordu; düzeltildi]
        if (dy > aV && adx < dAirR) {
            cmd.type    = Attack::DAir;
            cmd.atkBits = BIT_C;
            cmd.dirBits = BIT_S;
            cmd.why     = "air-DAir(below)";
            return cmd;
        }

        // ⚡ PRIORITY 4: Rakip YANDA → SAir (yön+C)
        if (adx < aS && ady < 150) {
            cmd.type    = Attack::SAir;
            cmd.atkBits = BIT_C;
            cmd.dirBits = (cmd.dir < 0) ? BIT_A : BIT_D;
            cmd.why     = "air-SAir(side)";
            return cmd;
        }

        // ⚡ PRIORITY 5: Rising (zıplıyoruz) + rakip çok yukarıda → Recovery ile chase
        if (Globals::HammerUseRecovery && rising &&
            dy > 150 && ady < recovMax && adx < recovR) {
            cmd.type    = Attack::Recovery;
            cmd.atkBits = BIT_X;
            cmd.dirBits = BIT_W;
            cmd.why     = "air-Recovery(chase)";
            return cmd;
        }

        cmd.why = "air-far";
        return cmd;
    }

    // ═════════════════════════════════════════════════════
    // YERDE — DLight (string başlatıcı) + SLight + NLight
    // ═════════════════════════════════════════════════════
    const double dLgtR = KitReach(k, Attack::DLight) + HURT_HALF_W;
    const double sLgtR = KitReach(k, Attack::SLight) + HURT_HALF_W;

    // nokta atışı → hızlı NLight (DLight'ın startup'ı ağır)
    if (adx < 70 && ady < 90) {
        cmd.type    = Attack::NLight;
        cmd.atkBits = BIT_C;
        cmd.dirBits = 0;
        cmd.why     = "ground-NLight(close)";
        return cmd;
    }

    // DLight: yakın-orta, rakibi yukarı kaldırır (klasik hammer string)
    if (adx < dLgtR && dy < 60 && dy > -100) {
        cmd.type    = Attack::DLight;
        cmd.atkBits = BIT_C;
        cmd.dirBits = BIT_S;
        cmd.why     = "ground-DLight(launch)";
        return cmd;
    }

    // SLight: orta-menial yan sweep, yön gerekir
    if (adx < sLgtR && ady < 90) {
        cmd.type    = Attack::SLight;
        cmd.atkBits = BIT_C;
        cmd.dirBits = (cmd.dir < 0) ? BIT_A : BIT_D;
        cmd.why     = "ground-SLight(side)";
        return cmd;
    }

    // Düşüşte tutulmayı başlatabilen NLight fallback'i sadece rakip
    // ÜSTÜMÜZDE DEĞİLSE — launch edilmiş rakibe jab whiff olur
    if (adx < nLgtR && ady < 120 && dy > -30) {
        cmd.type    = Attack::NLight;
        cmd.atkBits = BIT_C;
        cmd.dirBits = 0;
        cmd.why     = "ground-plain-C";
    } else {
        cmd.why = "ground-far";
    }
    return cmd;
}

// ═══════════════════════════════════════════════════════════
// Combo chains — STATE-DRIVEN: her link, mevcut
// yer/hava + menzil durumuna göre en iyi saldırıyı seçer.
// Sabit tablo değil; düşman knockback ile kaydıysa whiff etmez.
// ═══════════════════════════════════════════════════════════

bool IsEnemyInvuln(const Game::Entity& e) {
    return (e.intanKnown && e.intangible) || (e.dodgeKnown && e.dodge);
}

bool AirOnly(Attack a) {
    return a == Attack::NAir || a == Attack::SAir || a == Attack::DAir || a == Attack::Recovery;
}
bool GndOnly(Attack a) {
    return a == Attack::NLight || a == Attack::SLight || a == Attack::DLight;
}

// Saldırı bu noktada rakibe ulaşıyor mu? (kit menzillerine göre)
bool InLinkReach(const WeaponKit* k, Attack a, double dx, double dy) {
    const double aadx = std::fabs(dx), aady = std::fabs(dy);
    const double r = KitReach(k, a);
    switch (a) {
        case Attack::NLight:   return aadx < r + HURT_HALF_W && aady < 120.0;
        case Attack::SLight:   return aadx < r + HURT_HALF_W && aady <  90.0;
        case Attack::DLight:   return aadx < r + HURT_HALF_W && dy <  60.0 && dy > -100.0;
        case Attack::NAir:     return aadx < r + HURT_HALF_W && dy > -50.0;
        case Attack::SAir:     return aadx < r + HURT_HALF_W && aady < 150.0;
        case Attack::DAir:     return aadx < r + HURT_HALF_W && dy <  50.0;
        case Attack::Recovery: return aadx < r + HURT_HALF_W && dy >  40.0;
        default:               return false;
    }
}

// Önceki vuruştan sonra, şu an geçerli olan en iyi link
Attack ChainLink(Attack prev, const Game::Entity& me, const Game::Entity& enemy) {
    const WeaponKit* k = g_kit ? g_kit : DefaultKit();
    const Attack* set  = k->chain[KAI(prev)];
    if (!set) return Attack::NONE;
    const double dx = enemy.x - me.x;
    const double dy = enemy.y - me.y;
    const bool amAir = IsAirborne(me);
    for (const Attack* it = set; *it != Attack::NONE; ++it) {
        const Attack a = *it;
        if (AirOnly(a) != amAir) continue;          // yer/hava durumuna uygun olmalı
        if (a == prev && a != k->repeat) continue;  // cooldown — repeat'ler hariç
        if (InLinkReach(k, a, dx, dy)) return a;
    }
    return Attack::NONE;
}

AttackCmd MakeChainCmd(Attack a, int towardSign) {
    AttackCmd c;
    c.type = a;
    c.why  = "chain";
    c.dir  = towardSign;
    const uint32_t side = (towardSign < 0) ? BIT_A : BIT_D;
    switch (a) {
        case Attack::NLight:   c.atkBits = BIT_C; c.dirBits = 0;        break;
        case Attack::SLight:   c.atkBits = BIT_C; c.dirBits = side;     break;
        case Attack::DLight:   c.atkBits = BIT_C; c.dirBits = BIT_S;    break;
        case Attack::NAir:     c.atkBits = BIT_C; c.dirBits = 0;        break;
        case Attack::SAir:     c.atkBits = BIT_C; c.dirBits = side;     break;
        case Attack::DAir:     c.atkBits = BIT_C; c.dirBits = BIT_S;    break;
        case Attack::Recovery: c.atkBits = BIT_X; c.dirBits = BIT_W;    break;
        default: break;
    }
    return c;
}

// ═══════════════════════════════════════════════════════════
// State machine
// ═══════════════════════════════════════════════════════════
enum class Stage { IDLE, DIR_LEAD, ATTACK_HOLD, RECOVERY };

struct ComboState {
    Stage    stage      = Stage::IDLE;
    uint64_t tStage     = 0;
    AttackCmd pending;
    Attack   lastAttack = Attack::NONE;
    int      comboDepth = 0;
    uint64_t tLastAttack = 0;
    int      hits       = 0;
};
ComboState g_cs;

const char* g_lastDecision = "-";
bool g_lastFacingOK = true;
bool g_lastInRange  = false;

HANDLE g_worker = nullptr;
std::atomic<bool> g_run{false};

// ⚡ Entity cache — 1kHz tick'te ağır scan'i tekrarlamak yerine
//    her ~3ms'de bir tazele. Karar gecikmesini düşürür.
struct EntCache { uint64_t at = 0; std::vector<Game::Entity> ents; };
EntCache g_entCache;

const std::vector<Game::Entity>& CachedEntities(uint64_t now) {
    if (g_entCache.ents.empty() || now - g_entCache.at >= 3000) {
        g_entCache.at = now;
        g_entCache.ents = Game::Entities();
    }
    return g_entCache.ents;
}

bool FindLocal(Game::Entity& out) {
    auto ents = Game::Entities();
    for (auto& e : ents) if (e.isLocal) { out = e; return true; }
    return false;
}

bool FindClosestEnemy(const Game::Entity& me, Game::Entity& out, float& dist) {
    auto ents = Game::Entities();
    bool found = false;
    float best = 1e9f;
    for (auto& e : ents) {
        if (e.isLocal) continue;
        if (!e.hasX || !e.hasY) continue;
        float dx = (float)(e.x - me.x);
        float dy = (float)(e.y - me.y);
        float d = sqrtf(dx*dx + dy*dy);
        if (d < best) { best = d; out = e; found = true; }
    }
    dist = best;
    return found;
}

bool FacingEnemy(const Game::Entity& me, double targetX) {
    const double dx = targetX - me.x;
    if (std::fabs(dx) < 5.0) return true;
    if (!me.facingLeft && dx > 0) return true;
    if ( me.facingLeft && dx < 0) return true;
    return false;
}



// ═══════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════
void Tick() {
    const uint64_t now = NowUs();

    if (!Globals::EnableHammerCombo) {
        if (g_cs.stage != Stage::IDLE || g_botAtkBits || g_botDirBits) {
            ClearAll();
            g_cs.stage = Stage::IDLE;
            g_cs.comboDepth = 0;
        }
        g_kit = nullptr;
        return;
    }

    if (!Game::InputMaskAddr()) { ClearBotBits(); return; }

    // ⚡ Entity cache'den oku (scan başına değil)
    const auto& ents = CachedEntities(now);

    const Game::Entity* mePtr = nullptr;
    for (const auto& e : ents) if (e.isLocal) { mePtr = &e; break; }
    if (!mePtr) { ClearAll(); return; }
    const Game::Entity me = *mePtr;

    // ⚡ Silah kitini seç — id **veya** isim (scythe/gauntlets otomatik)
    const WeaponKit* kit = me.weaponKnown ? KitFor(me.weaponId, me.weaponName) : nullptr;
    if (!kit) {
        if (g_cs.stage != Stage::IDLE) {
            ClearAll();
            g_cs.stage = Stage::IDLE;
            g_cs.comboDepth = 0;
        }
        g_kit = nullptr;
        return;
    }
    if (g_kit != kit) {
        g_kit = kit;                                  // silah değişti → combo sıfırla
        g_cs.comboDepth = 0;
        g_cs.lastAttack = Attack::NONE;
        g_hit.pendingAttack = false;
    }

    const Game::Entity* enPtr = nullptr;
    float best = 1e9f;
    for (const auto& e : ents) {
        if (e.isLocal || !e.hasX || !e.hasY) continue;
        const float dx = (float)(e.x - me.x);
        const float dy = (float)(e.y - me.y);
        const float d = sqrtf(dx * dx + dy * dy);
        if (d < best) { best = d; enPtr = &e; }
    }
    if (!enPtr) { ClearAll(); return; }
    const Game::Entity enemy = *enPtr;

    if (Globals::HammerMinDamage > 0 && enemy.damageKnown &&
        enemy.damage < (double)Globals::HammerMinDamage) {
        ClearAll();
        return;
    }

    // ═══ Hit confirm ═══
    if (enemy.damageKnown) {
        const double dmgNow = enemy.damage;
        if (g_hit.pendingAttack && dmgNow > g_hit.lastDamage + 0.5) {
            g_hit.consecutiveHits++;
            g_hit.consecutiveMisses = 0;
            g_hit.pendingAttack = false;
        }
        g_hit.lastDamage = dmgNow;
    }

    if (g_hit.pendingAttack &&
        (now - g_hit.lastAttackT) > (uint64_t)Globals::HammerHitConfirmMs * 1000ULL) {
        // Saldırı zırh / spot-dodge'a yediyse whiff SAYMA — hasar
        // artmaz ama whiff-block'un devreye girmesi yanlış olur.
        const bool ateArmor = IsEnemyInvuln(enemy);
        if (!ateArmor) {
            g_hit.consecutiveMisses++;
            g_hit.consecutiveHits = 0;
            if (Globals::HammerWhiffBlock) {
                g_hit.whiffUntil = now + (uint64_t)Globals::HammerWhiffBlockMs * 1000ULL;
            }
        }
        g_hit.pendingAttack = false;
    }

    if (now < g_hit.whiffUntil) {
        if (g_cs.stage != Stage::IDLE) {
            ClearBotBits();
            g_cs.stage = Stage::IDLE;
        }
        ApplyBits();
        return;
    }

    // Kullanıcı attack basıyorsa bot çekilir
    if ((ReadUserBits() & ATK_MASK) != 0) {
        if (g_cs.stage != Stage::IDLE) {
            ClearBotBits();
            g_cs.stage = Stage::IDLE;
        }
        ApplyBits();
        return;
    }

    // Velocity tracking — önce, dodge izleme için gerekli
    EntTrack* track = trackFor(enemy.address);
    if (enemy.hasX && enemy.hasY) UpdateTrack(*track, enemy.x, enemy.y, now);
    const bool hasTrack = (track->stable >= 2);

    // ⚡ Rakip spot-dodge / invulnerable → attack basma, bekle
    if (IsEnemyInvuln(enemy)) {
        if (track->dodgeAt == 0) track->dodgeAt = now;      // dodge BAŞLADI
        if (g_cs.stage != Stage::IDLE) {
            ClearBotBits();
            g_cs.stage = Stage::IDLE;
        }
        if (Globals::ActOnEnemyDodge) {
            g_cs.comboDepth = 0;                             // string'i abort et
            g_hit.pendingAttack = false;
        }
        g_lastDecision = "enemy-dodge-hold";
        ApplyBits();
        return;
    }

    // ⚡ Dodge BİTTİ — pozisyon/hız oturana kadar yeniden saldırma:
    //    stale konuma basmak "misinput" üretir.
    if (Globals::ActOnEnemyDodge && track->dodgeAt != 0 &&
        (now - track->dodgeAt) < (uint64_t)Globals::DodgeSettleMs * 1000ULL) {
        if (g_cs.stage != Stage::IDLE) {
            ClearBotBits();
            g_cs.stage = Stage::IDLE;
        }
        g_hit.pendingAttack = false;
        g_lastDecision = "post-dodge-settle";
        ApplyBits();
        return;
    }
    // dodge izi’ni zaman aşımına bağla
    if (track->dodgeAt != 0 && (now - track->dodgeAt) > 1500 * 1000ULL)
        track->dodgeAt = 0;

    // Karar
    AttackCmd cmd = DecideAttack(me, enemy, enemy.x, enemy.y, *track, hasTrack);
    g_lastDecision = cmd.why;

    const bool needFacing = (cmd.type == Attack::SLight || cmd.type == Attack::SAir);
    const bool facingOK = !needFacing || FacingEnemy(me, enemy.x);
    g_lastFacingOK = facingOK;
    g_lastInRange = (cmd.type != Attack::NONE);

    const uint64_t DIR_LEAD_US    = (uint64_t)Globals::HammerDirLeadMs    * 1000ULL;
    const uint64_t ATTACK_HOLD_US = (uint64_t)Globals::HammerAttackHoldMs * 1000ULL;
    const uint64_t RECOVERY_US    = (uint64_t)Globals::HammerRecoveryMs   * 1000ULL;
    const uint64_t FOLLOWUP_US    = (uint64_t)Globals::HammerFollowupMs   * 1000ULL;

    switch (g_cs.stage) {

    case Stage::IDLE: {
        // ⚡ State-driven chain — son vuruş LANDED ise, MEVCUT duruma göre link seç
        if (Globals::HammerComboChains &&
            g_cs.comboDepth > 0 &&
            g_hit.consecutiveHits > 0 &&
            (now - g_cs.tLastAttack) < FOLLOWUP_US * 3) {
            const Attack link = ChainLink(g_cs.lastAttack, me, enemy);
            if (link != Attack::NONE) {
                cmd = MakeChainCmd(link, cmd.dir);
                g_lastDecision = "chain";
            }
        }

        if (g_cs.comboDepth > 0 && (now - g_cs.tLastAttack) > FOLLOWUP_US * 4)
            g_cs.comboDepth = 0;

        // Facing'i her linkte tazele — düşman üstümüzden atlayabilir
        const bool needFacing = (cmd.type == Attack::SLight || cmd.type == Attack::SAir);
        const bool facingOKNow = !needFacing || FacingEnemy(me, enemy.x);
        g_lastFacingOK = facingOKNow;

        if (cmd.type == Attack::NONE || !facingOKNow) {
            g_botAtkBits = 0;
            break;
        }

        if (Globals::HammerUseDirection) {
            g_botDirBits = cmd.dirBits;
        } else if (!facingOKNow) {
            g_botDirBits = (enemy.x < me.x) ? BIT_A : BIT_D;
        } else {
            g_botDirBits = 0;
        }

        g_cs.pending = cmd;
        g_cs.tStage  = now;
        g_cs.stage   = Stage::DIR_LEAD;
        g_botAtkBits = 0;
        break;
    }

    case Stage::DIR_LEAD: {
        // Direction'ı TEK BAŞINA uygulama — yatay girerse dash/run atak
        // üretir (misinput). Dikey (S/W) tabanda zararsız; yatay sadece
        // SLight/SAir için atakla birlikte basılır (ATTACK_HOLD).
        const Attack leadAtk = g_cs.pending.type;
        const bool sideMove  = (leadAtk == Attack::SLight || leadAtk == Attack::SAir);
        g_botDirBits = sideMove ? g_cs.pending.dirBits
                                : (g_cs.pending.dirBits & (BIT_S | BIT_W));
        // Chained link → çok kısa lead (4ms), gelen link daha erken işlenir
        const bool chaining = g_cs.comboDepth > 0 && g_hit.consecutiveHits > 0;
        const uint64_t leadUs = chaining ? 4000ULL : DIR_LEAD_US;
        if (now - g_cs.tStage < leadUs) break;
        g_cs.tStage = now;
        g_cs.stage  = Stage::ATTACK_HOLD;
        break;
    }

    case Stage::ATTACK_HOLD:
        g_botAtkBits = g_cs.pending.atkBits;
        if (Globals::HammerUseDirection) {
            const Attack t = g_cs.pending.type;
            // Yan saldırı: rakip attak sırasında üstümüzden geçtiyse yönü tazele
            if ((t == Attack::SLight || t == Attack::SAir) && me.hasX && enemy.hasX)
                g_botDirBits = (enemy.x < me.x) ? BIT_A : BIT_D;
            else
                g_botDirBits = g_cs.pending.dirBits;
        }

        if (now - g_cs.tStage < ATTACK_HOLD_US) break;

        g_cs.lastAttack  = g_cs.pending.type;
        g_cs.tLastAttack = now;
        g_cs.comboDepth++;
        g_cs.hits++;
        g_cs.tStage      = now;
        g_cs.stage       = Stage::RECOVERY;
        g_botAtkBits     = 0;

        g_hit.pendingAttack = true;
        g_hit.lastAttackT   = now;
        g_hit.lastDamage    = enemy.damageKnown ? enemy.damage : 0.0;
        break;

    case Stage::RECOVERY: {
        g_botAtkBits = 0;
        // Vuruş doğrulandıysa recovery kısalt — gerçek combo penceresi
        // sabit 120ms'den kısadır — erken bırak, link frame-perfect bassılsın
        const bool confirmed = !g_hit.pendingAttack &&
                               g_cs.comboDepth > 0 &&
                               g_hit.consecutiveHits > 0 &&
                               Globals::HammerComboChains;
        const uint64_t recovUs = confirmed ? (RECOVERY_US * 3) / 5 : RECOVERY_US;
        if (now - g_cs.tStage < recovUs) break;
        // Sık dodge yapan rakibe uzun string kurma — bir sonraki hit'te
        // dodge'layıp bizi boşluğa basmamıza yol açar (misinput).
        const bool evasive = Globals::ActOnEnemyDodge && track->dodgeAt &&
                             (now - track->dodgeAt) < 1200 * 1000ULL;
        const int depthCap = evasive ? 2 : Globals::HammerMaxComboDepth;
        if (g_cs.comboDepth >= depthCap)
            g_cs.comboDepth = 0;
        g_cs.stage = Stage::IDLE;
        break;
    }

    default:
        g_cs.stage = Stage::IDLE;
        ClearBotBits();
        break;
    }

    ApplyBits();
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
    ClearAll();
    return 0;
}

} // namespace

void Combo::Start() {
    if (g_worker) return;
    EnsureQPC();
    g_run.store(true, std::memory_order_relaxed);
    g_worker = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
}

void Combo::Stop() {
    if (!g_worker) return;
    g_run.store(false, std::memory_order_relaxed);
    WaitForSingleObject(g_worker, 2000);
    CloseHandle(g_worker);
    g_worker = nullptr;
}

void Combo::Update() {
    if (!Globals::HammerComboDebug) return;
    static DWORD lastLog = 0;
    DWORD now = GetTickCount();
    if (now - lastLog < 500) return;
    lastLog = now;

    const char* st = "?";
    switch (g_cs.stage) {
        case Stage::IDLE:        st = "IDLE";     break;
        case Stage::DIR_LEAD:    st = "DIR_LEAD"; break;
        case Stage::ATTACK_HOLD: st = "ATTACK";   break;
        case Stage::RECOVERY:    st = "RECOVERY"; break;
    }

    printf("[COMBO] %-9s last=%-8s d=%d hits=%d hc=%d mc=%d | why=%-22s | "
           "botD=0x%03X botA=0x%03X final=0x%03X\n",
           st, AtkName(g_cs.lastAttack), g_cs.comboDepth, g_cs.hits,
           g_hit.consecutiveHits, g_hit.consecutiveMisses,
           g_lastDecision,
           g_botDirBits, g_botAtkBits, g_lastWritten);
}

// ═══════════════════════════════════════════════════════════
Combo::DebugInfo::DebugInfo()
    : enabled(false), hasInputAddr(false), inputAddr(0),
      botDirBits(0), botAtkBits(0), userBits(0), actualMask(0),
      stage(0), stageUs(0), targetAttack(0), lastAttack(0),
      comboDepth(0), hits(0),
      hasLocal(false), weaponId(-1), weaponName(), kit(0),
      meX(0.0), meY(0.0), meIsAir(false),
      meVelX(0.0), meVelY(0.0),
      hasEnemy(false), enemyAddress(0), enemyX(0.0), enemyY(0.0),
      enemyVelX(0.0), enemyVelY(0.0), enemyHasVel(false),
      enemyPredX(0.0), enemyPredY(0.0), enemyDist(0.0f), enemyPredDist(0.0f),
      enemyAir(false), enemyStun(false), enemyDodge(false),
      enemyIntangible(false), enemyFastFall(false), enemyAirJumps(0),
      consecutiveHits(0), consecutiveMisses(0), pendingAttack(false),
      whiffMs(0), nextLink(0),
      levelId(0), levelType(0), arenaValid(false),
      mapX0(0.0), mapX1(0.0), levelName(),
      decision("-"), facingOK(true), inRange(false)
{}

Combo::DebugInfo Combo::GetDebug() {
    DebugInfo di;
    di.enabled = Globals::EnableHammerCombo;

    uintptr_t addr = Game::InputMaskAddr();
    di.inputAddr    = addr;
    di.hasInputAddr = (addr != 0);
    di.actualMask   = addr ? (Game::ReadInputMask() & CONTROL_MASK) : 0;
    di.botDirBits   = g_botDirBits;
    di.botAtkBits   = g_botAtkBits;
    di.userBits     = g_userBits;

    di.stage      = (int)g_cs.stage;
    di.stageUs    = (int)(NowUs() - g_cs.tStage); if (di.stageUs < 0) di.stageUs = 0;
    di.targetAttack = (int)g_cs.pending.type;
    di.lastAttack = (int)g_cs.lastAttack;
    di.comboDepth = g_cs.comboDepth;
    di.hits       = g_cs.hits;
    di.decision   = g_lastDecision;
    di.facingOK   = g_lastFacingOK;
    di.inRange    = g_lastInRange;
    di.consecutiveHits   = g_hit.consecutiveHits;
    di.consecutiveMisses = g_hit.consecutiveMisses;
    di.pendingAttack     = g_hit.pendingAttack;
    di.whiffMs    = (g_hit.whiffUntil > NowUs())
                        ? (int)((g_hit.whiffUntil - NowUs()) / 1000ULL) : 0;

    // Bir sonraki planlanan chain link (debug için önizleme)
    di.nextLink = 0;
    {
        const uint64_t followWin = (uint64_t)Globals::HammerFollowupMs * 1000ULL;
        if (Globals::HammerComboChains && g_cs.comboDepth > 0 &&
            g_hit.consecutiveHits > 0 && g_cs.lastAttack != Attack::NONE) {
            Game::Entity pm, pe; float pd;
            if (FindLocal(pm) && FindClosestEnemy(pm, pe, pd)) {
                const Attack nl = ChainLink(g_cs.lastAttack, pm, pe);
                di.nextLink = (int)nl;
            }
        }
    }

    Game::Entity me, en;
    di.hasLocal = FindLocal(me);
    if (di.hasLocal) {
        di.meX = me.x; di.meY = me.y;
        di.weaponId = me.weaponKnown ? me.weaponId : -1;
        di.weaponName = me.weaponKnown ? me.weaponName : std::string();
        di.kit = g_kit ? (int)g_kit->id : 0;
        di.meVelX = me.velX; di.meVelY = me.velY;
        di.meIsAir = IsAirborne(me);
    }

    float d = 0.0f;
    di.hasEnemy = di.hasLocal && FindClosestEnemy(me, en, d);
    if (di.hasEnemy) {
        di.enemyDist = d;
        di.enemyAddress = en.address;
        di.enemyX = en.x; di.enemyY = en.y;

        EntTrack* tr = nullptr;
        for (auto& t : g_track) if (t.addr == en.address) { tr = &t; break; }
        if (tr && tr->stable >= 2) {
            di.enemyHasVel = true;
            di.enemyVelX = tr->smoothVx;
            di.enemyVelY = tr->smoothVy;
            const double pT = Globals::HammerPredictionMs / 1000.0;
            di.enemyPredX = en.x + tr->smoothVx * pT;
            di.enemyPredY = en.y + tr->smoothVy * pT;
        } else {
            di.enemyHasVel = false;
            di.enemyPredX = en.x;
            di.enemyPredY = en.y;
        }
        di.enemyPredDist = (float)sqrt(
            (di.enemyPredX - me.x) * (di.enemyPredX - me.x) +
            (di.enemyPredY - me.y) * (di.enemyPredY - me.y));

        di.enemyAir        = IsAirborne(en);
        di.enemyStun       = en.stunned;
        di.enemyDodge      = en.dodge;
        di.enemyIntangible = en.intangible;
        di.enemyFastFall   = en.fastFalling;
        di.enemyAirJumps   = en.airJumpCounter;
    }

    // Map bilgisi
    Game::ArenaInfo ai = Game::ReadArena();
    if (ai.valid) {
        di.arenaValid = true;
        di.levelId    = ai.levelId;
        di.levelType  = ai.levelType;
        di.mapX0      = ai.x0;
        di.mapX1      = ai.x1;
        di.levelName  = ai.levelName;
    }

    return di;
}

uintptr_t Combo::FocusAddress() {
    if (!Globals::EnableHammerCombo) return 0;
    Game::Entity me;
    if (!FindLocal(me)) return 0;
    if (!me.weaponKnown || !KitFor(me.weaponId, me.weaponName)) return 0;
    Game::Entity en;
    float d = 0.0f;
    return FindClosestEnemy(me, en, d) ? en.address : 0;
}
