#pragma once

#include <cstddef>
#include <cstdint>

class DiscoSecret {
public:
    bool Input(wchar_t character, std::uint64_t tickMs) {
        if (progress_ && (tickMs < previousTick_ || tickMs - previousTick_ > 3000)) {
            progress_ = 0;
        }
        previousTick_ = tickMs;
        if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
        constexpr wchar_t code[] = L"DISCO";
        progress_ = character == code[progress_] ? progress_ + 1
                   : character == code[0] ? 1 : 0;
        if (progress_ != 5) return false;
        progress_ = 0;
        return true;
    }

    void Reset() {
        progress_ = 0;
        previousTick_ = 0;
    }

private:
    std::size_t progress_ = 0;
    std::uint64_t previousTick_ = 0;
};
