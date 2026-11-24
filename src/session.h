#pragma once
#include "settings.h"
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace nsfplay_component {
struct ChannelState {
    bool present=false, key=false;
    int volume=0, maximum=1, tone=-1, output=0, note=0;
    double frequency=0;
    uint32_t period=0;
    std::vector<int16_t> wave;
};
struct Snapshot {
    double seconds=0, emulated_seconds=0, length=0;
    bool detected=false, ended=false;
    int play_ms=0,loop_ms=0,fade_ms=0;
    Settings settings;
    std::array<ChannelState,32> channels;
    ChannelState envelope,noise;
    unsigned memory_address=0;
    std::array<uint8_t,256> memory{};
};
struct PlaybackSession {
    std::mutex mutex;
    std::string path, file_info, playlist, error;
    unsigned subsong=1;
    bool alive=true;
    uint64_t epoch=0;
    std::deque<Snapshot> history;
    std::optional<Settings> pending;
    Settings effective;
    std::vector<std::pair<uint16_t,uint8_t>> writes;
    unsigned memory_address=0;
    bool want_dump=false;
    std::vector<uint8_t> memory_dump;
    void configure(const Settings& s) { std::lock_guard lock(mutex); pending=s; }
    void write(uint16_t address,uint8_t value) { std::lock_guard lock(mutex); writes.emplace_back(address,value); }
    std::optional<Snapshot> at(double seconds) {
        std::lock_guard lock(mutex);
        if(history.empty()) return {};
        for(auto i=history.rbegin();i!=history.rend();++i) if(i->seconds<=seconds) return *i;
        return history.front();
    }
};
}
