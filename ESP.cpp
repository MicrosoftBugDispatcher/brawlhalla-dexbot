#include "ESP.h"
#include "Game.h"
#include "Globals.h"
#include "Combo.h"
#include "MapData.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>

namespace {

struct Proj {
    bool ok = false;
    float x = 0.0f;
    float y = 0.0f;
};

Game::Camera g_cam;

Proj ToScreen(double worldX, double worldY) {
    Proj r;
    if (!g_cam.valid) return r;
    if (!std::isfinite(worldX) || !std::isfinite(worldY)) return r;
    if (std::fabs(worldX) > 1.0e6 || std::fabs(worldY) > 1.0e6) return r;

    const double sx = g_cam.camX + worldX * g_cam.zoomX;
    const double sy = g_cam.camY + worldY * g_cam.zoomY;
    if (!std::isfinite(sx) || !std::isfinite(sy)) return r;
    if (std::fabs(sx) > 1.0e7 || std::fabs(sy) > 1.0e7) return r;

    r.ok = true;
    r.x = (float)sx;
    r.y = (float)sy;
    return r;
}

bool Visible(const Proj& p, float margin) {
    const ImVec2 scr = ImGui::GetIO().DisplaySize;
    return p.x > -margin && p.x < scr.x + margin && p.y > -margin && p.y < scr.y + margin;
}

void Shadowed(ImDrawList* dl, float x, float y, const char* text, ImU32 color) {
    dl->AddText(ImVec2(x + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 200), text);
    dl->AddText(ImVec2(x, y), color, text);
}

constexpr double kHurtboxWidth = 145.0;
constexpr double kHurtboxHeight = 160.0;

void DrawPlayer(ImDrawList* dl, const Game::Entity& e, uint32_t myTeam, uintptr_t focus) {
    if (!e.hasX || !e.hasY) return;

    Proj p = ToScreen(e.x, e.y);
    if (!p.ok || !Visible(p, 480.0f)) return;

    const float h = (float)(kHurtboxHeight * g_cam.zoomY * 1.02);
    if (!(h > 4.0f) || h > 1200.0f) return;
    const float w = h * (float)(kHurtboxWidth / kHurtboxHeight);

    const float bottom = p.y;
    const float top = p.y - h;
    const float left = p.x - w * 0.5f;
    const float right = p.x + w * 0.5f;

    const ImU32 color = e.isLocal ? IM_COL32(160, 90, 235, 255)
                        : (e.team && e.team == myTeam ? IM_COL32(240, 180, 60, 255)
                                                      : IM_COL32(235, 75, 85, 255));

    // ⚡ Dış çizgi (outline) — kalınlık ayarlanabilir
    const float ol = (float)Globals::ESPBorderThickness;
    dl->AddRect(ImVec2(left - ol, top - ol), ImVec2(right + ol, bottom + ol),
                IM_COL32(0, 0, 0, 150), 0.0f, 0, ol);
    dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), color, 0.0f, 0, 1.6f);

    // 🎯 Combo hedefi → parlayan çerçeve + TARGET etiketi
    if (focus && e.address == focus) {
        const float pulse = 0.55f + 0.45f * sinf(ImGui::GetTime() * 8.0f);
        const float m = 6.0f;
        const ImU32 tcol = IM_COL32(255, 60 + (int)(195.0f * pulse), 40, 255);
        dl->AddRect(ImVec2(left - m, top - m), ImVec2(right + m, bottom + m),
                    tcol, 0.0f, 0, 2.2f);
        const ImVec2 ts = ImGui::CalcTextSize("TARGET");
        Shadowed(dl, p.x - ts.x * 0.5f, bottom + 3.0f, "TARGET", tcol);
    }

    char name[24];
    if (e.isLocal)
        std::snprintf(name, sizeof(name), "YOU");
    else if (e.seat)
        std::snprintf(name, sizeof(name), "P%u", e.seat);
    else
        std::snprintf(name, sizeof(name), "PLAYER");

    char label[48];
    if (e.damageKnown)
        std::snprintf(label, sizeof(label), "%s  %.0f", name, e.damage);
    else
        std::snprintf(label, sizeof(label), "%s", name);

    char wbuf[16];
    const char* wpn = "FISTS";
    if (e.weaponKnown) {
        if (!e.weaponName.empty()) {
            wpn = e.weaponName.c_str();
        } else {
            std::snprintf(wbuf, sizeof(wbuf), e.weaponIsWeapon ? "WPN %d" : "ITEM %d", e.weaponId);
            wpn = wbuf;
        }
    }

    char sub[96];
    int k = std::snprintf(sub, sizeof(sub), "%s", wpn);
    if (e.airborne && k > 0 && k < (int)sizeof(sub))
        k += std::snprintf(sub + k, sizeof(sub) - (size_t)k, "  AIR");
    if (e.stunKnown && e.stunned && k > 0 && k < (int)sizeof(sub))
        k += std::snprintf(sub + k, sizeof(sub) - (size_t)k, "  STUN");
    if (e.stateKnown && e.state != 0 && k > 0 && k < (int)sizeof(sub))
        k += std::snprintf(sub + k, sizeof(sub) - (size_t)k, "  st%u", e.state);
    if (e.deathsKnown && e.deaths != 0 && k > 0 && k < (int)sizeof(sub))
        std::snprintf(sub + k, sizeof(sub) - (size_t)k, "  d%u", e.deaths);

    const ImVec2 ts = ImGui::CalcTextSize(label);
    Shadowed(dl, p.x - ts.x * 0.5f, top - ts.y - 4.0f, label, color);
    const ImVec2 ss = ImGui::CalcTextSize(sub);
    Shadowed(dl, p.x - ss.x * 0.5f, top - ts.y - 6.0f - ss.y, sub, IM_COL32(230, 230, 230, 255));
}

void DrawItem(ImDrawList* dl, const Game::Item& it) {
    Proj p = ToScreen(it.x, it.y);
    if (!p.ok || !Visible(p, 320.0f)) return;

    const float half = std::clamp((float)(30.0 * g_cam.zoomY), 6.0f, 90.0f);
    const ImU32 color = it.isWeapon ? IM_COL32(90, 220, 140, 255) : IM_COL32(240, 200, 80, 255);

    dl->AddRect(ImVec2(p.x - half - 1.0f, p.y - half - 1.0f), ImVec2(p.x + half + 1.0f, p.y + half + 1.0f), IM_COL32(0, 0, 0, 130), 0.0f, 0, 1.0f);
    dl->AddRect(ImVec2(p.x - half, p.y - half), ImVec2(p.x + half, p.y + half), color, 0.0f, 0, 1.6f);

    char label[48];
    std::snprintf(label, sizeof(label), "%s %d", it.isWeapon ? "WPN" : "ITEM", it.id);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    Shadowed(dl, p.x - ts.x * 0.5f, p.y - half - ts.y - 4.0f, label, color);
}

void DrawDebug(ImDrawList* dl, const Game::Debug& d, const Game::Entity* local,
               const Combo::DebugInfo& ci) {
    (void)d;
    (void)local;

    // Arena bounds debug
    Game::ArenaInfo ai = Game::ReadArena();
    char arenaBoundsStr[128];
    if (ai.valid) {
        std::snprintf(arenaBoundsStr, 128, "ARENA: x=%.1f..%.1f  y=%.1f..%.1f",
                     ai.x0, ai.x1, ai.y0, ai.y1);
    } else {
        std::snprintf(arenaBoundsStr, 128, "ARENA: invalid");
    }

    const char* stageNames[] = { "IDLE", "DIR_LEAD", "ATTACK", "RECOVERY" };
    const char* atkNames[]   = { "-", "NLight", "SLight", "DLight",
                                 "NAir", "SAir", "DAir", "Recovery" };
    const char* kitNames[]   = { "-", "Hammer", "Scythe", "Gauntlets" };

    int stageIdx = (ci.stage >= 0 && ci.stage <= 3)      ? ci.stage      : 0;
    int atkIdx   = (ci.lastAttack >= 0 && ci.lastAttack <= 7) ? ci.lastAttack : 0;
    int pendIdx  = (ci.targetAttack >= 0 && ci.targetAttack <= 7) ? ci.targetAttack : 0;
    int nextIdx  = (ci.nextLink >= 0 && ci.nextLink <= 7) ? ci.nextLink : 0;
    int kitIdx   = (ci.kit >= 1 && ci.kit <= 3) ? ci.kit : 0;

    char hdr[40];
    std::snprintf(hdr, sizeof(hdr), "AUTO COMBO - %s", kitNames[kitIdx]);

    bool writeMismatch = ci.hasInputAddr && ci.botAtkBits != 0 &&
                         ci.actualMask != (ci.botDirBits | ci.botAtkBits);

    const char* addrStatus =
        !ci.hasInputAddr   ? "!! NULL — chain broken" :
        writeMismatch      ? "!! WRITE FAILED" :
                             "OK";

    const char* inRange = "-";
    if (ci.hasEnemy) {
        const float adx = fabsf(ci.enemyX - ci.meX);
        if (adx < Globals::HammerGroundRange)      inRange = "IN RANGE";
        else                                      inRange = "FAR";
    }

    char lines[36][190];
    int n = 0;
    std::snprintf(lines[n++], 190, "==== %s ====", hdr);
    std::snprintf(lines[n++], 190, "Stage: %s +%dms   Weapon: %s (id=%d)",
                  stageNames[stageIdx], ci.stageUs / 1000,
                  ci.weaponName.empty() ? "-" : ci.weaponName.c_str(),
                  ci.weaponId);
    std::snprintf(lines[n++], 190, "LastAtk: %-8s  Pending: %-8s  Depth: %d Hits: %d",
                  atkNames[atkIdx], atkNames[pendIdx], ci.comboDepth, ci.hits);
    std::snprintf(lines[n++], 190, "Decision: %s  (facing=%d range=%d)",
                  ci.decision, ci.facingOK, ci.inRange);
    std::snprintf(lines[n++], 190, "");

    std::snprintf(lines[n++], 180, "Input 0x%llX  %s  actual=0x%03X",
                  (unsigned long long)ci.inputAddr, addrStatus, ci.actualMask);
    std::snprintf(lines[n++], 180, "Bot D=0x%03X A=0x%03X  User=0x%03X",
                  ci.botDirBits, ci.botAtkBits, ci.userBits);
    std::snprintf(lines[n++], 180, "");

    std::snprintf(lines[n++], 180, "Local: %s  pos=(%.0f,%.0f)  air=%d  weapon=%d",
                  ci.hasLocal ? "YES" : "NO", ci.meX, ci.meY, ci.meIsAir, ci.weaponId);
    std::snprintf(lines[n++], 180, "LocalVel: (%.0f, %.0f)", ci.meVelX, ci.meVelY);
    std::snprintf(lines[n++], 180, "");
    std::snprintf(lines[n++], 180, "Enemy: %s  d=(%.0f,%.0f)  dist=%.0f",
                  ci.hasEnemy ? "YES" : "NO",
                  ci.enemyX - ci.meX, ci.enemyY - ci.meY, ci.enemyDist);
    std::snprintf(lines[n++], 180, "EnemyVel: %s  (%.0f, %.0f)",
                  ci.enemyHasVel ? "OK " : "---", ci.enemyVelX, ci.enemyVelY);
    std::snprintf(lines[n++], 180, "Predicted: (%.0f,%.0f)  predDist=%.0f",
                  ci.enemyPredX, ci.enemyPredY, ci.enemyPredDist);
    std::snprintf(lines[n++], 180, "E.state: air=%d stun=%d dodge=%d intan=%d ff=%d jumps=%u",
                  ci.enemyAir, ci.enemyStun, ci.enemyDodge,
                  ci.enemyIntangible, ci.enemyFastFall, ci.enemyAirJumps);
    std::snprintf(lines[n++], 180, "");
    std::snprintf(lines[n++], 190, "HitTracker: hits=%d misses=%d pending=%d whiff=%dms",
                  ci.consecutiveHits, ci.consecutiveMisses, ci.pendingAttack,
                  ci.whiffMs);
    std::snprintf(lines[n++], 190, "Next link: %s", atkNames[nextIdx]);
    std::snprintf(lines[n++], 180, "Map: ID=%d / Alt=%d  LvlMgr=0x%llX",
                  d.mapId, d.mapIdAlt, (unsigned long long)d.levelMgr);
    const char* mapName = MapData::GetMapName(d.mapIdAlt);
    std::snprintf(lines[n++], 180, "Map Name: %s (%s)", mapName, MapData::GetMapLevelName(d.mapIdAlt));
    std::snprintf(lines[n++], 180, "%s", arenaBoundsStr);
    std::snprintf(lines[n++], 180, "Map: id=%d  type=%d  arena=%s",
                  ci.levelId, ci.levelType, ci.arenaValid ? "OK" : "--");
    std::snprintf(lines[n++], 180, "Map Bounds: x=[%.0f..%.0f]  name='%s'",
                  ci.mapX0, ci.mapX1,
                  ci.levelName.empty() ? "?" : ci.levelName.c_str());
    std::snprintf(lines[n++], 180, "Ranges: ground<%d  airV=%d  airS=%d  airB=%d  pred=%dms",
                  Globals::HammerGroundRange,
                  Globals::HammerAirVertical, Globals::HammerAirSide,
                  Globals::HammerAirBelow,
                  Globals::HammerPredictionMs);
    std::snprintf(lines[n++], 180, "Recovery: min=%d max=%d  use=%d",
                  Globals::HammerRecoveryMinDist, Globals::HammerRecoveryMaxDist,
                  Globals::HammerUseRecovery);

    const float fs   = ImGui::GetFontSize();
    const float pad  = 10.0f;
    const float head = fs + 8.0f;

    float w = 0.0f;
    for (int i = 0; i < n; i++) {
        const float lw = ImGui::CalcTextSize(lines[i]).x;
        if (lw > w) w = lw;
    }

    const ImVec2 scr = ImGui::GetIO().DisplaySize;
    const ImVec2 p0(8.0f, scr.y - (head + pad * 2.0f + n * (fs + 2.0f)) - 8.0f);
    const ImVec2 p1(p0.x + w + pad * 2.0f, scr.y - 8.0f);

    // 🎯 Tahmini pozisyon işaretçisi
    if (ci.hasEnemy && ci.enemyHasVel) {
        Proj pp = ToScreen(ci.enemyPredX, ci.enemyPredY);
        if (pp.ok) {
            const float s = 8.0f;
            dl->AddLine(ImVec2(pp.x - s, pp.y), ImVec2(pp.x + s, pp.y),
                        IM_COL32(0, 220, 255, 210), 1.5f);
            dl->AddLine(ImVec2(pp.x, pp.y - s), ImVec2(pp.x, pp.y + s),
                        IM_COL32(0, 220, 255, 210), 1.5f);
        }
    }

    dl->AddRectFilled(p0, p1, IM_COL32(15, 15, 15, 245), 6.0f);
    dl->AddRect(p0, p1, IM_COL32(90, 90, 90, 255), 6.0f);

    // Header — combo açıksa kırmızı, kapalıysa gri
    ImU32 hdrCol = ci.enabled ? IM_COL32(180, 40, 40, 255)
                              : IM_COL32(60, 60, 60, 255);
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + head), hdrCol, 6.0f,
                      ImDrawFlags_RoundCornersTop);
    dl->AddText(ImVec2(p0.x + pad, p0.y + 4.0f),
                IM_COL32(255, 255, 255, 255), hdr);

    for (int i = 0; i < n; i++) {
        ImU32 col = IM_COL32(200, 205, 215, 255);

        // Kritik satırları renklendir
        if (strstr(lines[i], "!! NULL"))       col = IM_COL32(255, 80, 80, 255);
        else if (strstr(lines[i], "!! WRITE")) col = IM_COL32(255, 180, 60, 255);
        else if (strstr(lines[i], "overwritten")) col = IM_COL32(255, 180, 60, 255);
        else if (strstr(lines[i], "NULL — chain")) col = IM_COL32(255, 80, 80, 255);
        else if (strstr(lines[i], "OK") && strstr(lines[i], "InputAddr"))
            col = IM_COL32(120, 255, 120, 255);

        dl->AddText(ImVec2(p0.x + pad, p0.y + head + pad + i * (fs + 2.0f)),
                    col, lines[i]);
    }

    // === MAP ID DOSYASINA YAZ (sadece değişince) ===
    static int lastMapId  = -1;
    static int lastMapAlt = -1;
    if ((d.mapId != lastMapId) || (d.mapIdAlt != lastMapAlt)) {
        lastMapId  = d.mapId;
        lastMapAlt = d.mapIdAlt;

        // DLL dizinini al
        char dllPath[MAX_PATH];
        HMODULE hModule = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)&DrawDebug, &hModule)) {
            GetModuleFileNameA(hModule, dllPath, MAX_PATH);
            std::string mapIdStr = dllPath;
            size_t slash = mapIdStr.find_last_of("\\/");
            if (slash != std::string::npos)
                mapIdStr = mapIdStr.substr(0, slash + 1);
            else
                mapIdStr = ".\\";
            mapIdStr += "mapid.txt";

            FILE* f = fopen(mapIdStr.c_str(), "w");
            if (f) {
                fprintf(f, "Map ID (LOM 0xA0): %d\n", d.mapId);
                fprintf(f, "Map ID (LT chain): %d\n", d.mapIdAlt);
                fprintf(f, "Level Mgr: 0x%llX\n", (unsigned long long)d.levelMgr);
                fclose(f);
            }
        }
    }
}

}

void ESP::Draw() {
    const ImVec2 scr = ImGui::GetIO().DisplaySize;
    g_cam = Game::ReadCamera(scr.x, scr.y);

    std::vector<Game::Entity> players = Game::Entities();
    std::vector<Game::Item> items = Game::Items();

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;

    const Game::Entity* local = nullptr;
    for (const auto& e : players) {
        if (e.isLocal) { local = &e; break; }
    }

    // ⚡ Debug overlay — GetDebug'i tek seferde al, hedef adresini de öğren
    Combo::DebugInfo ci;
    const bool wantDebug = Globals::HammerComboDebug || Globals::ShowMenu;
    if (wantDebug) {
        ci = Combo::GetDebug();
        DrawDebug(dl, Game::Snapshot(), local, ci);
    }
    const uintptr_t focus = (wantDebug && ci.enabled) ? ci.enemyAddress : 0;

    // ═══════════════════════════════════════════════════════════
    // 🗺️ MAP ESP — Arena Bounds (tüm map'ler için çalışır)
    // ═══════════════════════════════════════════════════════════
    if (Globals::EnableMapESP) {
        Game::ArenaInfo ai = Game::ReadArena();
        if (ai.valid && ai.x1 > ai.x0 && ai.y1 > ai.y0) {
            const ImU32 lineColor = IM_COL32(0, 255, 80, 255);   // ⚡ parlak yeşil
            const ImU32 fillColor = IM_COL32(0, 255, 80, 25);
            const float thick     = (float)Globals::MapESPThickness;

            // Sol-üst ve sağ-alt köşeleri ekrana projekte et
            Proj tl = ToScreen(ai.x0, ai.y0);
            Proj br = ToScreen(ai.x1, ai.y1);

            if (tl.ok && br.ok) {
                float x0 = tl.x < br.x ? tl.x : br.x;
                float x1 = tl.x < br.x ? br.x : tl.x;
                float y0 = tl.y < br.y ? tl.y : br.y;
                float y1 = tl.y < br.y ? br.y : tl.y;

                ImVec2 a(x0, y0), b(x1, y1);

                if (Globals::MapESPFill)
                    dl->AddRectFilled(a, b, fillColor);

                dl->AddRect(a, b, lineColor, 0.0f, 0, thick);
            }
        }

        // Platform ESP (MapData varsa)
        Game::Debug debug = Game::Snapshot();
        int targetMapId = debug.mapIdAlt > 0 ? debug.mapIdAlt : debug.mapId;
        const MapData::MapGeometry* geo = MapData::GetMapGeometry(targetMapId);
        if (geo && geo->platformCount > 0) {
            const ImU32 platLineColor = IM_COL32(0, 200, 255, 200); // Açık mavi
            const float platThick = 1.0f;
            const ImVec2 scr = ImGui::GetIO().DisplaySize;

            for (int i = 0; i < geo->platformCount; i++) {
                const MapData::Platform& plat = geo->platforms[i];
                if (plat.width <= 0 || plat.height <= 0) continue;

                Proj tl = ToScreen(plat.x, plat.y);
                Proj br = ToScreen(plat.x + plat.width, plat.y + plat.height);
                if (!tl.ok || !br.ok) continue;

                float x0 = tl.x < br.x ? tl.x : br.x;
                float x1 = tl.x < br.x ? br.x : tl.x;
                float y0 = tl.y < br.y ? tl.y : br.y;
                float y1 = tl.y < br.y ? br.y : tl.y;
                if (x1 < 0 || x0 > scr.x || y1 < 0 || y0 > scr.y) continue;

                ImVec2 a(x0, y0), b(x1, y1);
                dl->AddRect(a, b, platLineColor, 0.0f, 0, platThick);
            }
        }
    }

    if (!Globals::EnableESP || !g_cam.valid) return;

    uint32_t myTeam = 0;
    for (const auto& e : players) {
        if (e.isLocal) { myTeam = e.team; break; }
    }

    for (const auto& it : items)
        if (!it.held) DrawItem(dl, it);

    for (const auto& e : players)
        DrawPlayer(dl, e, myTeam, focus);
}
