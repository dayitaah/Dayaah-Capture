#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

inline bool ValidFirmwareUuid(const BYTE* uuid) {
    bool allZero = true;
    bool allOnes = true;
    for (int i = 0; i < 16; ++i) {
        allZero = allZero && uuid[i] == 0;
        allOnes = allOnes && uuid[i] == 0xff;
    }
    return !allZero && !allOnes;
}

inline std::vector<BYTE> FirmwareUuid() {
    constexpr DWORD provider = (static_cast<DWORD>('R') << 24) |
                               (static_cast<DWORD>('S') << 16) |
                               (static_cast<DWORD>('M') << 8) |
                               static_cast<DWORD>('B');
    const UINT size = GetSystemFirmwareTable(provider, 0, nullptr, 0);
    if (size < 24 || size > 16 * 1024 * 1024) return {};
    std::vector<BYTE> data(size);
    if (GetSystemFirmwareTable(provider, 0, data.data(), size) != size) return {};
    if (size < 8) return {};
    DWORD tableLength = 0;
    std::memcpy(&tableLength, data.data() + 4, sizeof(tableLength));
    const size_t limit = std::min<size_t>(size, 8 + tableLength);
    size_t position = 8;
    while (position + 4 <= limit) {
        const BYTE type = data[position];
        const BYTE length = data[position + 1];
        if (length < 4 || position + length > limit) break;
        if (type == 1 && length >= 24 && ValidFirmwareUuid(data.data() + position + 8)) {
            return std::vector<BYTE>(data.begin() + position + 8,
                                     data.begin() + position + 24);
        }
        size_t next = position + length;
        while (next + 1 < limit && (data[next] != 0 || data[next + 1] != 0)) ++next;
        position = next + 2;
        if (type == 127) break;
    }
    return {};
}

inline std::wstring MachineGuid() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    wchar_t value[128]{};
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const LONG result = RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
                                         reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    value[_countof(value) - 1] = 0;
    return value;
}

inline std::wstring DeviceFingerprint() {
    std::uint64_t hash = 1469598103934665603ull;
    auto add = [&](const void* bytes, size_t count) {
        const BYTE* data = static_cast<const BYTE*>(bytes);
        for (size_t i = 0; i < count; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ull;
        }
    };
    const std::vector<BYTE> firmware = FirmwareUuid();
    if (!firmware.empty()) add(firmware.data(), firmware.size());
    const std::wstring machine = MachineGuid();
    if (!machine.empty()) add(machine.data(), machine.size() * sizeof(wchar_t));
    if (firmware.empty() && machine.empty()) {
        wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD length = _countof(computer);
        if (GetComputerNameW(computer, &length)) add(computer, length * sizeof(wchar_t));
    }
    wchar_t output[17]{};
    _snwprintf_s(output, _countof(output), _TRUNCATE, L"%016llx",
                 static_cast<unsigned long long>(hash));
    return output;
}
