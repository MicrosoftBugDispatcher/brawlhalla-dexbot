#pragma once
#include <windows.h>

namespace Globals {

struct AttackTune {
    bool enabled = false;
    int powerId = 0;
    int weaponId = 0;
    int phase1StartAdjust = 0;
    int phase1EndAdjust = 0;
    int phase2EndAdjust = 0;
    int finalEndAdjust = 0;
    float yOffsetAdjust = 0.0f;
    float xOffsetAdjust = 0.0f;
    float rxAdjust = 0.0f;
    float ryAdjust = 0.0f;
};

inline bool Unload = false;
inline bool ShowMenu = true;

inline bool EnableESP = true;
inline bool EnableMapESP = false;
inline bool MapESPFill = false;
inline int MapESPThickness = 1;
inline bool EnableAutoAttack = false;
inline bool EnableAutoDodge = false;
inline bool EnableBunnyhop = false;
inline bool ShowHitDisplay = false;
inline bool ShowDynamicHitboxes = false;
inline bool ShowState = false;

// ═══════════════════════════════════════════════════════════
// ⚡ BUNNYHOP FLAGS (YENİ)
// ═══════════════════════════════════════════════════════════
inline bool BunnyhopDebug  = false;          // Debug overlay

// ═══════════════════════════════════════════════════════════
// ⚡ BUNNYHOP TIMING (manuel kayıttan alındı)
// ═══════════════════════════════════════════════════════════
inline int BunnyhopDodgeToJumpMs = 15;    // Dodge → Jump arası
inline int BunnyhopJumpHoldMs    = 60;    // Jump basılı tutma
inline int BunnyhopJumpToFallMs  = 30;    // Jump bırak → S bas

// Eski değişkenler (artık kullanılmıyor, geri uyumluluk için bırakıldı)
inline int  BunnyhopCooldownMs = 40;         // Hop arası min süre
inline int  BunnyhopDashJumpWindow = 150;    // ms (9 frame ~ 150ms)
inline int  BunnyhopLandingDelay = 70;       // Yere değdikten sonra bekle
inline int  BunnyhopTurnDelay = 200;         // Yön flip timeout

// ═══════════════════════════════════════════════════════════
// 🔨 HAMMER AUTO COMBO (v5 — hitbox-aware + recovery)
// ═══════════════════════════════════════════════════════════
inline bool EnableHammerCombo  = false;
inline bool HammerComboDebug   = false;
inline int  HammerWeaponId     = 107;

// Menziller (daha geniş — agresif)
inline int  HammerGroundRange  = 140;    // Yerde C menzili
inline int  HammerAirVertical  = 40;     // Hava dikey tolerans
inline int  HammerAirSide      = 130;    // SAir yatay menzil
inline int  HammerAirBelow     = 70;     // DAir alt tolerans

// Recovery (W+K)
inline bool HammerUseRecovery  = true;   // Fall sırasında yukarıdaki düşmana W+K
inline int  HammerRecoveryMinDist = 80;  // Minimum dikey mesafe
inline int  HammerRecoveryMaxDist = 280; // Maksimum dikey mesafe

// Timing (daha hızlı — agresif)
inline int  HammerDirLeadMs    = 5;
inline int  HammerAttackHoldMs = 30;
inline int  HammerRecoveryMs   = 80;
inline int  HammerFollowupMs   = 90;
inline int  HammerMaxComboDepth = 4;
inline int  HammerMinDamage    = 0;

// Smart
inline bool HammerAutoAttack   = true;
inline bool HammerUseDirection = true;
inline int  HammerPredictionMs = 60;
inline bool HammerComboChains  = true;
inline bool HammerWhiffBlock   = true;
inline int  HammerWhiffBlockMs = 120;
inline int  HammerHitConfirmMs = 150;

// Dodge-aware combo — rakip dodge'larsa string'i abort et, pozisyon
// oturana kadar bekle; sık dodge yapana uzun string kurma.
inline bool ActOnEnemyDodge = true;      // dodge yönetimi açık
inline int  DodgeSettleMs   = 140;       // dodge bitince yeniden nişan süresi

// Jump/fall detection
inline int  HammerJumpVelThresh = 100;   // velY > bu → zıplıyor
inline int  HammerFallVelThresh = -60;   // velY < bu → düşüyor

// ═══════════════════════════════════════════════════════════
// 🌐 MULTI-WEAPON KITS (scythe / gauntlets / hammer)
// ═══════════════════════════════════════════════════════════
inline int  ScytheWeaponId   = 0;    // 0 = sadece isim eşleştir
inline int  GauntletWeaponId = 0;    // 0 = sadece isim eşleştir

// ESP kutu dış çizgisi
inline int  ESPBorderThickness = 2;

inline int KeyUp = 'W';
inline int KeyDown = 'S';
inline int KeyLeft = 'A';
inline int KeyRight = 'D';
inline int KeyLight = 'J';
inline int KeyHeavy = 'K';
inline int KeyDodge = VK_SHIFT;
inline int KeyJump = VK_SPACE;
inline int KeyPickup = 'E';

inline constexpr int MaxAttackTunes = 32;
inline AttackTune CalibTunes[MaxAttackTunes];

inline int CalibObservedPowerId = 0;
inline int CalibObservedWeaponId = 0;
inline int CalibObservedPowerIds[8] = {};
inline int CalibObservedPowerIdsCount = 0;
inline int CalibEditPowerId = 0;
inline int CalibEditWeaponId = 0;
inline const char* CalibLastSavePath = "";
inline int CalibGroundCastFrameExtra = 0;
inline int CalibAirCastFrameExtra = 0;
inline float CalibAirHitboxYOffsetExtra = 0.0f;

}
