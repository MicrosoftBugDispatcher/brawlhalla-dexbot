#pragma once
#include <windows.h>
#include <d3d11.h>

namespace Hooks {
    void Initialize();
    void Shutdown();
    
    // Internal state
    extern HWND window;
    extern ID3D11Device* pDevice;
    extern ID3D11DeviceContext* pContext;
    extern ID3D11RenderTargetView* mainRenderTargetView;
}
