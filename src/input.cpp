#include <foobar2000/SDK/foobar2000-lite.h>
#include <foobar2000/SDK/componentversion.h>
#include <foobar2000/SDK/input_impl.h>
#include <foobar2000/SDK/audio_chunk.h>
#include <foobar2000/SDK/file_info.h>
#include <foobar2000/SDK/input_file_type.h>
#include <foobar2000/SDK/file_info_filter.h>
#include <foobar2000/SDK/file_info_impl.h>
#include <array>
#include <cstring>
#include <cstdio>
#include "engine.h"
#include "host.h"
#include "tags.h"
#include <filesystem>

DECLARE_COMPONENT_VERSION("NSFPlay", "0.3.0",
    "NSF/NSFe decoder with preferences, mixer and chip inspector.\n"
    "NSFPlay by Brezza, maintained by Brad Smith.\n"
    "https://bbbradsmith.github.io/nsfplay/");
VALIDATE_COMPONENT_FILENAME("foo_input_nsfplay.dll");

namespace {
class input_nsfplay : public input_stubs {
public:
    void open(file::ptr hint, const char* path, t_input_open_reason reason, abort_callback& abort) {
        path_=path; native_=nsfplay_component::native_path(path);
        if(reason==input_open_info_write&&native_.empty())throw exception_tagging_unsupported();
        settings_=nsfplay_component::host_settings();
        if(nsfplay_component::integer(settings_,"LOG_CPU")) {
            auto file=std::filesystem::u8path(settings_.at("LOG_CPU_FILE"));
            if(file.is_relative())file=std::filesystem::u8path(nsfplay_component::profile_folder())/file;
            const auto utf8=file.u8string();settings_["LOG_CPU_FILE"]={utf8.begin(),utf8.end()};
        }
        file_ = hint;
        input_open_file_helper(file_, path, reason==input_open_info_write?input_open_info_read:reason, abort);
        file_->reopen(abort);
        constexpr size_t limit = 16 * 1024 * 1024;
        const auto size = file_->get_size(abort);
        if (size != filesize_invalid && (size < 4 || size > limit))
            throw exception_io_data("NSF file size outside supported range (4 bytes to 16 MiB)");
        std::vector<uint8_t> image;
        std::array<uint8_t, 65536> block;
        for (;;) {
            abort.check();
            const auto got = file_->read(block.data(), block.size(), abort);
            if (!got) break;
            if (image.size() + got > limit) throw exception_io_data("NSF file exceeds 16 MiB");
            image.insert(image.end(), block.begin(), block.begin() + got);
        }
        if (image.size() < 4 || (std::memcmp(image.data(), "NSFE", 4) != 0 &&
            (image.size() < 128 || std::memcmp(image.data(), "NESM\x1a", 5) != 0)))
            throw exception_io_data("Not an NSF or NSFe file");
        try {
            engine_ = std::make_unique<nsfplay_component::Engine>(std::move(image), [&] { abort.check(); },settings_);
            engine_->set_source(native_.empty()?path_:native_);
            tags_=nsfplay_component::read_tags(native_);
            if(nsfplay_component::integer(settings_,"READ_TAGINFO")||tags_.owned||reason==input_open_info_write)engine_->set_overrides(tags_);
        }
        catch (const exception_aborted&) { throw; }
        catch (const std::exception& e) { throw exception_io_data(e.what()); }
    }
    unsigned get_subsong_count() { return engine_->track_count(); }
    t_uint32 get_subsong(unsigned index) { return index + 1; }
    void get_info(t_uint32 song, file_info& info, abort_callback& abort) {
        const auto data = engine_->info(index(song), [&] { abort.check(); },false);
        info.reset();
        info.meta_set("title", data.title.c_str());
        if (!data.album.empty()) info.meta_set("album", data.album.c_str());
        if (!data.artist.empty()) info.meta_set("artist", data.artist.c_str());
        if (!data.copyright.empty()) info.meta_set("copyright", data.copyright.c_str());
        info.meta_set("tracknumber", std::to_string(song).c_str());
        info.meta_set("totaltracks", std::to_string(data.count).c_str());
        info.meta_set("NSF_PLAY_MS",std::to_string(data.play_ms).c_str());info.meta_set("NSF_LOOP_MS",std::to_string(data.loop_ms).c_str());info.meta_set("NSF_FADE_MS",std::to_string(data.fade_ms).c_str());
        if(auto tag=tags_.songs.find(song);tag!=tags_.songs.end()&&(tags_.owned||nsfplay_component::integer(settings_,"READ_TAGINFO"))) {
            if(tag->second.complete_metadata)info.meta_remove_all();
            for(const auto& [name,values]:tag->second.metadata) for(size_t i=0;i<values.size();++i) {
                if(info.info_set_replaygain(name.c_str(),values[i].c_str()))continue;
                if(!i)info.meta_set(name.c_str(),values[i].c_str());else info.meta_add(name.c_str(),values[i].c_str());
            }
        }
        info.info_set("codec", "NSFPlay");
        info.info_set("encoding", "synthesized");
        info.info_set_int("samplerate", engine_->sample_rate());
        info.info_set_int("channels", engine_->output_channels());
        info.info_set_int("bitspersample", 16);
        info.set_length(data.seconds);
        info.info_set("nsfplay_version","0.3.0");
        info.info_set_int("nsf_play_ms",data.play_ms);info.info_set_int("nsf_loop_ms",data.loop_ms);info.info_set_int("nsf_fade_ms",data.fade_ms);
    }
    t_filestats get_file_stats(abort_callback& abort) { return file_->get_stats(abort); }
    t_filestats2 get_stats2(unsigned flags, abort_callback& abort) { return file_->get_stats2_(flags, abort); }
    void decode_initialize(t_uint32 song, unsigned flags, abort_callback& abort) {
        const bool interactive=(flags&input_flag_playback)!=0 && !(flags&input_flag_testing_integrity);
        if(interactive) {
            session_=std::make_shared<nsfplay_component::PlaybackSession>();session_->path=path_;session_->subsong=song;
            session_->file_info=engine_->info(index(song),[&]{abort.check();}).details;
            if(!native_.empty()) {
                auto time=[](int ms){return ms<0?std::string{}:std::to_string(ms/1000)+"'"+std::to_string((ms%1000)/10);};
                for(unsigned i=0;i<engine_->track_count();++i) {
                    const auto t=engine_->info(i,[&]{abort.check();},false);char number[16];std::snprintf(number,sizeof(number),"$%02X",i+1);
                    session_->playlist+=native_+"::NSF,"+number+","+nsfplay_component::escape_legacy(t.legacy_title)+","+time(t.play_ms)+","+time(t.loop_ms)+","+time(t.fade_ms)+"\r\n";
                }
            }
            engine_->attach_session(session_);nsfplay_component::register_session(session_);
            if(nsfplay_component::integer(settings_,"WRITE_TAGINFO")&&!native_.empty()&&!tags_.found) {
                try { nsfplay_component::write_tags(native_,all_tags(abort));tags_=nsfplay_component::read_tags(native_); }
                catch(const std::exception& e) { session_->error=e.what(); }
            }
        }
        if(flags&input_flag_canonical_decode) {auto canonical=settings_;canonical["APU2_OPTION5"]="0";canonical["APU2_OPTION7"]="0";engine_->configure(canonical,[&]{abort.check();});}
        engine_->initialize(index(song), [&] { abort.check(); },interactive,(flags&input_flag_no_looping)!=0,(flags&input_flag_allow_inaccurate_seeking)!=0);
        wrote_detected_=false;
    }
    bool decode_run(audio_chunk& chunk, abort_callback& abort) {
        std::array<int16_t, nsfplay_component::Engine::max_block_frames * nsfplay_component::Engine::channels> pcm;
        const auto got = engine_->render(pcm.data(), engine_->block_size(), [&] { abort.check(); });
        if (!got) { save_detected();return false; }
        chunk.set_data_fixedpoint(pcm.data(), got * engine_->output_channels() * sizeof(int16_t),
            engine_->sample_rate(), engine_->output_channels(), 16, audio_chunk::g_guess_channel_config(engine_->output_channels()));
        return true;
    }
    void decode_seek(double seconds, abort_callback& abort) { engine_->seek(seconds, [&] { abort.check(); }); }
    bool decode_can_seek() { return true; }
    void retag_set_info(t_uint32 song, const file_info& info, abort_callback& abort) {
        const auto current=engine_->info(index(song),[&]{abort.check();},false);
        auto& tag=edits_[song];auto field=[&](const char* name){const auto v=info.meta_get(name,0);return v?std::string(v):std::string{};};
        tag.title=field("title");tag.album=field("album");tag.artist=field("artist");tag.copyright=field("copyright");
        tag.complete_metadata=true;tag.metadata.clear();
        for(size_t i=0;i<info.meta_get_count();++i)for(size_t j=0;j<info.meta_enum_value_count(i);++j)tag.metadata[info.meta_enum_name(i)].push_back(info.meta_enum_value(i,j));
        info.get_replaygain().for_each([&](const char* name,const char* value){tag.metadata[name]={value};});
        tag.play_ms=current.play_ms;tag.loop_ms=current.loop_ms;tag.fade_ms=current.fade_ms;
        auto timing=[&](const char* name,int& value) { if(auto v=info.meta_get(name,0)) {size_t used=0;const auto ms=std::stoll(v,&used);if(used!=strlen(v)||ms<0||ms>32767000)throw exception_io_data("NSF timing must be 0 to 32767000 milliseconds");value=static_cast<int>(ms); } };
        timing("NSF_PLAY_MS",tag.play_ms);timing("NSF_LOOP_MS",tag.loop_ms);timing("NSF_FADE_MS",tag.fade_ms);
    }
    void retag_commit(abort_callback& abort) {
        abort.check();nsfplay_component::write_tags(native_,edits_);tags_=nsfplay_component::read_tags(native_);engine_->set_overrides(tags_);edits_.clear();
    }
    void remove_tags(abort_callback& abort) { abort.check();nsfplay_component::write_tags(native_,{},true);tags_={};engine_->set_overrides({}); }
    bool decode_get_dynamic_info(file_info& info,double& delta) {
        if(!session_)return false;
        auto snap=session_->at(1e100);
        if(!snap||!snap->detected||!nsfplay_component::integer(snap->settings,"UPDATE_PLAYLIST"))return false;
        if(snap->length==last_length_)return false;last_length_=snap->length;info.set_length(snap->length);delta=0;return true;
    }
    void decode_poll_live_info(pfc::list_base_t<input_live_info_t>& out,abort_callback& abort) {
        abort.check();file_info_impl info;double delta=0;
        if(decode_get_dynamic_info(info,delta)) {
            const double length=info.get_length();input_live_info_t change;change.scope=input_live_info_t::scope_info;change.deltaTime=delta;
            change.payload=file_info_filter::create([length](trackRef,t_filestats,file_info& target){target.set_length(length);return true;});out.add_item(change);
        }
    }
    static bool g_is_our_content_type(const char*) { return false; }
    static bool g_is_our_path(const char*, const char* extension) {
        return stricmp_utf8(extension, "nsf") == 0 || stricmp_utf8(extension, "nsfe") == 0;
    }
    static const char* g_get_name() { return "NSFPlay"; }
    static GUID g_get_preferences_guid() { return nsfplay_component::preferences_guid; }
    static GUID g_get_guid() { return {0x295734ba, 0xd873, 0x4760, {0xb9,0x28,0xc9,0x03,0x64,0x86,0x3a,0x09}}; }
private:
    unsigned index(t_uint32 song) const {
        if (!song || song > engine_->track_count()) throw exception_io_data("Invalid NSF subsong");
        return song - 1;
    }
    file::ptr file_;
    std::unique_ptr<nsfplay_component::Engine> engine_;
    nsfplay_component::Settings settings_;
    nsfplay_component::LegacyTags tags_;
    std::string path_,native_;
    std::shared_ptr<nsfplay_component::PlaybackSession> session_;
    std::map<unsigned,nsfplay_component::SongOverride> edits_;
    bool wrote_detected_=false;
    double last_length_=-1;
    std::map<unsigned,nsfplay_component::SongOverride> all_tags(abort_callback& abort) {
        std::map<unsigned,nsfplay_component::SongOverride> out;
        for(unsigned i=0;i<engine_->track_count();++i) { const auto t=engine_->info(i,[&]{abort.check();},false);out[i+1]={t.legacy_title,t.album,t.artist,t.copyright,t.play_ms,t.loop_ms,t.fade_ms}; }
        return out;
    }
    void save_detected() {
        if(wrote_detected_||!session_||native_.empty())return;
        auto snap=session_->at(1e100);
        if(!snap||!snap->detected||!nsfplay_component::integer(snap->settings,"UPDATE_PLAYLIST")||!nsfplay_component::integer(settings_,"WRITE_TAGINFO"))return;
        wrote_detected_=true;
        try { auto t=tags_.songs[session_->subsong];t.play_ms=snap->play_ms;t.loop_ms=snap->loop_ms;t.fade_ms=snap->fade_ms;nsfplay_component::write_tags(native_,{{session_->subsong,t}}); }
        catch(const std::exception& e) { std::lock_guard guard(session_->mutex);session_->error=e.what(); }
    }
};
input_factory_t<input_nsfplay> factory;
DECLARE_FILE_TYPE("Nintendo Sound Format", "*.NSF;*.NSFE");
}
