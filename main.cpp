#include <windows.h>
#include "Hooks.h"
#include "Globals.h"
#include "Game.h"
#include "Bot.h"
#include "Combo.h"

DWORD WINAPI MainThread(LPVOID lpReserved) {
    Hooks::Initialize();
    Game::InitHooks();
    Bot::Start();
    Combo::Start();

    while (!Globals::Unload) {
        if (GetAsyncKeyState(VK_F1) & 1) {
            Globals::Unload = true;
        }
        Sleep(10);
    }

    Combo::Stop();
    Bot::Stop();
    Game::Shutdown();
    Hooks::Shutdown();
    FreeLibraryAndExitThread((HMODULE)lpReserved, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE hThread = CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}
