#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "settings.h"
#include "session.h"
#include "tags.h"

namespace nsfplay_component {
struct TrackInfo {
    std::string title, album, artist, copyright, legacy_title, details;
    unsigned count = 0;
    double seconds = 0;
    int play_ms=0,loop_ms=0,fade_ms=0;
};

// The unmodified upstream engine has process-wide scratch buffers and tables.
// Serialize engine calls, including construction/destruction, for this first port.
class Engine {
public:
    using CheckAbort = std::function<void()>;
    explicit Engine(std::vector<uint8_t> image, const CheckAbort& check, Settings settings = defaults());
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    unsigned track_count() const;
    TrackInfo info(unsigned track, const CheckAbort& check, bool detailed = true);
    void initialize(unsigned track, const CheckAbort& check, bool interactive = false, bool no_loop = true, bool allow_fast_seek = false);
    unsigned render(int16_t* pcm, unsigned frames, const CheckAbort& check);
    void seek(double seconds, const CheckAbort& check);
    static constexpr unsigned rate = 48000, channels = 2, block_frames = 480;
    static constexpr unsigned max_block_frames = 960;
    unsigned sample_rate() const { return rate_; }
    unsigned output_channels() const { return channels_; }
    unsigned block_size() const { return rate_ / 100; }
    void attach_session(std::shared_ptr<PlaybackSession> session);
    void configure(const Settings& settings, const CheckAbort& check);
    void set_overrides(LegacyTags tags) { tags_=std::move(tags); }
    void set_source(std::string path) { source_=std::move(path); }
private:
    struct State;
    std::vector<uint8_t> image_;
    std::unique_ptr<State> state_;
    Settings settings_;
    LegacyTags tags_;
    std::string source_;
    std::shared_ptr<PlaybackSession> session_;
    unsigned rate_=rate, channels_=channels;
    bool interactive_=false, no_loop_=true;
    bool allow_fast_seek_=false;
    uint64_t last_snapshot_=0;
    double emulated_position_=0;
    void apply_live(const Settings& settings);
    void publish();
    unsigned count_ = 0, track_ = 0;
    uint64_t position_ = 0, end_ = 0;
};
}
