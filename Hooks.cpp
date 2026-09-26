#include "Hooks.h"
#include "Globals.h"
#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <stdio.h>
#include <fstream>
#include <string>
#include "ESP.h"
#include "Bot.h"
#include "Combo.h"

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
extern "C" IMAGE_DOS_HEADER __ImageBase;

Present_t oPresent = nullptr;
ResizeBuffers_t oResizeBuffers = nullptr;
WNDPROC oWndProc = nullptr;

HWND Hooks::window = nullptr;
ID3D11Device* Hooks::pDevice = nullptr;
ID3D11DeviceContext* Hooks::pContext = nullptr;
ID3D11RenderTargetView* Hooks::mainRenderTargetView = nullptr;
bool init = false;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        Globals::ShowMenu = !Globals::ShowMenu;
        return true;
    }

    if (Globals::ShowMenu) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
            return true;
        // Commenting out the strict WantCaptureKeyboard/Mouse block
        // because it causes input queue lag loops in DirectX fullscreen
        // when the menu is just idling on screen.
    }

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void SetRenderTarget() {
    Hooks::pContext->OMSetRenderTargets(1, &Hooks::mainRenderTargetView, nullptr);
}

void CleanupRenderTarget() {
    if (Hooks::mainRenderTargetView) { Hooks::mainRenderTargetView->Release(); Hooks::mainRenderTargetView = nullptr; }
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    static bool s_defaultAxeTunesLoaded = false;
    if (!s_defaultAxeTunesLoaded) {
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA((HMODULE)&__ImageBase, modulePath, MAX_PATH);
        std::string savePath = modulePath;
        size_t slash = savePath.find_last_of("\\/");
        if (slash != std::string::npos) {
            savePath = savePath.substr(0, slash + 1);
        } else {
            savePath = ".\\";
        }
        savePath += "power_tuning_log.txt";

        std::ifstream in(savePath);
        if (in.is_open()) {
            std::string line;
            int freeSlot = 0;
            while (std::getline(in, line) && freeSlot < Globals::MaxAttackTunes) {
                if (line.empty() || line[0] == '#') continue;
                int pid = 0, wid = 0, en = 0, p1s = 0, p1e = 0, p2e = 0, fe = 0;
                float yo = 0.0f, xo = 0.0f, rxo = 0.0f, ryo = 0.0f;
                int parsed = sscanf_s(line.c_str(), "%d,%d,%d,%d,%d,%d,%d,%f,%f,%f,%f",
                                    &pid, &wid, &en, &p1s, &p1e, &p2e, &fe, &yo, &xo, &rxo, &ryo);
                if (parsed >= 8) {
                    auto& t = Globals::CalibTunes[freeSlot++];
                    t.enabled = (en != 0);
                    t.powerId = pid;
                    t.weaponId = wid;
                    t.phase1StartAdjust = p1s;
                    t.phase1EndAdjust = p1e;
                    t.phase2EndAdjust = p2e;
                    t.finalEndAdjust = fe;
                    t.yOffsetAdjust = yo;
                    t.xOffsetAdjust = xo;
                    t.rxAdjust = rxo;
                    t.ryAdjust = ryo;
                }
            }
        }
        s_defaultAxeTunesLoaded = true;
    }

    if (!init) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&Hooks::pDevice))) {
            Hooks::pDevice->GetImmediateContext(&Hooks::pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            Hooks::window = sd.OutputWindow;

            ID3D11Texture2D* pBackBuffer = nullptr;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            if (pBackBuffer) {
                Hooks::pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &Hooks::mainRenderTargetView);
                pBackBuffer->Release();
            }

            oWndProc = (WNDPROC)SetWindowLongPtr(Hooks::window, GWLP_WNDPROC, (LONG_PTR)WndProc);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;

            // Setup ImGui Dark Style
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 8.0f;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.5f, 0.8f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.1f, 0.4f, 0.7f, 1.0f);

            ImGui_ImplWin32_Init(Hooks::window);
            ImGui_ImplDX11_Init(Hooks::pDevice, Hooks::pContext);
            init = true;
        }
        else {
            return oPresent(pSwapChain, SyncInterval, Flags);
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // MAIN BOT MENU & ESP DRAWING
    Bot::Update();
    Combo::Update();
    ESP::Draw();

    if (Globals::ShowMenu) {
        ImGui::Begin("Sakqu Internal", &Globals::ShowMenu);
        
        // === Features ===
        ImGui::Text("Features");
        ImGui::Separator();
        ImGui::Checkbox("Enable ESP", &Globals::EnableESP);
        if (Globals::EnableESP)
            ImGui::SliderInt("Box Outline Thickness", &Globals::ESPBorderThickness, 1, 4);
        ImGui::Checkbox("Enable Map ESP", &Globals::EnableMapESP);
        if (Globals::EnableMapESP) {
            ImGui::Checkbox("Map ESP Fill", &Globals::MapESPFill);
            ImGui::SliderInt("Map ESP Thickness", &Globals::MapESPThickness, 1, 3);
        }
        ImGui::Checkbox("Enable Auto Attack", &Globals::EnableAutoAttack);
        ImGui::Checkbox("Enable Auto Dodge", &Globals::EnableAutoDodge);
        ImGui::Checkbox("Show Hit Display", &Globals::ShowHitDisplay);
        ImGui::Checkbox("Show Dynamic Hitboxes", &Globals::ShowDynamicHitboxes);
        ImGui::Checkbox("Show Player States", &Globals::ShowState);

        // ═══════════════════════════════════════════════════════════
        // ⚡ BUNNYHOP BÖLÜMÜ (YENİ)
        // ═══════════════════════════════════════════════════════════
        ImGui::Spacing();
        ImGui::Text("Bunnyhop");
        ImGui::Separator();

        ImGui::Checkbox("Enable Bunnyhop", &Globals::EnableBunnyhop);
        ImGui::SameLine();
        ImGui::TextColored(
            Globals::EnableBunnyhop ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f)
                                    : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            Globals::EnableBunnyhop ? "[ACTIVE]" : "[IDLE]");

        ImGui::Checkbox("Debug Overlay", &Globals::BunnyhopDebug);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Dodge→Jump (ms)", &Globals::BunnyhopDodgeToJumpMs, 0, 50);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Jump Hold (ms)", &Globals::BunnyhopJumpHoldMs, 20, 120);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Jump→S (ms)", &Globals::BunnyhopJumpToFallMs, 0, 80);

        // ═══════════════════════════════════════════════════════════
        // 🔨 AUTO COMBO (Hammer / Scythe / Gauntlets)
        // ═══════════════════════════════════════════════════════════
        ImGui::Spacing();
        ImGui::Text("Auto Combo (GInput)");
        ImGui::Separator();

        ImGui::Checkbox("Enable Auto Combo", &Globals::EnableHammerCombo);
        ImGui::SameLine();
        ImGui::TextColored(
            Globals::EnableHammerCombo ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f)
                                        : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            Globals::EnableHammerCombo ? "[ACTIVE]" : "[IDLE]");

        ImGui::Checkbox("Show Combo Debug Overlay", &Globals::HammerComboDebug);
        ImGui::Checkbox("Auto Attack (kullanıcı basmıyorsa)", &Globals::HammerAutoAttack);
        ImGui::Checkbox("Use Direction (sadece havada)", &Globals::HammerUseDirection);
        ImGui::Checkbox("Use Recovery (W+K falling)", &Globals::HammerUseRecovery);
        ImGui::Checkbox("Enable Combo Chains (hit sonrası)", &Globals::HammerComboChains);
        ImGui::Checkbox("Whiff Block (ıskalarsa bekle)", &Globals::HammerWhiffBlock);
        ImGui::Checkbox("Dodge-Aware (dodge'a göre combo)", &Globals::ActOnEnemyDodge);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Post-Dodge Settle (ms)", &Globals::DodgeSettleMs, 0, 400);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Prediction (ms)", &Globals::HammerPredictionMs, 0, 200);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Ground Range (C)", &Globals::HammerGroundRange, 60, 200);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Air Vertical Tolerance", &Globals::HammerAirVertical, 20, 90);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Air Side Range (SAir)", &Globals::HammerAirSide, 60, 180);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Air Below Range (DAir)", &Globals::HammerAirBelow, 40, 150);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Recovery Min Dist", &Globals::HammerRecoveryMinDist, 40, 200);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Recovery Max Dist", &Globals::HammerRecoveryMaxDist, 150, 400);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Attack Hold (ms)", &Globals::HammerAttackHoldMs, 20, 120);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Recovery (ms)", &Globals::HammerRecoveryMs, 80, 400);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Follow-up Window (ms)", &Globals::HammerFollowupMs, 50, 300);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Whiff Block (ms)", &Globals::HammerWhiffBlockMs, 100, 1000);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Hit Confirm Window (ms)", &Globals::HammerHitConfirmMs, 80, 400);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Max Combo Depth", &Globals::HammerMaxComboDepth, 1, 5);

        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Hammer Weapon ID", &Globals::HammerWeaponId, 0, 255);

        ImGui::TextDisabled("0 = isimden otomatik tanı (overlay'de görünen kit'e bak)");
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Scythe Weapon ID", &Globals::ScytheWeaponId, 0, 255);
        ImGui::SetNextItemWidth(220);
        ImGui::SliderInt("Gauntlets Weapon ID", &Globals::GauntletWeaponId, 0, 255);

        ImGui::TextWrapped("Kit otomatik seçilir (Hammer/Scythe/Gauntlets). "
                           "YERDE: yakın→C, orta→S+C (DLight) ya da yön+C. "
                           "HAVADA: yukarı→C, aşağı→S+C, yana→yön+C. "
                           "Iskalarsa %.0fms bekler. Debug'da Stage +%dms takip et.",
                           (float)Globals::HammerWhiffBlockMs,
                           Globals::HammerDirLeadMs + Globals::HammerAttackHoldMs);

        ImGui::Spacing();
        ImGui::Text("Calibration");
        ImGui::Separator();
        ImGui::Text("Tune per-attack timing using PowerID.");
        ImGui::Text("Observed PowerID: %d", Globals::CalibObservedPowerId);
        ImGui::Text("Observed WeaponID: %d", Globals::CalibObservedWeaponId);
        ImGui::Text("Recent local cast IDs:");
        if (Globals::CalibObservedPowerIdsCount <= 0) {
            ImGui::Text("  (none yet)");
        } else {
            for (int i = 0; i < Globals::CalibObservedPowerIdsCount; i++) {
                ImGui::Text("  #%d: %d", i + 1, Globals::CalibObservedPowerIds[i]);
            }
        }
        if (ImGui::Button("Use observed PowerID")) {
            Globals::CalibEditPowerId = Globals::CalibObservedPowerId;
            Globals::CalibEditWeaponId = Globals::CalibObservedWeaponId;
        }
        ImGui::SameLine();
        if (ImGui::Button("Use recent #1") && Globals::CalibObservedPowerIdsCount > 0) {
            Globals::CalibEditPowerId = Globals::CalibObservedPowerIds[0];
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear recent IDs")) {
            Globals::CalibObservedPowerIdsCount = 0;
            for (int i = 0; i < (int)(sizeof(Globals::CalibObservedPowerIds) / sizeof(Globals::CalibObservedPowerIds[0])); i++) {
                Globals::CalibObservedPowerIds[i] = 0;
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputInt("Edit PowerID", &Globals::CalibEditPowerId);
        if (Globals::CalibEditPowerId < 0) Globals::CalibEditPowerId = 0;
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputInt("Edit WeaponID (0=Any)", &Globals::CalibEditWeaponId);
        if (Globals::CalibEditWeaponId < 0) Globals::CalibEditWeaponId = 0;

        int tuneSlot = -1;
        int freeSlot = -1;
        for (int i = 0; i < Globals::MaxAttackTunes; i++) {
            if (Globals::CalibTunes[i].enabled &&
                Globals::CalibTunes[i].powerId == Globals::CalibEditPowerId &&
                Globals::CalibTunes[i].weaponId == Globals::CalibEditWeaponId) {
                tuneSlot = i;
                break;
            }
            if (!Globals::CalibTunes[i].enabled && freeSlot < 0) freeSlot = i;
        }

        if (tuneSlot < 0 && Globals::CalibEditPowerId > 0 && freeSlot >= 0) {
            if (ImGui::Button("Create tuning for this PowerID")) {
                auto& t = Globals::CalibTunes[freeSlot];
                t.enabled = true;
                t.powerId = Globals::CalibEditPowerId;
                t.weaponId = Globals::CalibEditWeaponId;
                t.phase1StartAdjust = 0;
                t.phase1EndAdjust = 0;
                t.phase2EndAdjust = 0;
                t.finalEndAdjust = 0;
                t.yOffsetAdjust = 0.0f;
                t.xOffsetAdjust = 0.0f;
                t.rxAdjust = 0.0f;
                t.ryAdjust = 0.0f;
                tuneSlot = freeSlot;
            }
        }

        if (tuneSlot >= 0) {
            auto& t = Globals::CalibTunes[tuneSlot];
            ImGui::Text("Editing slot %d (PowerID %d, WeaponID %d)", tuneSlot, t.powerId, t.weaponId);
            ImGui::Checkbox("Enable this tune", &t.enabled);
            ImGui::SliderInt("Phase1 Start Phase", &t.phase1StartAdjust, -180, 180);
            ImGui::SliderInt("Phase1 End Phase", &t.phase1EndAdjust, -180, 180);
            ImGui::SliderInt("Phase2 End Phase", &t.phase2EndAdjust, -180, 180);
            ImGui::SliderInt("Final End Phase", &t.finalEndAdjust, -240, 240);
            ImGui::SliderFloat("Hitbox Y Offset", &t.yOffsetAdjust, -300.0f, 300.0f, "%.1f");
            ImGui::SliderFloat("Hitbox X Offset", &t.xOffsetAdjust, -300.0f, 300.0f, "%.1f");
            ImGui::SliderFloat("Hitbox Width (+)", &t.rxAdjust, -300.0f, 300.0f, "%.1f");
            ImGui::SliderFloat("Hitbox Height (+)", &t.ryAdjust, -300.0f, 300.0f, "%.1f");
            if (ImGui::Button("Reset this tuning")) {
                t.phase1StartAdjust = 0;
                t.phase1EndAdjust = 0;
                t.phase2EndAdjust = 0;
                t.finalEndAdjust = 0;
                t.yOffsetAdjust = 0.0f;
                t.xOffsetAdjust = 0.0f;
                t.rxAdjust = 0.0f;
                t.ryAdjust = 0.0f;
            }
        } else {
            ImGui::Text("No tuning yet for this PowerID.");
        }

        if (ImGui::Button("Save tuning log")) {
            char modulePath[MAX_PATH] = {};
            GetModuleFileNameA((HMODULE)&__ImageBase, modulePath, MAX_PATH);
            std::string savePath = modulePath;
            size_t slash = savePath.find_last_of("\\/");
            if (slash != std::string::npos) {
                savePath = savePath.substr(0, slash + 1);
            } else {
                savePath = ".\\";
            }
            savePath += "power_tuning_log.txt";

            bool needHeader = false;
            {
                std::ifstream in(savePath, std::ios::binary);
                if (!in.good() || in.peek() == std::ifstream::traits_type::eof()) {
                    needHeader = true;
                }
            }

            std::ofstream out(savePath, std::ios::app);
            if (needHeader) {
                out << "# Power tuning export\n";
                out << "# powerId,weaponId,enabled,p1Start,p1End,p2End,finalEnd,yOffset,xOffset,rx,ry\n";
            }
            if (tuneSlot >= 0) {
                const auto& t = Globals::CalibTunes[tuneSlot];
                out << t.powerId << ","
                    << t.weaponId << ","
                    << (t.enabled ? 1 : 0) << ","
                    << t.phase1StartAdjust << ","
                    << t.phase1EndAdjust << ","
                    << t.phase2EndAdjust << ","
                    << t.finalEndAdjust << ","
                    << t.yOffsetAdjust << ","
                    << t.xOffsetAdjust << ","
                    << t.rxAdjust << ","
                    << t.ryAdjust << "\n";
            }
            out.close();
            static std::string s_lastPath;
            s_lastPath = savePath;
            Globals::CalibLastSavePath = s_lastPath.c_str();
        }
        ImGui::TextWrapped("Last save path: %s", Globals::CalibLastSavePath);

        ImGui::Spacing();
        ImGui::Text("Global baseline (shared):");
        ImGui::Text("Ground extra=%d | Air extra=%d | Air Y extra=%.1f",
                    Globals::CalibGroundCastFrameExtra, Globals::CalibAirCastFrameExtra, Globals::CalibAirHitboxYOffsetExtra);
        
        // === Keybinds ===
        ImGui::Spacing();
        ImGui::Text("Keybinds");
        ImGui::Separator();
        
        // Helper lambda: show key name from VK code
        auto VkName = [](int vk) -> const char* {
            static char buf[8];
            if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
            if (vk >= '0' && vk <= '9') { buf[0] = (char)vk; buf[1] = 0; return buf; }
            switch(vk) {
                case VK_SHIFT: return "SHIFT"; case VK_CONTROL: return "CTRL";
                case VK_SPACE: return "SPACE"; case VK_UP: return "UP";
                case VK_DOWN: return "DOWN"; case VK_LEFT: return "LEFT";
                case VK_RIGHT: return "RIGHT"; default: sprintf(buf,"0x%02X",vk); return buf;
            }
        };
        
        // Keybind capture: click button, press key to assign
        static int* capturingKey = nullptr;
        auto KeybindButton = [&](const char* label, int* keyVar) {
            char btnLabel[64];
            if (capturingKey == keyVar) {
                sprintf(btnLabel, "[...] ##%s", label);
                // Scan for key press
                for (int vk = 0x08; vk <= 0x5A; vk++) {
                    if (vk == VK_INSERT) continue; // reserved for menu toggle
                    if (GetAsyncKeyState(vk) & 1) {
                        *keyVar = vk;
                        capturingKey = nullptr;
                        break;
                    }
                }
            } else {
                sprintf(btnLabel, "%s ##%s", VkName(*keyVar), label);
            }
            ImGui::Text("%s", label); ImGui::SameLine(120);
            if (ImGui::Button(btnLabel, ImVec2(60, 0))) {
                capturingKey = keyVar;
            }
        };
        
        ImGui::Spacing();
        KeybindButton("Move Up", &Globals::KeyUp);
        KeybindButton("Move Down", &Globals::KeyDown);
        KeybindButton("Move Left", &Globals::KeyLeft);
        KeybindButton("Move Right", &Globals::KeyRight);
        ImGui::Spacing();
        KeybindButton("Light Attack", &Globals::KeyLight);
        KeybindButton("Heavy Attack", &Globals::KeyHeavy);
        KeybindButton("Dodge", &Globals::KeyDodge);
        KeybindButton("Jump", &Globals::KeyJump);
        KeybindButton("Pickup", &Globals::KeyPickup);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Press [INS] to toggle menu.");
        ImGui::Text("Press [F1] to detach.");
        ImGui::End();
    }

    // TODO: Add ESP Drawing here

    ImGui::Render();
    SetRenderTarget();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return oPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    CleanupRenderTarget();
    HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (SUCCEEDED(hr)) {
        ID3D11Texture2D* pBackBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        if (pBackBuffer) {
            Hooks::pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &Hooks::mainRenderTargetView);
            pBackBuffer->Release();
        }
    }
    return hr;
}

void Hooks::Initialize() {
    if (MH_Initialize() != MH_OK) return;

    // Dummy device to get SwapChain vtable
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = GetForegroundWindow();
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* dummyDevice = nullptr;
    ID3D11DeviceContext* dummyContext = nullptr;
    IDXGISwapChain* dummySwapChain = nullptr;

    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1, D3D11_SDK_VERSION, &sd, &dummySwapChain, &dummyDevice, nullptr, &dummyContext))) {
        void** pVTable = *reinterpret_cast<void***>(dummySwapChain);
        dummySwapChain->Release();
        dummyDevice->Release();
        dummyContext->Release();

        // IDXGISwapChain::Present is index 8
        MH_CreateHook(pVTable[8], &hkPresent, reinterpret_cast<void**>(&oPresent));
        // IDXGISwapChain::ResizeBuffers is index 13
        MH_CreateHook(pVTable[13], &hkResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers));

        MH_EnableHook(MH_ALL_HOOKS);
    }
}

void Hooks::Shutdown() {
    MH_DisableHook(MH_ALL_HOOKS);
    if (oWndProc) SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    CleanupRenderTarget();
    if (init) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    MH_Uninitialize();
}
