#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace dusk::profiler {
namespace detail {

inline constexpr int kMaxZones = 24;
inline constexpr int kHistory = 180;

struct Zone {
    const char* name = nullptr;
    std::atomic<uint64_t> curUs{0};
    std::atomic<uint32_t> curCalls{0};
    double avgMs = 0.0;
    double peakMs = 0.0;
    double avgCalls = 0.0;
};

inline std::array<Zone, kMaxZones>& zones() {
    static std::array<Zone, kMaxZones> z{};
    return z;
}
inline int& zoneCount() {
    static int n = 0;
    return n;
}
inline int registerZone(const char* name) {
    int i = zoneCount()++;
    if (i < kMaxZones) {
        zones()[static_cast<size_t>(i)].name = name;
    }
    return i;
}

inline std::array<float, kHistory>& frameHistory() {
    static std::array<float, kHistory> h{};
    return h;
}
inline int& historyPos() {
    static int p = 0;
    return p;
}
inline uint64_t& frameCountRef() {
    static uint64_t f = 0;
    return f;
}
inline double& frameAvgRef() {
    static double v = 0.0;
    return v;
}
inline double& frameP95Ref() {
    static double v = 0.0;
    return v;
}
inline std::chrono::steady_clock::time_point& frameStartRef() {
    static std::chrono::steady_clock::time_point t{};
    return t;
}

inline void endFrameSnapshot(double frameMs) {
    frameCountRef()++;
    double& avg = frameAvgRef();
    avg += (frameMs - avg) * 0.05;
    auto& h = frameHistory();
    h[static_cast<size_t>(historyPos() % kHistory)] = static_cast<float>(frameMs);
    historyPos()++;
    if (frameCountRef() >= 30) {
        std::array<float, kHistory> sorted = h;
        size_t n = frameCountRef() < static_cast<uint64_t>(kHistory)
                       ? static_cast<size_t>(frameCountRef())
                       : static_cast<size_t>(kHistory);
        std::sort(sorted.begin(), sorted.begin() + n);
        frameP95Ref() = sorted[n * 95 / 100];
    } else {
        frameP95Ref() = frameMs;
    }
    for (int i = 0; i < zoneCount() && i < kMaxZones; ++i) {
        Zone& z = zones()[static_cast<size_t>(i)];
        double sampleMs = static_cast<double>(z.curUs.exchange(0)) / 1000.0;
        uint32_t calls = z.curCalls.exchange(0);
        z.avgMs += (sampleMs - z.avgMs) * 0.06;
        z.peakMs = sampleMs > z.peakMs ? sampleMs : z.peakMs * 0.995;
        z.avgCalls += (static_cast<double>(calls) - z.avgCalls) * 0.06;
    }
}

class ScopedZone {
  public:
    explicit ScopedZone(int idx)
        : mIdx(idx < kMaxZones ? idx : -1), mStart(std::chrono::steady_clock::now()) {}
    ~ScopedZone() {
        if (mIdx < 0) {
            return;
        }
        auto us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                  mStart)
                .count());
        Zone& z = zones()[static_cast<size_t>(mIdx)];
        z.curUs.fetch_add(us, std::memory_order_relaxed);
        z.curCalls.fetch_add(1, std::memory_order_relaxed);
    }
    ScopedZone(const ScopedZone&) = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;

  private:
    int mIdx;
    std::chrono::steady_clock::time_point mStart;
};

} // namespace detail

inline int numZones() {
    return detail::zoneCount() < detail::kMaxZones ? detail::zoneCount() : detail::kMaxZones;
}
inline const char* zoneName(int i) { return detail::zones()[static_cast<size_t>(i)].name; }
inline double zoneAvgMs(int i) { return detail::zones()[static_cast<size_t>(i)].avgMs; }
inline double zonePeakMs(int i) { return detail::zones()[static_cast<size_t>(i)].peakMs; }
inline double zoneAvgCalls(int i) { return detail::zones()[static_cast<size_t>(i)].avgCalls; }
inline uint64_t frameCount() { return detail::frameCountRef(); }
inline double frameAvgMs() { return detail::frameAvgRef(); }
inline double frameP95Ms() { return detail::frameP95Ref(); }

inline void beginFrame() { detail::frameStartRef() = std::chrono::steady_clock::now(); }
inline void endFrame() {
    double ms = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - detail::frameStartRef())
                                        .count()) /
                1000.0;
    detail::endFrameSnapshot(ms);
}

} // namespace dusk::profiler

#define DUSK_PROFILE(name)                                                                     \
    static int duskProfZone##__LINE__ = ::dusk::profiler::detail::registerZone(name);           \
    ::dusk::profiler::detail::ScopedZone duskProfScope##__LINE__(duskProfZone##__LINE__)
#define DUSK_PROFILE_FRAME_BEGIN() ::dusk::profiler::beginFrame()
#define DUSK_PROFILE_FRAME_END() ::dusk::profiler::endFrame()
