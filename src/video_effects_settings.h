#pragma once
#include <windows.h>
#include <cerrno>
#include <climits>
#include <cwchar>
#include <string>
#include "video_effects_model.h"

namespace video_effects_settings {
inline constexpr const wchar_t* kSection = L"video_effects_v1";
inline constexpr const wchar_t* kKeys[] = {L"sharpness", L"contrast", L"saturation", L"gamma",
    L"temperature", L"scanlines", L"mask", L"curvature"};
inline constexpr int VideoEffects::*kFields[] = {&VideoEffects::sharpness, &VideoEffects::contrast,
    &VideoEffects::saturation, &VideoEffects::gamma, &VideoEffects::temperature,
    &VideoEffects::scanlines, &VideoEffects::mask, &VideoEffects::curvature};
inline constexpr const wchar_t* kFlagKeys[] = {L"enabled", L"sharpen_enabled", L"color_enabled", L"crt_enabled"};
inline constexpr bool VideoEffects::*kFlags[] = {&VideoEffects::enabled, &VideoEffects::sharpenEnabled,
    &VideoEffects::colorEnabled, &VideoEffects::crtEnabled};
inline int ReadInt(const std::wstring& path, const wchar_t* key, int fallback) {
    wchar_t text[32]{};
    const DWORD count = GetPrivateProfileStringW(kSection, key, L"", text, 32, path.c_str());
    if (!count || count >= 31) return fallback;
    wchar_t* end = nullptr;
    errno = 0;
    const long value = std::wcstol(text, &end, 10);
    if (errno == ERANGE || end == text || *end || value < INT_MIN || value > INT_MAX) return fallback;
    return static_cast<int>(value);
}
inline VideoEffects Load(const std::wstring& path) {
    VideoEffects s;
    for (int i = 0; i < 8; ++i) s.*kFields[i] = ReadInt(path, kKeys[i], s.*kFields[i]);
    for (int i = 0; i < 4; ++i) s.*kFlags[i] = ReadInt(path, kFlagKeys[i], s.*kFlags[i] ? 1 : 0) == 1;
    s.Sanitize();
    return s;
}
inline bool Save(const std::wstring& path, VideoEffects s) {
    s.Sanitize();
    std::wstring section;
    auto append = [&](const wchar_t* key, int value) {
        section += key;
        section += L"=";
        section += std::to_wstring(value);
        section.push_back(L'\0');
    };
    for (int i = 0; i < 8; ++i) append(kKeys[i], s.*kFields[i]);
    for (int i = 0; i < 4; ++i) append(kFlagKeys[i], s.*kFlags[i] ? 1 : 0);
    section.push_back(L'\0');
    return WritePrivateProfileSectionW(kSection, section.c_str(), path.c_str()) != FALSE;
}
}
