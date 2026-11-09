#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <cassert>
#include <iostream>
#include "../src/video_effects_shader.h"
#include "../src/video_effects_settings.h"

int main() {
    for (int i = 0; i < 2; ++i) {
        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(kVideoEffectsShader, sizeof(kVideoEffectsShader) - 1, "DayaahOriginalEffects",
            nullptr, nullptr, i ? "PSMain" : "VSMain", i ? "ps_4_0" : "vs_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (errors) {
            std::cerr << static_cast<const char*>(errors->GetBufferPointer());
            errors->Release();
        }
        if (FAILED(hr)) { std::cerr << "Shader compile failed: " << std::hex << hr << "\n"; return 1; }
        assert(code && code->GetBufferSize() > 0);
        code->Release();
    }
    wchar_t dir[MAX_PATH]{}, temp[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, dir));
    assert(GetTempFileNameW(dir, L"dye", 0, temp));
    const std::wstring path = temp;
    VideoEffects s = video_effects_settings::Load(path);
    assert(!s.enabled && s.sharpness == 0 && s.gamma == 100);
    assert(WritePrivateProfileStringW(L"capture", L"video_device", L"keep-this", path.c_str()));
    s.enabled = true; s.crtEnabled = true; s.sharpness = 27; s.temperature = -65;
    s.contrast = 123; s.saturation = 147; s.gamma = 177; s.curvature = 31;
    assert(video_effects_settings::Save(path, s));
    assert(video_effects_settings::Load(path).Pack() == s.Pack());
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"capture", L"video_device", L"", value, 64, path.c_str());
    assert(std::wstring(value) == L"keep-this");
    WritePrivateProfileStringW(video_effects_settings::kSection, L"gamma", L"0", path.c_str());
    WritePrivateProfileStringW(video_effects_settings::kSection, L"sharpness", L"200", path.c_str());
    WritePrivateProfileStringW(video_effects_settings::kSection, L"temperature", L"-999", path.c_str());
    s = video_effects_settings::Load(path);
    assert(s.gamma == 50 && s.sharpness == 100 && s.temperature == -100);
    WritePrivateProfileStringW(video_effects_settings::kSection, L"gamma", L"NaN", path.c_str());
    WritePrivateProfileStringW(video_effects_settings::kSection, L"sharpness", L"999999999999999999999", path.c_str());
    s = video_effects_settings::Load(path);
    assert(s.gamma == 100 && s.sharpness == 0);
    assert(DeleteFileW(path.c_str()));
    std::cout << "HLSL VS/PS 4.0 compilation and INI persistence/corruption handling: PASS\n";
}
