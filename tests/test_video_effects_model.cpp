#include "../src/video_effects_model.h"
#include <atomic>
#include <cassert>
#include <climits>
#include <iostream>
#include <random>
#include <thread>

static bool Equal(const VideoEffects& a, const VideoEffects& b) {
    return a.enabled == b.enabled && a.sharpenEnabled == b.sharpenEnabled &&
        a.colorEnabled == b.colorEnabled && a.crtEnabled == b.crtEnabled &&
        a.sharpness == b.sharpness && a.contrast == b.contrast && a.saturation == b.saturation &&
        a.gamma == b.gamma && a.temperature == b.temperature && a.scanlines == b.scanlines &&
        a.mask == b.mask && a.curvature == b.curvature;
}
int main() {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    VideoEffects s;
    assert(!s.HasWork());
    s.enabled = true; assert(!s.HasWork());
    s.sharpness = 50; assert(s.HasWork());
    s.sharpenEnabled = false; assert(!s.HasWork());
    s.temperature = -20; assert(s.HasWork());
    s.colorEnabled = false; assert(!s.HasWork());
    s.crtEnabled = true; assert(s.HasWork());
    s.scanlines = s.mask = s.curvature = 0; assert(!s.HasWork());
    s.curvature = 1; assert(s.HasWork());
    s.enabled = false; assert(!s.HasWork());

    // Detect overlapping bit fields, sign loss and boundary errors.
    std::mt19937 random(14001);
    for (int i = 0; i < 50000; ++i) {
        VideoEffects v;
        v.enabled = random() & 1; v.sharpenEnabled = random() & 1;
        v.colorEnabled = random() & 1; v.crtEnabled = random() & 1;
        v.sharpness = int(random() % 101); v.contrast = 50 + int(random() % 101);
        v.saturation = int(random() % 201); v.gamma = 50 + int(random() % 151);
        v.temperature = int(random() % 201) - 100; v.scanlines = int(random() % 101);
        v.mask = int(random() % 101); v.curvature = int(random() % 101);
        assert(Equal(v, VideoEffects::Unpack(v.Pack())));
    }
    s.sharpness = INT_MAX; s.gamma = INT_MIN; s.temperature = INT_MIN;
    s.saturation = INT_MAX; s.contrast = INT_MIN;
    const auto clamped = VideoEffects::Unpack(s.Pack());
    assert(clamped.sharpness == 100 && clamped.gamma == 50 && clamped.temperature == -100 &&
           clamped.saturation == 200 && clamped.contrast == 50);

    VideoEffects a; a.enabled = true; a.sharpness = 100; a.temperature = -100;
    VideoEffects b; b.enabled = true; b.gamma = 200; b.curvature = 100; b.crtEnabled = true;
    const auto ap = a.Pack(), bp = b.Pack();
    std::atomic<std::uint64_t> mailbox{ap};
    std::thread writer([&] {
        for (int i = 0; i < 100000; ++i) mailbox.store(i & 1 ? ap : bp, std::memory_order_relaxed);
    });
    for (int i = 0; i < 100000; ++i) {
        const auto v = VideoEffects::Unpack(mailbox.load(std::memory_order_relaxed));
        assert(Equal(v, a) || Equal(v, b));
    }
    writer.join();
    std::cout << "Effects defaults, bypass, clamps, 50000 round trips and concurrent publication: PASS\n";
}
