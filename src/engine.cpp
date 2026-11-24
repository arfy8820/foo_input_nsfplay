#include "engine.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <utility>
#include <stdexcept>
#include "../../bbbradsmith/nsfplay/xgm/player/nsf/nsfplay.h"

namespace nsfplay_component {
namespace {
std::timed_mutex& engine_mutex() { static std::timed_mutex value; return value; }
std::unique_lock<std::timed_mutex> lock_engine(const Engine::CheckAbort& check) {
    std::unique_lock<std::timed_mutex> lock(engine_mutex(), std::defer_lock);
    do { check(); } while (!lock.try_lock_for(std::chrono::milliseconds(10)));
    check();
    return lock;
}
std::string text(const char* value) { return value ? value : ""; }
void validate_chunks(const std::vector<uint8_t>& image, size_t offset, bool standalone) {
    bool info = !standalone, data = !standalone, end = false;
    while (offset < image.size()) {
        if (image.size() - offset < 8) throw std::runtime_error("Truncated NSFe chunk header");
        uint32_t size = 0;
        for (unsigned i = 0; i < 4; ++i) size |= uint32_t(image[offset + i]) << (8 * i);
        const auto* id = image.data() + offset + 4;
        offset += 8;
        if (size > image.size() - offset) throw std::runtime_error("Truncated NSFe chunk data");
        if (!std::memcmp(id, "INFO", 4)) info = true;
        if (!std::memcmp(id, "DATA", 4)) data = true;
        if (!std::memcmp(id, "NEND", 4)) { end = true; break; }
        offset += size;
    }
    if (standalone && (!info || !data || !end))
        throw std::runtime_error("NSFe requires INFO, DATA and NEND chunks");
}
void validate_image(const std::vector<uint8_t>& image) {
    if (image.size() < 4 || image.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Unsupported NSF file size");
    if (!std::memcmp(image.data(), "NSFE", 4)) validate_chunks(image, 4, true);
    else {
        if (image.size() < 128 || std::memcmp(image.data(), "NESM\x1a", 5))
            throw std::runtime_error("Not an NSF or NSFe file");
        const uint32_t suffix = uint32_t(image[125]) | (uint32_t(image[126]) << 8) | (uint32_t(image[127]) << 16);
        if (suffix) {
            if (suffix > image.size() - 128) throw std::runtime_error("Invalid NSF metadata offset");
            validate_chunks(image, 128 + suffix, false);
        }
    }
}
}

class HostPlayer : public xgm::NSFPlayer {
public:
    // NSFPlay's block API truncates elapsed milliseconds per call. Anchor it to
    // the adapter's accumulated sample clock, including at 11025/22050 Hz.
    void synchronize_time(double seconds) { time_in_ms=static_cast<int>(std::min(seconds*1000.0,double(INT32_MAX))); }
};
struct Engine::State {
    xgm::NSFPlayerConfig config;
    xgm::NSF nsf;
    HostPlayer player; // destroyed before its data and configuration
    explicit State(std::vector<uint8_t>& image, const Settings& values, bool interactive=false) {
        for (const auto& spec : parameters()) {
            if (!config.HasValue(spec.key)) continue;
            if(spec.kind==ParamKind::text || spec.kind==ParamKind::color) config[spec.key]=values.at(spec.key);
            else config[spec.key]=static_cast<int>(static_cast<uint32_t>(integer(values,spec.key)));
        }
        // The upstream fixed-size title formatter is not used by this adapter.
        // Keep logger titles bounded; the component formats exported titles safely.
        config["TITLE_FORMAT"]="NSFPlay";
        if(!interactive)config["LOG_CPU"]=0;
        // LoadFile() normally sets these fields after parsing a PLSITEM. We use
        // the memory loader to support foobar2000 virtual files instead.
        nsf.filename[0] = '\0';
        nsf.total_songs = nsf.songs = 0;
        nsf.bodysize = 0;
        nsf.speed_dendy = 0;
        nsf.playlist_mode = false;
        nsf.enable_multi_tracks = true;
        nsf.title_unknown = true;
        nsf.playtime_unknown = true;
        nsf.time_in_ms = nsf.loop_in_ms = nsf.fade_in_ms = nsf.loop_num = -1;
        nsf.SetDefaults(config["PLAY_TIME"], config["FADE_TIME"], config["LOOP_NUM"]);
        if (!nsf.Load(image.data(), static_cast<xgm::UINT32>(image.size())))
            throw std::runtime_error(text(nsf.LoadError()));
        if (!nsf.total_songs || nsf.bodysize <= 0)
            throw std::runtime_error("NSF contains no songs or program data");
        const bool banked=std::any_of(std::begin(nsf.bankswitch),std::end(nsf.bankswitch),[](auto n){return n!=0;});
        if(banked&&uint64_t(nsf.bodysize)+(nsf.load_address&0xfff)>0x100000)
            throw std::runtime_error("Banked NSF program exceeds the engine's 256-bank capacity");
        if (nsf.nsfe_plst) {
            if (nsf.nsfe_plst_size <= 0 || nsf.nsfe_plst_size > 255)
                throw std::runtime_error("Unsupported NSFe playlist size");
            for (int i = 0; i < nsf.nsfe_plst_size; ++i)
                if (nsf.nsfe_plst[i] >= nsf.total_songs)
                    throw std::runtime_error("Invalid NSFe playlist song");
        }
        player.SetConfig(&config);
        if (!player.Load(&nsf)) throw std::runtime_error("NSFPlay could not load this file");
    }
};

Engine::Engine(std::vector<uint8_t> image, const CheckAbort& check, Settings settings) : image_(std::move(image)), settings_(std::move(settings)) {
    check();
    validate_image(image_);
    auto lock = lock_engine(check);
    state_ = std::make_unique<State>(image_, settings_);
    rate_=static_cast<unsigned>(integer(settings_,"RATE"));
    channels_=static_cast<unsigned>(integer(settings_,"NCH"));
    count_ = state_->nsf.songs;
}
Engine::~Engine() {
    if(session_) { std::lock_guard lock(session_->mutex); session_->alive=false; }
    std::lock_guard<std::timed_mutex> lock(engine_mutex());
    state_.reset();
}
unsigned Engine::track_count() const { return count_; }

TrackInfo Engine::info(unsigned track, const CheckAbort& check, bool detailed) {
    if (track >= count_) throw std::runtime_error("Invalid NSF subsong");
    auto lock = lock_engine(check);
    auto& nsf = state_->nsf;
    const int saved = nsf.song;
    const int saved_time=nsf.time_in_ms,saved_loop=nsf.loop_in_ms,saved_fade=nsf.fade_in_ms;
    nsf.time_in_ms=nsf.loop_in_ms=nsf.fade_in_ms=-1;
    nsf.SetSong(track);
    // Copy all strings before restoring song; never expose upstream scratch data.
    const unsigned physical = nsf.nsfe_plst ? nsf.nsfe_plst[track] : track;
    TrackInfo result;
    result.album = text(nsf.title);
    result.title = text(nsf.nsfe_entry[physical].tlbl);
    if (result.title.empty()) result.title = result.album + " (" + std::to_string(track + 1) + ")";
    result.artist = text(nsf.nsfe_entry[physical].taut);
    if (result.artist.empty()) result.artist = text(nsf.artist);
    result.copyright = text(nsf.copyright);
    result.count = count_;
    result.seconds = std::max(0, nsf.GetLength()) / 1000.0;
    result.play_ms=nsf.GetPlayTime();result.loop_ms=nsf.GetLoopTime();result.fade_ms=nsf.GetFadeTime();
    if(auto it=tags_.songs.find(track+1);it!=tags_.songs.end()) {
        const auto& t=it->second;
        if(!t.title.empty())result.title=t.title;if(!t.album.empty())result.album=t.album;if(!t.artist.empty())result.artist=t.artist;if(!t.copyright.empty())result.copyright=t.copyright;
        if(t.play_ms>=0)result.play_ms=t.play_ms;if(t.loop_ms>=0)result.loop_ms=t.loop_ms;if(t.fade_ms>=0)result.fade_ms=t.fade_ms;
        result.seconds=(int64_t(result.play_ms)+int64_t(result.loop_ms)*std::max(0,static_cast<int>(integer(settings_,"LOOP_NUM"))-1)+result.fade_ms)/1000.0;
    }
    const auto format=settings_.at("TITLE_FORMAT");
    for(size_t i=0;i<format.size()&&result.legacy_title.size()<4096;++i) {
        if(format[i]!='%' || i+1==format.size()) { result.legacy_title+=format[i]; continue; }
        const char token=format[++i];
        switch(token) {
        case 'F': case 'f': { const auto slash=source_.find_last_of("/\\");result.legacy_title+=source_.substr(slash==std::string::npos?0:slash+1);break; }
        case 'P': case 'p': { const auto slash=source_.find_last_of("/\\");result.legacy_title+=slash==std::string::npos?std::string{}:source_.substr(0,slash);break; }
        case 'T': case 't': result.legacy_title+=result.album; break;
        case 'A': case 'a': result.legacy_title+=result.artist; break;
        case 'C': case 'c': result.legacy_title+=result.copyright; break;
        case 'L': case 'l': result.legacy_title+=text(nsf.nsfe_entry[physical].tlbl); break;
        case 'n': result.legacy_title+=std::to_string(track+1); break;
        case 'e': result.legacy_title+=std::to_string(count_); break;
        case 's': result.legacy_title+=std::to_string(nsf.start); break;
        case '%': result.legacy_title+='%'; break;
        case 'N': case 'E': case 'S': {
            char buf[16]; std::snprintf(buf,sizeof(buf),"$%02X",token=='N'?track+1:token=='E'?count_:nsf.start); result.legacy_title+=buf; break;
        }
        default: break;
        }
    }
    if(result.legacy_title.size()>4096)result.legacy_title.resize(4096);
    if(detailed) {
    std::ostringstream details;
    details<<"Title: "<<result.album<<"\r\nArtist: "<<result.artist<<"\r\nCopyright: "<<result.copyright
        <<"\r\nRipper: "<<text(nsf.ripper)<<"\r\nSubsong: "<<track+1<<" / "<<count_<<" (physical song "<<physical+1<<")"
        <<"\r\nNSF version: "<<unsigned(nsf.version)<<"\r\nRegion flags: "<<unsigned(nsf.regn)
        <<"\r\nEffective region: "<<state_->player.GetRegion(nsf.regn,nsf.regn_pref)
        <<" (0 NTSC, 1 PAL, 2 Dendy)\r\nNTSC / PAL / Dendy tick period (us): "
        <<nsf.speed_ntsc<<" / "<<nsf.speed_pal<<" / "<<nsf.speed_dendy
        <<"\r\nLoad / init / play: $"<<std::hex<<std::setw(4)<<std::setfill('0')<<nsf.load_address<<" / $"<<std::setw(4)<<nsf.init_address<<" / $"<<std::setw(4)<<nsf.play_address
        <<"\r\nBanks:";
    for(auto bank:nsf.bankswitch)details<<" "<<std::setw(2)<<unsigned(bank);
    details<<std::dec<<"\r\nChips: APU";
    if(nsf.use_fds)details<<" FDS"; if(nsf.use_mmc5)details<<" MMC5"; if(nsf.use_fme7)details<<" 5B";
    if(nsf.use_vrc6)details<<" VRC6"; if(nsf.use_vrc7)details<<" VRC7"; if(nsf.use_n106)details<<" N163";
    details<<"\r\nNSF2 flags: "<<unsigned(nsf.nsf2_bits)<<"\r\n\r\nEmbedded tracks (physical order):\r\n";
    for(unsigned i=0;i<nsf.total_songs;++i) details<<i+1<<": "<<text(nsf.nsfe_entry[i].tlbl)<<" | "<<text(nsf.nsfe_entry[i].taut)<<" | time "<<nsf.nsfe_entry[i].time<<" ms, fade "<<nsf.nsfe_entry[i].fade<<" ms"<<(nsf.nsfe_entry[i].psfx?" [SFX]":"")<<"\r\n";
    if(nsf.nsfe_plst) { details<<"\r\nNSFe playlist:"; for(int i=0;i<nsf.nsfe_plst_size;++i)details<<" "<<unsigned(nsf.nsfe_plst[i])+1; details<<"\r\n"; }
    if(nsf.text)details<<"\r\nNSFe text:\r\n"<<std::string(nsf.text,std::min<uint32_t>(nsf.text_len,65536));
    result.details=details.str();
    }
    result.seconds/=double(integer(settings_,"MULT_SPEED"))/256.0;
    nsf.song = saved;
    nsf.time_in_ms=saved_time;nsf.loop_in_ms=saved_loop;nsf.fade_in_ms=saved_fade;
    return result;
}

void Engine::initialize(unsigned track, const CheckAbort& check, bool interactive, bool no_loop, bool allow_fast_seek) {
    if (track >= count_) throw std::runtime_error("Invalid NSF subsong");
    auto lock = lock_engine(check);
    // Reparse the original image: an NSF program may modify its own memory.
    interactive_=interactive; no_loop_=no_loop;allow_fast_seek_=allow_fast_seek;
    state_ = std::make_unique<State>(image_,settings_,interactive_);
    auto& s = *state_;
    s.player.SetPlayFreq(rate_);
    s.player.SetChannels(channels_);
    s.player.SetSong(track);
    if(auto it=tags_.songs.find(track+1);it!=tags_.songs.end()) {
        auto& t=it->second;const unsigned physical=s.nsf.nsfe_plst?s.nsf.nsfe_plst[track]:track;
        if(t.play_ms>=0) { s.nsf.nsfe_entry[physical].time=t.play_ms;s.nsf.time_in_ms=t.play_ms;s.nsf.playtime_unknown=false; }
        if(t.fade_ms>=0)s.nsf.nsfe_entry[physical].fade=t.fade_ms;
        if(t.loop_ms>=0)s.nsf.loop_in_ms=t.loop_ms;
        if(t.loop_ms>0 && int64_t(t.loop_ms)*std::max(0,s.nsf.GetLoopNum())+s.nsf.GetPlayTime()+s.nsf.GetFadeTime()>INT32_MAX)
            throw std::runtime_error("Sidecar loop duration exceeds the engine's time range");
    }
    if(!interactive_||no_loop_) s.config["PLAY_ADVANCE"]=0;
    if(interactive_&&integer(settings_,"MASK_INIT")) s.config["MASK"]=0;
    end_ = static_cast<uint64_t>(std::max(0, s.nsf.GetLength())) * rate_ / 1000;
    s.player.Reset();
    track_ = track;
    position_ = 0;
    emulated_position_=0; last_snapshot_=0;
    auto effective=settings_;
    effective["MASK"]=std::to_string(static_cast<uint32_t>(s.config["MASK"].GetInt()));
    apply_live(effective);
    if(session_) { std::lock_guard guard(session_->mutex); session_->history.clear(); ++session_->epoch; }
    publish();
}

unsigned Engine::render(int16_t* pcm, unsigned frames, const CheckAbort& check) {
    std::optional<Settings> changes;
    std::vector<std::pair<uint16_t,uint8_t>> writes;
    if(session_) {
        std::lock_guard guard(session_->mutex);
        changes.swap(session_->pending); writes.swap(session_->writes);
    }
    auto lock = lock_engine(check);
    if(changes) apply_live(*changes);
    for(auto [address,value]:writes) state_->player.cpu.Write(address,value);
    const bool infinite=interactive_&&!no_loop_&&integer(settings_,"PLAY_ADVANCE")==1;
    if ((!infinite && emulated_position_ >= double(end_)/rate_) || state_->player.IsStopped()) return 0;
    frames=std::min(frames,block_size());
    const double speed=double(integer(settings_,"MULT_SPEED"))/256.0;
    if(!infinite) frames=std::min(frames,static_cast<unsigned>(std::max(0.0,std::ceil((double(end_)/rate_-emulated_position_)*rate_/speed-1e-7))));
    if(!frames) return 0;
    const bool was_fading=state_->player.fader.IsFading();
    const double fade_begin=double(std::max(0,state_->nsf.GetLength()-state_->nsf.GetFadeTime()))/1000.0;
    if(!infinite&&!was_fading&&fade_begin>emulated_position_)frames=std::min(frames,static_cast<unsigned>(std::ceil((fade_begin-emulated_position_)*rate_/speed-1e-7)));
    if(!frames)frames=1;
    state_->player.synchronize_time(emulated_position_);
    const bool start_fade=!infinite&&!was_fading&&emulated_position_+1e-8>=fade_begin;
    if(start_fade)state_->player.FadeOut(static_cast<int>(std::lround(state_->nsf.GetFadeTime()/speed)));
    const unsigned got = state_->player.Render(pcm, frames);
    position_ += got;
    emulated_position_+=double(got)/rate_*speed;
    state_->player.synchronize_time(emulated_position_);
    if(!infinite&&!was_fading&&!start_fade&&(state_->player.fader.IsFading()||emulated_position_+1e-8>=fade_begin))state_->player.FadeOut(static_cast<int>(std::lround(state_->nsf.GetFadeTime()/speed)));
    if(session_ && (position_-last_snapshot_>=rate_/integer(settings_,"INFO_FREQ") || state_->player.IsStopped())) publish();
    return got;
}

void Engine::seek(double seconds, const CheckAbort& check) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid seek time");
    const double speed=double(integer(settings_,"MULT_SPEED"))/256.0;
    const auto target = static_cast<uint64_t>(std::min(seconds * rate_, static_cast<double>(end_)/speed) + 0.5);
    if (target < position_) initialize(track_, check,interactive_,no_loop_,allow_fast_seek_);
    std::array<int16_t, max_block_frames * channels> discard;
    while (position_ < target) {
        if(allow_fast_seek_&&integer(settings_,"FAST_SEEK")) {
            auto lock=lock_engine(check);
            const auto frames=static_cast<unsigned>(std::min<uint64_t>(block_size(),target-position_));
            if(state_->player.IsStopped())break;
            state_->player.synchronize_time(emulated_position_);
            const auto got=state_->player.Skip(frames);position_+=got;emulated_position_+=double(got)/rate_*speed;state_->player.synchronize_time(emulated_position_);
            if(!got)break;
            continue;
        }
        if (!render(discard.data(), static_cast<unsigned>(std::min<uint64_t>(block_size(), target - position_)), check))
            break;
    }
    { auto lock=lock_engine(check); if(session_) { std::lock_guard guard(session_->mutex);session_->history.clear();++session_->epoch; } publish(); }
}
void Engine::attach_session(std::shared_ptr<PlaybackSession> session) { session_=std::move(session); }
void Engine::configure(const Settings& settings,const CheckAbort& check) { auto lock=lock_engine(check); apply_live(settings); }
void Engine::apply_live(const Settings& values) {
    auto& s=*state_;
    bool quality=false;
    for(const auto& p:parameters()) if(p.live) {
        quality |= p.key=="QUALITY" && settings_.at(p.key)!=values.at(p.key);
        settings_[p.key]=values.at(p.key);
        if(s.config.HasValue(p.key)) {
            if(p.kind==ParamKind::color) s.config[p.key]=values.at(p.key);
            else s.config[p.key]=static_cast<int>(static_cast<uint32_t>(integer(values,p.key)));
        }
    }
    if(!interactive_||no_loop_) s.config["PLAY_ADVANCE"]=0;
    if(quality) s.player.SetPlayFreq(rate_);
    s.config.Notify(-1);
    const auto mask=static_cast<uint32_t>(integer(settings_,"MASK"));
    s.player.apu->SetMask(mask&3); s.player.dmc->SetMask((mask>>2)&7);
    s.player.fds->SetMask((mask>>5)&1); s.player.mmc5->SetMask((mask>>6)&7);
    s.player.fme7->SetMask((mask>>9)&7); s.player.vrc6->SetMask((mask>>12)&7);
    s.player.vrc7->SetMask(((mask>>15)&63)|(((mask>>29)&7)<<6));
    s.player.n106->SetMask((mask>>21)&255);
    s.player.rconv.SetFastSkip(integer(settings_,"FAST_SEEK")!=0);
}
void Engine::publish() {
    if(!session_) return;
    auto& s=*state_;
    Snapshot snap;
    snap.settings=settings_; snap.seconds=double(position_)/rate_; snap.emulated_seconds=emulated_position_;
    snap.length=snap.seconds+std::max(0.0,std::max(0,s.nsf.GetLength())/1000.0-emulated_position_)/(double(integer(settings_,"MULT_SPEED"))/256.0);
    snap.detected=s.player.IsDetected(); snap.ended=s.player.IsStopped();
    snap.play_ms=s.nsf.GetPlayTime();snap.loop_ms=s.nsf.GetLoopTime();snap.fade_ms=s.nsf.GetFadeTime();
    const bool present[]={true,true,s.nsf.use_fme7,s.nsf.use_mmc5,s.nsf.use_n106,s.nsf.use_vrc6,s.nsf.use_vrc7,s.nsf.use_fds};
    for(unsigned i=0;i<32;++i) {
        auto& dest=snap.channels[i];
        dest.present=present[channel_devices[i]] && (i<29 || s.nsf.vrc7_type==1 || integer(settings_,"VRC7_OPTION0"));
        if(!dest.present) continue;
        auto ti=dynamic_cast<xgm::ITrackInfo*>(s.player.GetInfo(-1,xgm::NSFPlayerConfig::channel_track[i]));
        if(!ti) continue;
        dest.key=ti->GetKeyStatus(); dest.volume=ti->GetVolume(); dest.maximum=std::max(1,ti->GetMaxVolume());
        dest.frequency=ti->GetFreqHz(); dest.period=ti->GetFreq(); dest.tone=ti->GetTone(); dest.output=ti->GetOutput(); dest.note=std::max(0,xgm::ITrackInfo::GetNote(dest.frequency)-36);
        if(auto f=dynamic_cast<xgm::TrackInfoFDS*>(ti)) dest.wave.assign(f->wave,f->wave+64);
        else if(auto n=dynamic_cast<xgm::TrackInfoN106*>(ti)) dest.wave.assign(n->wave,n->wave+std::clamp(n->wavelen,0,256));
        else if(i==0||i==1||i==6||i==7||i==12||i==13) {
            const int duty=std::max(0,dest.tone);const int apu_duty[]={4,8,16,24};
            for(int j=0;j<32;++j) dest.wave.push_back(j < (i<12?apu_duty[duty%4]:(duty%8+1)*4) ? 15 : 0);
        } else if(i==2) for(int j=0;j<32;++j)dest.wave.push_back(j<16?15-j:j-16);
        else if(i==14) for(int j=0;j<32;++j)dest.wave.push_back(j);
    }
    if(s.nsf.use_fme7) {
        auto copy=[](ChannelState& out,xgm::ITrackInfo* ti) {if(!ti)return;out.present=true;out.key=ti->GetKeyStatus();out.frequency=ti->GetFreqHz();out.period=ti->GetFreq();out.tone=ti->GetTone();out.output=ti->GetOutput();};
        copy(snap.envelope,dynamic_cast<xgm::ITrackInfo*>(s.player.GetInfo(-1,xgm::NSFPlayer::FME7_TRK3)));
        copy(snap.noise,dynamic_cast<xgm::ITrackInfo*>(s.player.GetInfo(-1,xgm::NSFPlayer::FME7_TRK4)));
    }
    bool dump=false;
    { std::lock_guard guard(session_->mutex); snap.memory_address=session_->memory_address&0xff00;dump=std::exchange(session_->want_dump,false); }
    for(unsigned i=0;i<256;++i) { uint32_t v=0; if(!s.player.bank.Read(snap.memory_address+i,v))s.player.mem.Read(snap.memory_address+i,v); snap.memory[i]=static_cast<uint8_t>(v); }
    std::vector<uint8_t> memory;
    if(dump) {memory.resize(65536);for(unsigned i=0;i<65536;++i){uint32_t v=0;if(!s.player.bank.Read(i,v))s.player.mem.Read(i,v);memory[i]=static_cast<uint8_t>(v);}}
    last_snapshot_=position_;
    std::lock_guard guard(session_->mutex);
    session_->effective=settings_;
    if(dump)session_->memory_dump=std::move(memory);
    session_->history.push_back(std::move(snap));
    while(session_->history.size()>size_t(integer(settings_,"INFO_FREQ"))*12) session_->history.pop_front();
}
}
