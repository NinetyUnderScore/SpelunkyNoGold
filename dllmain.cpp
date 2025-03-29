#include <Windows.h>
#include <cstdint>
#include <vector>
#include <d3d9.h>
#include <MinHook.h>

// --- Globals for D3D9 hooking ---
typedef HRESULT(WINAPI* EndSceneFn)(IDirect3DDevice9*);
EndSceneFn g_originalEndScene = nullptr;
IDirect3DDevice9* g_d3dDevice = nullptr;

// --- Your existing code ---
#include <stdio.h>
#include <string>
#include <sstream>

// Helper function to format debug messages
void DebugPrint(const std::string& message) {
    OutputDebugStringA(message.c_str());
}

bool ReadSafe(uintptr_t address, void* buffer, size_t size) {
    HANDLE process = GetCurrentProcess();
    SIZE_T bytesRead;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), buffer, size, &bytesRead);
}

bool WriteSafe(uintptr_t address, void* buffer, size_t size) {
    HANDLE process = GetCurrentProcess();
    DWORD oldProtect;

    if (!VirtualProtectEx(process, reinterpret_cast<LPVOID>(address), size, PAGE_READWRITE, &oldProtect)) {
        return false;
    }

    SIZE_T bytesWritten;
    BOOL result = WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), buffer, size, &bytesWritten);
    VirtualProtectEx(process, reinterpret_cast<LPVOID>(address), size, oldProtect, &oldProtect);

    return result && (bytesWritten == size);
}

uintptr_t ResolvePointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets) {
    uintptr_t address = base;
    for (size_t i = 0; i < offsets.size(); ++i) {
        address += offsets[i];
        if (i != offsets.size() - 1 && (!ReadSafe(address, &address, sizeof(address)) || !address)) {
            return 0;
        }
    }
    return address;
}

void RunFrameLogic() {
    static uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandleA("Spelunky.exe"));
    static std::vector<uintptr_t> moneyOffsets = { 0x15446C, 0x44592C };
    static std::vector<uintptr_t> healthOffsets = { 0x154510, 0x30, 0x140 };

    static uintptr_t moneyAddress = ResolvePointerChain(baseAddress, moneyOffsets);
    static uintptr_t lastHealthAddress = 0;

    // Re-resolve health address if necessary
    uintptr_t healthAddress = ResolvePointerChain(baseAddress, healthOffsets);
    if (healthAddress != lastHealthAddress) {
        DebugPrint("[Spelunky] Health address updated: 0x" + std::to_string(healthAddress));
        lastHealthAddress = healthAddress;
    }

    // Check money and health
    int moneyValue;
    if (ReadSafe(moneyAddress, &moneyValue, sizeof(int)) && moneyValue > 0) {
        int currentHealth;
        if (ReadSafe(healthAddress, &currentHealth, sizeof(int)) && currentHealth > 0) {
            DebugPrint("[Spelunky] Killing player (frame " + std::to_string(GetTickCount64()) + ")");
            int newHealth = 0;
            WriteSafe(healthAddress, &newHealth, sizeof(int));
        }
    }
}

// --- Hooks ---
HRESULT WINAPI HookedEndScene(IDirect3DDevice9* device) {
    // Run your logic every frame
    RunFrameLogic();

    // Call the original EndScene to render the game
    return g_originalEndScene(device);
}

// --- Initialize hook ---
void InitializeD3DHook() {
    // Get D3D9 device
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return;

    D3DPRESENT_PARAMETERS params = { 0 };
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;

    HRESULT hr = d3d->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(),
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &g_d3dDevice
    );

    if (FAILED(hr)) {
        d3d->Release();
        return;
    }

    // Hook EndScene
    void** vTable = *reinterpret_cast<void***>(g_d3dDevice);
    MH_Initialize();
    MH_CreateHook(vTable[42], &HookedEndScene, (void**)&g_originalEndScene);
    MH_EnableHook(MH_ALL_HOOKS);

    d3d->Release();
}

// --- Thread to set up hook ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    // Wait for DirectX to initialize
    while (!g_d3dDevice) {
        InitializeD3DHook();
        Sleep(100);
    }
    return 0;
}

// --- DLL entry ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}