#include "Game.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace {

using Ptr = uintptr_t;

const Ptr SCAN_START = 0x10000;
const Ptr SCAN_TOP = 0x00007FFFFFFE0000ull;
const size_t SCAN_WINDOW = 0x620;

const size_t G_LOCAL_ENTITY = 0x610;
const size_t G_ROSTER = 0x590;
const size_t G_MATCH_CONFIG = 0x550;
const size_t G_ITEM_MGR = 0x4F8;
const size_t G_CAMERA = 0x650;
const size_t G_BACKREF = 0x518;

const size_t V_DATA = 0x38;
const size_t V_LEN = 0x40;
const size_t V_ATOMS = 0x10;

const size_t I_MGR_VECTOR = 0x48;
const size_t I_TYPE = 0xE0;
const size_t I_ID = 0xD8;
const size_t I_IS_WEAPON = 0x24;
const size_t I_STATE = 0x90;
const size_t I_X = 0x1A0;
const size_t I_Y = 0x198;
const size_t I_OWNER = 0x64;

const size_t E_SEAT = 0x260;
const size_t E_TEAM = 0x198;
const size_t E_WORD = 0x258;
const size_t E_DAMAGE = 0x5D8;
const size_t E_STATE = 0x25C;
const size_t E_STATS = 0x448;
const size_t ST_DEATHS = 0x8C;
const size_t E_WAS_IN_AIR = 0x118;
const size_t E_FACING = 0xFC;
const size_t E_FASTFALL = 0x11C;
const size_t E_DODGE = 0x54;
const size_t E_WALL = 0x68;
const size_t E_STUN = 0x23C;
const size_t E_INTANGIBLE = 0x9C;
const size_t E_JUMP_KEY = 0x238;
const size_t E_JUMP_ENC = 0x2A4;
const size_t E_COMBAT = 0x4C8;
const size_t CC_HELD = 0xD0;
const size_t HI_TYPE = 0x48;
const size_t T_NAME = 0x168;

// ═══════════════════════════════════════════════════════════
// ⚡ BUNNYHOP OFFSETS (YENİ)
// ═══════════════════════════════════════════════════════════
const size_t E_WAS_ON_GROUND = 0x0A8;
const size_t E_DID_LAND_OFF = 0x0F0;
const size_t E_DID_DASH_JUMP_OFF = 0x0DC;
const size_t E_DASH_FLIP_OFF = 0x0D8;
const size_t E_DASH_ID_OFF = 0x294;
const size_t E_AIRJUMP_CNT = 0x2A4;

// ═══════════════════════════════════════════════════════════
// ⚡ VELOCITY KEY REGISTERS (GUIDE)
//    fighter + 0x15C -> key VX (register index → double)
//    fighter + 0x234 -> key VY (register index → double)
// ═══════════════════════════════════════════════════════════
const size_t KEY_VX_OFF = 0x15C;
const size_t KEY_VY_OFF = 0x234;

const size_t VX_REGS[] = { KEY_VX_OFF };
const double kVelScale = 1.0;
const double kMaxRawVel = 5000.0;

// ═══════════════════════════════════════════════════════════
// 🎮 INPUT CHAIN (build 25394207)
// ═══════════════════════════════════════════════════════════
const size_t E_INPUT_CTRL   = 0x388;
const size_t IN_CMD         = 0x70;
const size_t IN_CMD_MASK    = 0x48;   // basılı tuş bitmask
const size_t IN_COOKED_MASK = 0x54;   // alternate

const size_t G_LEVEL_MGR = 0x6B0;
const size_t LOM_BACKREF = 0x140;
const size_t LOM_GEO_BOUNDS = 0x80;
const size_t LOM_LEVEL_NAME  = 0x98;
const size_t LOM_LEVEL_ID    = 0xA0;
const size_t LOM_LEVEL_TYPE  = 0xD0;

const size_t C_CURRENT_BOUNDS = 0x68;
const size_t C_SHAKE = 0x48;
const size_t C_SCREEN_W = 0x98;
const size_t C_SCREEN_H = 0xA8;
const size_t RECT_X = 0x20;
const size_t RECT_Y = 0x28;
const size_t RECT_W = 0x30;
const size_t RECT_H = 0x38;
const size_t POINT_X = 0x20;
const size_t POINT_Y = 0x28;

const size_t S_STORE = 0x2E0;
const size_t S_VECTOR = 0x20;
const size_t S_SIBLING = 0x28;
const size_t S_NUM_LEN = 0x48;
const size_t S_NUM_BACKING = 0x38;
const size_t S_NUM_VALUES = 0x08;
const size_t S_INT_VALUES = 0x04;
const uint32_t S_SLOTS = 32;
const uint32_t S_PHYS = 128;

const size_t X_REGS[] = { 0x3C, 0x38, 0x34, 0x218, 0x1AC };
const size_t Y_REGS[] = { 0x54, 0x128, 0x1B8 };

Ptr airBase = 0;
Ptr airEnd = 0;
Ptr gameType = 0;
volatile bool running = false;
volatile bool stopFlag = false;
HANDLE worker = nullptr;
std::mutex lock;
std::vector<Game::Entity> cachedEntities;
std::vector<Game::Item> cachedItems;
Game::Debug g_dbg;

bool sane(double v) {
    return std::isfinite(v) && v > -1.0e6 && v < 1.0e6;
}

bool readMem(Ptr addr, void* out, size_t size) {
    __try {
        std::memcpy(out, (const void*)addr, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool writeMem(Ptr addr, const void* data, size_t size) {
    __try {
        // Sayfa zaten RW olmalı (oyun kendisi yazıyor) — ama garanti olsun
        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)addr, size, PAGE_READWRITE, &oldProt)) {
            return false;
        }
        std::memcpy((void*)addr, data, size);
        VirtualProtect((LPVOID)addr, size, oldProt, &oldProt);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
T read(Ptr addr) {
    T value{};
    readMem(addr, &value, sizeof(T));
    return value;
}

Ptr readPtr(Ptr addr) {
    return read<Ptr>(addr);
}

bool inAir(Ptr p) {
    return p >= airBase && p < airEnd;
}

bool loadAirRange() {
    HMODULE mod = GetModuleHandleA("Adobe AIR.dll");
    if (!mod) mod = GetModuleHandleA("AdobeAIR.dll");
    if (!mod) mod = GetModuleHandleA("AIR.dll");
    if (!mod) return false;

    Ptr base = (Ptr)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    airBase = base;
    airEnd = base + nt->OptionalHeader.SizeOfImage;
    return true;
}

bool looksLikeEntity(Ptr e, Ptr game) {
    if (!e || (e & 7)) return false;
    if (!inAir(readPtr(e))) return false;
    return readPtr(e + G_BACKREF) == game;
}

bool readVector(Ptr vec, Ptr& data, uint32_t& len) {
    len = read<uint32_t>(vec + V_LEN);
    data = readPtr(vec + V_DATA);
    return data != 0 && len > 0 && len <= 64;
}

bool decodeAtom(Ptr atom, Ptr& obj) {
    if (atom <= 1 || (atom & 7) != 1) return false;
    Ptr o = atom - 1;
    if (o & 7) return false;
    obj = o;
    return true;
}

bool isGameType(Ptr cand, Ptr local, Ptr roster) {
    if (!local || !roster) return false;
    if ((local & 7) || (roster & 7)) return false;
    if (!looksLikeEntity(local, cand)) return false;

    Ptr data = 0;
    uint32_t len = 0;
    if (!readVector(roster, data, len)) return false;

    uint32_t good = 0;
    for (uint32_t i = 0; i < len; i++) {
        Ptr e = 0;
        if (decodeAtom(readPtr(data + V_ATOMS + (size_t)i * 8), e) && looksLikeEntity(e, cand))
            good++;
    }
    if (good == 0) return false;
    if (!readPtr(cand + G_MATCH_CONFIG)) return false;
    return true;
}

bool scannable(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)) return false;
    switch (protect & 0xFF) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

Ptr findGameType() {
    const size_t CHUNK = 1u << 20;
    std::vector<uint8_t> buf(CHUNK);
    MEMORY_BASIC_INFORMATION mbi{};
    Ptr addr = SCAN_START;

    while (addr < SCAN_TOP) {
        if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) break;
        Ptr base = (Ptr)mbi.BaseAddress;
        Ptr end = base + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && scannable(mbi.Protect)) {
            for (Ptr c = base; c < end; ) {
                size_t want = (size_t)(end - c);
                if (want > CHUNK) want = CHUNK;
                if (want < SCAN_WINDOW) break;

                if (readMem(c, buf.data(), want)) {
                    for (size_t off = 0; off + SCAN_WINDOW <= want; off += 8) {
                        Ptr local = 0;
                        Ptr roster = 0;
                        std::memcpy(&local, buf.data() + off + G_LOCAL_ENTITY, 8);
                        std::memcpy(&roster, buf.data() + off + G_ROSTER, 8);
                        if (isGameType(c + off, local, roster)) return c + off;
                    }
                }
                c += want;
            }
        }
        if (end <= addr) break;
        addr = end;
    }
    return 0;
}

bool packedBool(uint32_t raw, bool& out) {
    const uint32_t sel = (raw >> 24) & 0x1F;
    if (sel >= 24) return false;
    out = ((raw >> sel) & 1u) != 0;
    return true;
}

struct ShiftLock {
    Ptr store = 0;
    uint32_t shift = 0;
    bool locked = false;
};

ShiftLock shiftLocks[8];

uint32_t shiftFor(Ptr store) {
    for (auto& s : shiftLocks)
        if (s.store == store && s.locked) return s.shift;
    return 0;
}

bool readSlotMap(Ptr store, Ptr prim, uint32_t* map) {
    const size_t cands[] = { 0, 0x18, 0x10, 0x30, 0x28 };
    for (size_t off : cands) {
        Ptr m = off == 0 ? prim : readPtr(store + off);
        if (!m) continue;
        if (read<uint32_t>(m + S_NUM_LEN) != S_SLOTS) continue;
        Ptr back = readPtr(m + S_NUM_BACKING);
        if (!back) continue;
        bool seen[128] = {};
        bool ok = true;
        for (uint32_t i = 0; i < S_SLOTS && ok; i++) {
            const uint32_t v = read<uint32_t>(back + S_INT_VALUES + (size_t)i * 4);
            if (v >= S_PHYS || seen[v]) ok = false;
            else {
                seen[v] = true;
                map[i] = v;
            }
        }
        if (ok) return true;
    }
    return false;
}

bool decodeNode(Ptr entity, uint32_t node, double& out) {
    if (node == 0 || node >= S_SLOTS) return false;
    Ptr store = readPtr(entity + S_STORE);
    if (!store) return false;
    Ptr prim = readPtr(store + S_VECTOR);
    if (!prim) return false;
    const uint32_t plen = read<uint32_t>(prim + S_NUM_LEN);
    Ptr sib = readPtr(store + S_SIBLING);
    const uint32_t slen = sib ? read<uint32_t>(sib + S_NUM_LEN) : 0;

    Ptr table = 0;
    uint32_t slot = node;
    if ((sib && slen == S_PHYS) || plen == S_PHYS) {
        uint32_t map[S_SLOTS] = {};
        if (!readSlotMap(store, prim, map)) return false;
        table = (sib && slen == S_PHYS) ? sib : prim;
        slot = (map[node] + shiftFor(store)) % S_PHYS;
    } else if (plen == S_SLOTS) {
        table = prim;
    } else {
        return false;
    }

    Ptr back = readPtr(table + S_NUM_BACKING);
    if (!back) return false;
    out = read<double>(back + S_NUM_VALUES + (size_t)slot * 8);
    if (out != 0.0 && std::fabs(out) < 1.0e-300) return false;
    return out > -30000.0 && out < 30000.0;
}

bool readCoord(Ptr entity, const size_t* regs, size_t count, double& out) {
    double vals[9] = {};
    int vn = 0;
    for (size_t i = 0; i < count && vn < 9; i++) {
        const uint32_t node = read<uint32_t>(entity + regs[i]);
        double v = 0.0;
        if (!decodeNode(entity, node, v) || v == 0.0) continue;
        vals[vn++] = v;
    }

    double best = 0.0;
    int bestVotes = 0;
    for (int i = 0; i < vn; i++) {
        int votes = 0;
        for (int j = 0; j < vn; j++)
            if (std::fabs(vals[j] - vals[i]) < 0.5) votes++;
        if (votes > bestVotes) {
            bestVotes = votes;
            best = vals[i];
        }
    }

    if (bestVotes == 0) return false;
    out = best;
    return true;
}

std::string readString(Ptr obj, size_t off) {
    Ptr str = readPtr(obj + off);
    if (!str) return std::string();

    uint32_t len = read<uint32_t>(str + 0x20);
    uint32_t flags = read<uint32_t>(str + 0x24);
    Ptr buf = readPtr(str + 0x10);
    if (len == 0 || len > 512) return std::string();

    const bool utf16 = (flags & 1u) != 0;
    if (((flags >> 1) & 3u) == 2) {
        Ptr master = readPtr(str + 0x18);
        if (!master || master == str) return std::string();
        Ptr mbuf = readPtr(master + 0x10);
        if (!mbuf) return std::string();
        buf = mbuf + buf;
    }
    if (!buf) return std::string();

    std::string out;
    for (uint32_t i = 0; i < len; i++) {
        char c = utf16 ? (char)(read<uint16_t>(buf + (size_t)i * 2) & 0xFF)
                       : (char)read<uint8_t>(buf + i);
        if (c < 0x20 || c == 0x7F) return std::string();
        out.push_back(c);
    }
    return out;
}

struct VelTrack {
    Ptr e = 0;
    double px = 0.0, py = 0.0;
    uint64_t t = 0;
    double ldx = 0.0, ldy = 0.0;
};

VelTrack velTracks[8];

void velFallback(Ptr e, Game::Entity& ent, bool& okVx, bool& okVy) {
    const uint64_t now = GetTickCount64();
    VelTrack* vt = nullptr;
    for (auto& s : velTracks) {
        if (s.e == e) { vt = &s; break; }
    }
    if (!vt) {
        for (auto& s : velTracks) {
            if (!s.e) { s.e = e; vt = &s; break; }
        }
    }
    if (!vt) vt = &velTracks[0];

    if (vt->t != 0 && now > vt->t && ent.hasX && ent.hasY) {
        const double dt = (double)(now - vt->t) / 1000.0;
        if (dt >= 0.008 && dt <= 0.4) {
            const double dx = (ent.x - vt->px) / dt;
            const double dy = (ent.y - vt->py) / dt;
            const bool dxOk = std::isfinite(dx) && std::fabs(dx) < 8000.0;
            const bool dyOk = std::isfinite(dy) && std::fabs(dy) < 8000.0;
            const bool dxSmooth = std::fabs(dx - vt->ldx) < 1400.0;
            const bool dySmooth = std::fabs(dy - vt->ldy) < 1400.0;
            if (dxSmooth && dxOk &&
                (std::fabs(dx) > 40.0 || !okVx || std::fabs(ent.velX) < 15.0)) {
                ent.velX = dx;
                okVx = true;
            }
            if (dySmooth && dyOk &&
                (std::fabs(dy) > 40.0 || !okVy || std::fabs(ent.velY) < 15.0)) {
                ent.velY = dy;
                okVy = true;
            }
            vt->ldx = dx;
            vt->ldy = dy;
        }
    }
    vt->px = ent.x;
    vt->py = ent.y;
    vt->t = now;
    ent.hasVel = okVx || okVy;
}

struct FlagTrust {
    Ptr e = 0;
    bool dodge = false;
    bool wall = false;
    bool stun = false;
    bool intan = false;
};

FlagTrust flagTrust[8];

FlagTrust* trustFor(Ptr e) {
    for (auto& s : flagTrust) {
        if (s.e == e) return &s;
    }
    for (auto& s : flagTrust) {
        if (!s.e) { s.e = e; return &s; }
    }
    flagTrust[0] = FlagTrust();
    flagTrust[0].e = e;
    return &flagTrust[0];
}

struct DiscRow {
    uint32_t off = 0;
    int n = 0;
    double v[8] = {};
};

struct AxisPick {
    size_t offs[9] = {};
    int n = 0;
    int primaryRow = -1;
};

struct PosFieldSet {
    Ptr store = 0;
    bool valid = false;
    uint64_t stamp = 0;
    size_t xRegs[9] = {};
    int xN = 0;
    size_t yRegs[9] = {};
    int yN = 0;
    uint32_t misses = 0;
};

PosFieldSet posFields[8];

PosFieldSet* fieldSlot(Ptr store, bool create) {
    if (!store) return nullptr;
    for (auto& s : posFields) {
        if (s.valid && s.store == store) return &s;
    }
    if (!create) return nullptr;
    PosFieldSet* v = &posFields[0];
    for (auto& s : posFields) {
        if (!s.valid) { v = &s; break; }
        if (s.stamp < v->stamp) v = &s;
    }
    *v = PosFieldSet();
    v->store = store;
    return v;
}

bool geoBounds(double& x, double& y, double& w, double& h) {
    if (!gameType) return false;
    Ptr lom = readPtr(gameType + G_LEVEL_MGR);
    if (!lom || readPtr(lom + LOM_BACKREF) != gameType) return false;
    Ptr b = readPtr(lom + LOM_GEO_BOUNDS);
    if (!b) return false;
    x = read<double>(b + RECT_X);
    y = read<double>(b + RECT_Y);
    w = read<double>(b + RECT_W);
    h = read<double>(b + RECT_H);
    return (w > 0.0) && w < 1.0e6 && (h > 0.0) && h < 1.0e6;
}

bool arenaBand(double& x0, double& x1, double& y0, double& y1) {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    if (!geoBounds(x, y, w, h)) return false;
    double m = (w > h ? w : h) * 0.35;
    if (m < 64.0) m = 64.0;
    x0 = x - m;
    x1 = x + w + m;
    y0 = y - m;
    y1 = y + h + m;
    return true;
}

AxisPick rankAxis(DiscRow* kept, int* idx, int cnt, int bodyN) {
    AxisPick pick;
    int bestG[9];
    int bestN = 0;
    for (int i = 0; i < cnt; i++) {
        int g[9];
        int gn = 0;
        g[gn++] = idx[i];
        for (int j = 0; j < cnt && gn < 9; j++) {
            if (j == i || kept[idx[j]].off == kept[idx[i]].off) continue;
            bool agree = true;
            for (int f = 0; f < bodyN; f++) {
                if (std::fabs(kept[idx[j]].v[f] - kept[idx[i]].v[f]) > 1.0) {
                    agree = false;
                    break;
                }
            }
            if (agree) g[gn++] = idx[j];
        }
        for (int a = 0; a + 1 < gn; a++) {
            for (int b = a + 1; b < gn; b++) {
                if (kept[g[b]].off < kept[g[a]].off) std::swap(g[a], g[b]);
            }
        }
        if (gn > bestN) {
            bestN = gn;
            for (int k = 0; k < gn; k++) bestG[k] = g[k];
        }
    }
    if (bestN > 0) {
        pick.primaryRow = bestG[0];
        for (int k = 0; k < bestN && pick.n < 9; k++)
            pick.offs[pick.n++] = kept[bestG[k]].off;
    }
    return pick;
}

void discoverPosFields(Ptr const* ents, int n) {
    static uint64_t next = 0;
    const uint64_t now = GetTickCount64();
    if (now < next) return;
    next = now + 1000;
    if (n < 2) return;

    double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
    if (!arenaBand(x0, x1, y0, y1)) return;

    Ptr bodies[8] = {};
    Ptr stores[8] = {};
    int bodyN = 0;
    bool need = false;
    for (int i = 0; i < n && bodyN < 8; i++) {
        if (!ents[i]) continue;
        Ptr st = readPtr(ents[i] + S_STORE);
        if (!st) continue;
        bodies[bodyN] = ents[i];
        stores[bodyN] = st;
        bodyN++;
        PosFieldSet* s = fieldSlot(st, false);
        if (!s || !s->valid || now - s->stamp > 30000) need = true;
    }
    if (bodyN < 2 || !need) return;

    DiscRow kept[32];
    int idxX[32], nX = 0;
    int idxY[32], nY = 0;
    int idxA[32], nA = 0;
    int keptN = 0;

    for (uint32_t off = 0x20; off < 0x300; off += 4) {
        DiscRow r;
        r.off = off;
        for (int i = 0; i < bodyN; i++) {
            uint32_t node = read<uint32_t>(bodies[i] + off);
            if (node == 0 || node >= S_SLOTS) continue;
            double v = 0.0;
            if (!decodeNode(bodies[i], node, v) || std::fabs(v) > 100000.0) continue;
            r.v[i] = v;
            r.n++;
        }
        if (r.n < bodyN) continue;

        bool inX = true, inY = true;
        double lo = r.v[0], hi = r.v[0];
        for (int i = 0; i < bodyN; i++) {
            const double v = r.v[i];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            if (v < x0 || v > x1) inX = false;
            if (v < y0 || v > y1) inY = false;
        }
        if (!inX && !inY) continue;
        if (hi - lo < 1.0) continue;
        if (keptN >= 32) continue;

        kept[keptN] = r;
        if (inX && inY) idxA[nA++] = keptN;
        else if (inX) idxX[nX++] = keptN;
        else idxY[nY++] = keptN;
        keptN++;
    }

    AxisPick px, py;
    if (nX > 0) px = rankAxis(kept, idxX, nX, bodyN);
    if (nY > 0) py = rankAxis(kept, idxY, nY, bodyN);
    for (int a = 0; a < nA && px.primaryRow >= 0 && px.n < 9; a++) {
        bool agree = true;
        for (int f = 0; f < bodyN; f++) {
            if (std::fabs(kept[idxA[a]].v[f] - kept[px.primaryRow].v[f]) > 1.0) {
                agree = false;
                break;
            }
        }
        if (agree) px.offs[px.n++] = kept[idxA[a]].off;
    }
    for (int a = 0; a < nA && py.primaryRow >= 0 && py.n < 9; a++) {
        bool agree = true;
        for (int f = 0; f < bodyN; f++) {
            if (std::fabs(kept[idxA[a]].v[f] - kept[py.primaryRow].v[f]) > 1.0) {
                agree = false;
                break;
            }
        }
        if (agree) py.offs[py.n++] = kept[idxA[a]].off;
    }
    if (px.n == 0 && py.n == 0) return;

    for (int i = 0; i < bodyN; i++) {
        PosFieldSet* s = fieldSlot(stores[i], true);
        if (!s) continue;
        *s = PosFieldSet();
        s->store = stores[i];
        s->valid = true;
        s->stamp = now;
        for (int k = 0; k < px.n; k++) s->xRegs[k] = px.offs[k];
        s->xN = px.n;
        for (int k = 0; k < py.n; k++) s->yRegs[k] = py.offs[k];
        s->yN = py.n;
    }
}

void noteDiscovered(Ptr store, bool ok) {
    PosFieldSet* s = fieldSlot(store, false);
    if (!s || !s->valid) return;
    if (ok) {
        s->misses = 0;
        s->stamp = GetTickCount64();
        return;
    }
    if (++s->misses >= 45) s->valid = false;
}

void resolveShifts(Ptr const* ents, int n) {
    static Ptr lastLocal = 0;
    static int cooldown = 0;
    const Ptr local = readPtr(gameType + G_LOCAL_ENTITY);
    if (local != lastLocal) {
        lastLocal = local;
        for (auto& s : shiftLocks) s = ShiftLock();
        cooldown = 0;
    }
    if (cooldown > 0) {
        cooldown--;
        return;
    }
    cooldown = 16;

    double lx = 0.0, ly = 0.0, lw = 0.0, lh = 0.0;
    if (!geoBounds(lx, ly, lw, lh)) return;
    const double x0 = lx;
    const double x1 = lx + lw;

    for (int i = 0; i < n; i++) {
        if (!ents[i]) continue;
        Ptr store = readPtr(ents[i] + S_STORE);
        if (!store) continue;
        bool done = false;
        for (auto& s : shiftLocks)
            if (s.store == store && s.locked) { done = true; break; }
        if (done) continue;
        Ptr sib = readPtr(store + S_SIBLING);
        if (!sib || read<uint32_t>(sib + S_NUM_LEN) != S_PHYS) continue;
        Ptr back = readPtr(sib + S_NUM_BACKING);
        if (!back) continue;
        uint32_t map[S_SLOTS] = {};
        if (!readSlotMap(store, readPtr(store + S_VECTOR), map)) continue;

        unsigned bestShift = 0;
        int bestAgree = 0;
        for (unsigned sh = 0; sh < S_PHYS; sh++) {
            double vals[8];
            int vn = 0;
            for (size_t reg : X_REGS) {
                const uint32_t node = read<uint32_t>(ents[i] + reg);
                if (node == 0 || node >= S_SLOTS) continue;
                const double v = read<double>(
                    back + S_NUM_VALUES + (size_t)((map[node] + sh) % S_PHYS) * 8);
                if (!std::isfinite(v) || v == 0.0 || std::fabs(v) > 20000.0) continue;
                if (v < x0 || v > x1) continue;
                if (vn < 8) vals[vn++] = v;
            }
            int agree = 0;
            for (int a = 0; a < vn; a++) {
                int c = 0;
                for (int b = 0; b < vn; b++)
                    if (std::fabs(vals[b] - vals[a]) <= 0.5 + 0.001 * std::fabs(vals[a])) c++;
                if (c > agree) agree = c;
            }
            if (agree > bestAgree) {
                bestAgree = agree;
                bestShift = sh;
            }
        }
        if (bestAgree < 2) continue;
        ShiftLock* sl = &shiftLocks[0];
        for (auto& s : shiftLocks)
            if (!s.store) { sl = &s; break; }
        sl->store = store;
        sl->shift = bestShift;
        sl->locked = true;
    }
}

bool rotFor(Ptr e, uint32_t& out) {
    const Ptr st = readPtr(e + S_STORE);
    if (!st) return false;
    for (auto& s : shiftLocks)
        if (s.store == st && s.locked) { out = s.shift; return true; }
    return false;
}

void readWeapon(Ptr e, Game::Entity& ent) {
    Ptr cc = readPtr(e + E_COMBAT);
    if (!cc) return;
    Ptr item = readPtr(cc + CC_HELD);
    if (!item) return;
    Ptr type = readPtr(item + HI_TYPE);
    if (!type) return;

    const int id = read<int>(type + I_ID);
    if (id <= 0 || id > 4095) return;

    ent.weaponKnown = true;
    ent.weaponId = id;
    ent.weaponIsWeapon = read<uint32_t>(type + I_IS_WEAPON) != 0;
    ent.weaponName = readString(type, T_NAME);
}

void readEntities(Ptr game, std::vector<Game::Entity>& out, Game::Debug& d) {
    out.clear();
    d.local = readPtr(game + G_LOCAL_ENTITY);
    d.roster = readPtr(game + G_ROSTER);
    d.rosterLen = 0;
    d.rosterOk = 0;

    Ptr data = 0;
    uint32_t len = 0;
    if (!d.roster || !readVector(d.roster, data, len)) return;
    d.rosterLen = len;

    for (uint32_t i = 0; i < len; i++) {
        Ptr e = 0;
        if (!decodeAtom(readPtr(data + V_ATOMS + (size_t)i * 8), e)) continue;
        if (!looksLikeEntity(e, game)) continue;
        d.rosterOk++;

        Game::Entity ent;
        ent.address = e;
        ent.isLocal = (e == d.local);
        ent.seat = read<uint32_t>(e + E_SEAT);
        ent.team = read<uint32_t>(e + E_TEAM);
        if (ent.seat > 8) ent.seat = 0;
        if (ent.team > 8) ent.team = 0;

        double dmg = read<double>(e + E_DAMAGE);
        ent.damageKnown = dmg >= 0.0 && dmg < 1000.0;
        ent.damage = ent.damageKnown ? dmg : 0.0;

        Ptr posStore = readPtr(e + S_STORE);
        size_t discX[9] = {};
        int discXN = 0;
        size_t discY[9] = {};
        int discYN = 0;
        if (posStore) {
            PosFieldSet* s = fieldSlot(posStore, false);
            if (s && s->valid) {
                for (int k = 0; k < s->xN; k++) discX[k] = s->xRegs[k];
                discXN = s->xN;
                for (int k = 0; k < s->yN; k++) discY[k] = s->yRegs[k];
                discYN = s->yN;
            }
        }

        double sxv = 0.0, syv = 0.0;
        bool pinX = readCoord(e, X_REGS, 5, sxv);
        bool pinY = readCoord(e, Y_REGS, 3, syv);
        if (pinX && pinY && std::fabs(sxv) < 1.0 && std::fabs(syv) < 1.0) {
            pinX = false;
            pinY = false;
        }
        double dxv = 0.0, dyv = 0.0;
        bool derX = discXN > 0 && readCoord(e, discX, (size_t)discXN, dxv);
        bool derY = discYN > 0 && readCoord(e, discY, (size_t)discYN, dyv);
        if (derX && derY && std::fabs(dxv) < 1.0 && std::fabs(dyv) < 1.0) {
            derX = false;
            derY = false;
        }
        ent.hasX = pinX || derX;
        ent.hasY = pinY || derY;
        if (ent.hasX) ent.x = pinX ? sxv : dxv;
        if (ent.hasY) ent.y = pinY ? syv : dyv;
        if (posStore && discXN > 0 && !pinX) noteDiscovered(posStore, ent.hasX);
        if (posStore && discYN > 0 && !pinY) noteDiscovered(posStore, ent.hasY);

        uint32_t st = 0;
        if (readMem(e + E_STATE, &st, 4) && st <= 0x7F) {
            ent.state = st;
            ent.stateKnown = true;
        }

        Ptr stats = readPtr(e + E_STATS);
        if (stats) {
            ent.deaths = read<uint32_t>(stats + ST_DEATHS);
            ent.deathsKnown = ent.deaths <= 99;
        }

        ent.entityWord = read<uint32_t>(e + E_WORD);

        uint32_t key = 0, enc = 0;
        if (readMem(e + E_JUMP_KEY, &key, 4) && readMem(e + E_JUMP_ENC, &enc, 4)) {
            ent.airJumps = key ^ enc;
            ent.airJumpsKnown = ent.airJumps <= 2;
        }

        FlagTrust* ft = trustFor(e);
        uint32_t raw = 0;
        bool pv = false;
        if (readMem(e + E_WAS_IN_AIR, &raw, 4) && packedBool(raw, pv)) ent.airborne = pv;
        if (readMem(e + E_FASTFALL, &raw, 4) && packedBool(raw, pv)) ent.fastFalling = pv;
        if (readMem(e + E_FACING, &raw, 4) && packedBool(raw, pv)) ent.facingLeft = !pv;
        if (readMem(e + E_DODGE, &raw, 4) && packedBool(raw, pv)) {
            if (!pv) ft->dodge = true;
            ent.dodge = pv && ft->dodge;
            ent.dodgeKnown = true;
        }
        if (readMem(e + E_WALL, &raw, 4) && packedBool(raw, pv)) {
            if (!pv) ft->wall = true;
            ent.wallSliding = pv && ft->wall;
            ent.wallKnown = true;
        }
        if (readMem(e + E_STUN, &raw, 4) && packedBool(raw, pv)) {
            if (!pv) ft->stun = true;
            ent.stunned = pv && ft->stun;
            ent.stunKnown = true;
        }
        if (readMem(e + E_INTANGIBLE, &raw, 4) && packedBool(raw, pv)) {
            if (!pv) ft->intan = true;
            ent.intangible = pv && ft->intan;
            ent.intanKnown = true;
        }

        // ═══════════════════════════════════════════════════════════
        // ⚡ BUNNYHOP STATE OKUMALARI (YENİ)
        // ═══════════════════════════════════════════════════════════
        {
            uint32_t raw = 0;
            bool pv = false;

            if (readMem(e + E_WAS_ON_GROUND, &raw, 4))
                ent.wasOnGroundThisFrame = (raw != 0);

            if (readMem(e + E_DID_LAND_OFF, &raw, 4))
                ent.didLand = (raw != 0);

            if (readMem(e + E_DID_DASH_JUMP_OFF, &raw, 4))
                ent.didDashJump = (raw != 0);

            if (readMem(e + E_DASH_FLIP_OFF, &raw, 4))
                ent.didDashFlip = (raw != 0);

            // Dash ID (doğrudan uint32)
            ent.dashId = read<uint32_t>(e + E_DASH_ID_OFF);
            if (ent.dashId > 100) ent.dashId = 0;

            // Air jump counter (doğrudan uint32)
            ent.airJumpCounter = read<uint32_t>(e + E_AIRJUMP_CNT);
            if (ent.airJumpCounter > 2) ent.airJumpCounter = 0;

            ent.bunnyFieldsKnown = true;
        }

        // ═══════════════════════════════════════════════════════════
        // ⚡ VELOCITY: keyVx (0x15C) ve keyVy (0x234)
        //    1) node = u32 @ fighter + keyOff
        //    2) value = decodeNode(fighter, node)  // register lookup
        //    3) sanity + position-mismatch check
        // ═══════════════════════════════════════════════════════════
        const uint32_t vxNode = read<uint32_t>(e + KEY_VX_OFF);
        const uint32_t vyNode = read<uint32_t>(e + KEY_VY_OFF);
        ent.keyVxNode = vxNode;
        ent.keyVyNode = vyNode;

        double rvx = 0.0, rvy = 0.0;

        bool okVx = (vxNode > 0 && vxNode < S_SLOTS) && decodeNode(e, vxNode, rvx);
        if (okVx) {
            if (!std::isfinite(rvx) || std::fabs(rvx) > kMaxRawVel) {
                okVx = false;
            } else if (ent.hasX && std::fabs(rvx - ent.x) < 0.5) {
                // Velocity pozisyonla aynı olamaz → yanlış slot
                okVx = false;
            }
        }

        bool okVy = (vyNode > 0 && vyNode < S_SLOTS) && decodeNode(e, vyNode, rvy);
        if (okVy) {
            if (!std::isfinite(rvy) || std::fabs(rvy) > kMaxRawVel) {
                okVy = false;
            } else if (ent.hasY && std::fabs(rvy - ent.y) < 0.5) {
                okVy = false;
            }
        }

        if (okVx) ent.velX = rvx * kVelScale;
        if (okVy) ent.velY = rvy * kVelScale;
        ent.hasVelX = okVx;
        ent.hasVelY = okVy;

        // derived fallback'ı hem X hem Y için çalıştır
        velFallback(e, ent, okVx, okVy);
        ent.hasVelX = okVx;
        ent.hasVelY = okVy;

        if (ent.isLocal) d.rotKnown = rotFor(e, d.rot);
        readWeapon(e, ent);
        out.push_back(ent);
    }
}

void readItems(Ptr game, std::vector<Game::Item>& out, Game::Debug& d) {
    out.clear();
    d.itemMgr = readPtr(game + G_ITEM_MGR);
    d.itemVec = 0;
    d.itemLen = 0;
    d.itemOk = 0;
    d.ground = 0;
    d.held = 0;
    if (!d.itemMgr) return;

    d.itemVec = readPtr(d.itemMgr + I_MGR_VECTOR);
    Ptr data = 0;
    uint32_t len = 0;
    if (!d.itemVec || !readVector(d.itemVec, data, len)) return;
    d.itemLen = len;

    for (uint32_t i = 0; i < len; i++) {
        Ptr e = 0;
        if (!decodeAtom(readPtr(data + V_ATOMS + (size_t)i * 8), e)) continue;

        Ptr type = readPtr(e + I_TYPE);
        int id = type ? read<int>(type + I_ID) : 0;
        uint32_t state = read<uint32_t>(e + I_STATE);
        uint32_t owner = read<uint32_t>(e + I_OWNER);
        double x = read<double>(e + I_X);
        double y = read<double>(e + I_Y);

        if (id <= 0 || id > 4095) continue;
        if (state > 9) continue;
        if (!(std::fabs(x) < 30000.0) || !(std::fabs(y) < 30000.0)) continue;
        if (std::fabs(x) < 1.0 && std::fabs(y) < 1.0) continue;
        d.itemOk++;

        Game::Item item;
        item.address = e;
        item.id = id;
        item.isWeapon = type ? read<uint32_t>(type + I_IS_WEAPON) != 0 : false;
        item.owner = owner;
        item.held = owner != 0;
        item.x = x;
        item.y = y;
        if (item.held)
            d.held++;
        else
            d.ground++;
        out.push_back(item);
    }
}

DWORD WINAPI workerProc(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    while (!stopFlag) {
        if (!gameType) {
            gameType = findGameType();
            if (!gameType) {
                Sleep(1000);
                continue;
            }
        }

        std::vector<Game::Entity> ents;
        std::vector<Game::Item> items;
        Game::Debug d;
        readEntities(gameType, ents, d);

        // ═══════════════════════════════════════════════════════════
        // ⚡ VELOCITY DEBUG için yerel oyuncunun ham değerlerini al
        // ═══════════════════════════════════════════════════════════
        for (const auto& en : ents) {
            if (!en.isLocal) continue;
            d.keyVx = en.keyVxNode;
            d.keyVy = en.keyVyNode;
            d.keyVxOk = en.hasVelX;
            d.keyVyOk = en.hasVelY;
            d.velX = en.velX;
            d.velY = en.velY;
            d.velOk = en.hasVel;
            break;
        }

        // ═══════════════════════════════════════════════════════════
        // 🗺️ MAP ID OKUMA
        // ═══════════════════════════════════════════════════════════
        d.levelMgr = readPtr(gameType + G_LEVEL_MGR);
        d.mapId = 0;
        d.mapIdAlt = 0;
        if (d.levelMgr) {
            // Asıl level ID (Level Manager içinde direkt)
            d.mapId = read<int>(d.levelMgr + LOM_LEVEL_ID);   // 0xA0

            // Level Type üzerinden alternatif ID
            Ptr levelType = readPtr(d.levelMgr + LOM_LEVEL_TYPE);   // 0xD0
            if (levelType) {
                d.mapIdAlt = read<int>(levelType + 0x80);           // LT_LEVEL_ID
            }
        }

        Ptr addrs[8];
        int na = 0;
        for (const auto& en : ents) {
            if (na < 8) addrs[na++] = en.address;
        }
        discoverPosFields(addrs, na);
        resolveShifts(addrs, na);
        readItems(gameType, items, d);
        d.camera = readPtr(gameType + G_CAMERA);

        {
            std::lock_guard<std::mutex> guard(lock);
            cachedEntities.swap(ents);
            cachedItems.swap(items);
            g_dbg = d;
        }

        Sleep(8);
    }
    return 0;
}

}

void Game::InitHooks() {
    if (running) return;
    loadAirRange();
    stopFlag = false;
    running = true;
    worker = CreateThread(nullptr, 0, workerProc, nullptr, 0, nullptr);
}

void Game::Shutdown() {
    if (!running) return;
    stopFlag = true;
    if (worker) {
        WaitForSingleObject(worker, 2000);
        CloseHandle(worker);
        worker = nullptr;
    }
    running = false;
}

bool Game::Ready() {
    return gameType != 0;
}

uintptr_t Game::GameType() {
    return gameType;
}

uintptr_t Game::LocalEntity() {
    return gameType ? readPtr(gameType + G_LOCAL_ENTITY) : 0;
}

std::vector<Game::Entity> Game::Entities() {
    std::lock_guard<std::mutex> guard(lock);
    return cachedEntities;
}

std::vector<Game::Item> Game::Items() {
    std::lock_guard<std::mutex> guard(lock);
    return cachedItems;
}

Game::Debug Game::Snapshot() {
    std::lock_guard<std::mutex> guard(lock);
    return g_dbg;
}

// ═══════════════════════════════════════════════════════════
// 🎮 GINPUT public API
// ═══════════════════════════════════════════════════════════
uintptr_t Game::InputMaskAddr() {
    if (!gameType) return 0;

    Ptr local = readPtr(gameType + G_LOCAL_ENTITY);
    if (!local) return 0;

    Ptr ctrl = readPtr(local + E_INPUT_CTRL);
    if (!ctrl) return 0;

    Ptr cmd = readPtr(ctrl + IN_CMD);
    if (!cmd) return 0;

    return cmd + IN_CMD_MASK;
}

uint32_t Game::ReadInputMask() {
    uintptr_t a = InputMaskAddr();
    return a ? read<uint32_t>(a) : 0;
}

bool Game::WriteInputMask(uint32_t bits) {
    uintptr_t a = InputMaskAddr();
    if (!a) return false;
    return writeMem(a, &bits, 4);
}

// ═══════════════════════════════════════════════════════════
// 🗺️ MAP / ARENA INFO
// ═══════════════════════════════════════════════════════════
Game::ArenaInfo Game::ReadArena() {
    ArenaInfo ai;
    if (!gameType) return ai;

    Ptr lom = readPtr(gameType + G_LEVEL_MGR);
    if (!lom || readPtr(lom + LOM_BACKREF) != gameType) return ai;

    // Level ID
    const int id = read<int>(lom + LOM_LEVEL_ID);
    if (id <= 0 || id > 99999) return ai;
    ai.levelId = id;

    // Level type
    const int type = read<int>(lom + LOM_LEVEL_TYPE);
    if (type >= 0 && type <= 32) ai.levelType = type;

    // Level name
    ai.levelName = readString(lom, LOM_LEVEL_NAME);

    // Geo bounds
    Ptr b = readPtr(lom + LOM_GEO_BOUNDS);
    if (b) {
        double x = read<double>(b + RECT_X);
        double y = read<double>(b + RECT_Y);
        double w = read<double>(b + RECT_W);
        double h = read<double>(b + RECT_H);
        if (w > 0 && w < 1e6 && h > 0 && h < 1e6) {
            ai.x0 = x;
            ai.x1 = x + w;
            ai.y0 = y;
            ai.y1 = y + h;
        }
    }

    ai.valid = true;
    return ai;
}

Game::Camera Game::ReadCamera(double screenW, double screenH) {
    Camera cam;
    if (!gameType || screenW < 1.0 || screenH < 1.0) return cam;

    Ptr obj = readPtr(gameType + G_CAMERA);
    if (!obj) return cam;

    Ptr bounds = readPtr(obj + C_CURRENT_BOUNDS);
    if (!bounds) return cam;

    double bx = read<double>(bounds + RECT_X);
    double by = read<double>(bounds + RECT_Y);
    double bw = read<double>(bounds + RECT_W);
    double bh = read<double>(bounds + RECT_H);
    if (!sane(bx) || !sane(by)) return cam;
    if (!(bw > 0.0) || bw > 1.0e6 || !(bh > 0.0) || bh > 1.0e6) return cam;

    double sw = read<double>(obj + C_SCREEN_W);
    double sh = read<double>(obj + C_SCREEN_H);
    if (!(sw > 0.0) || !(sh > 0.0) || sw > 1.0e5 || sh > 1.0e5) return cam;

    double shx = 0.0;
    double shy = 0.0;
    Ptr shake = readPtr(obj + C_SHAKE);
    if (shake) {
        shx = read<double>(shake + POINT_X);
        shy = read<double>(shake + POINT_Y);
        if (!sane(shx)) shx = 0.0;
        if (!sane(shy)) shy = 0.0;
    }

    const double ratio = screenW / sw;
    const double scale = screenW / bw;

    cam.camX = shx * ratio - bx * scale;
    cam.camY = shy * ratio - by * scale;
    cam.zoomX = scale;
    cam.zoomY = scale;
    cam.valid = sane(cam.camX) && sane(cam.camY) && scale > 1.0e-3 && scale < 20.0;
    return cam;
}
