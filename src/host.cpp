#include "host.h"
#include "tags.h"
#include <foobar2000/SDK/cfg_var.h>
#include <foobar2000/SDK/playback_control.h>
#include <foobar2000/SDK/play_callback.h>
#include <foobar2000/SDK/preferences_page.h>
#include <foobar2000/SDK/ui.h>
#include <foobar2000/SDK/initquit.h>
#include <algorithm>
#include <filesystem>

namespace nsfplay_component {
namespace {
const GUID settings_guid={0x497e4aba,0x6ee1,0x4c71,{0x97,0x98,0xf2,0xa1,0xe1,0xdf,0x09,0x70}};
cfg_string stored(settings_guid,"");
std::mutex registry_mutex;
std::vector<std::weak_ptr<PlaybackSession>> sessions;
std::weak_ptr<PlaybackSession> pinned;
bool owned_stop=false;
void stop_policy(bool single) {
    if(!single&&!owned_stop)return;
    const auto player=playback_control::get();
    if(single&&!player->get_stop_after_current()) {player->set_stop_after_current(true);owned_stop=true;}
    else if(!single&&owned_stop) {player->set_stop_after_current(false);owned_stop=false;}
}
}
Settings host_settings() {
    const auto text=stored.get();
    try { return deserialize(text.c_str()); }
    catch(const std::exception& e) { console::printf("NSFPlay: invalid saved settings; using defaults: %s",e.what());return defaults(); }
}
void save_settings(const Settings& settings) {
    // Validate the entire transaction before changing the persisted configuration.
    const auto validated=deserialize(serialize(settings));
    stored=serialize(validated).c_str();
    if(auto session=active_session()) {session->configure(validated);stop_policy(integer(validated,"PLAY_ADVANCE")==2);}
}
void register_session(const std::shared_ptr<PlaybackSession>& session) {
    std::lock_guard guard(registry_mutex);
    std::erase_if(sessions,[](auto& weak){return weak.expired();});sessions.emplace_back(session);
}
std::shared_ptr<PlaybackSession> active_session() {
    metadb_handle_ptr track;const auto player=playback_control::get();
    if(!player->get_now_playing(track))return {};
    std::lock_guard guard(registry_mutex);
    auto matches=[&](const std::shared_ptr<PlaybackSession>& s) { return s&&s->path==track->get_path()&&s->subsong==track->get_subsong_index(); };
    if(auto current=pinned.lock();matches(current))return current;
    for(auto it=sessions.rbegin();it!=sessions.rend();++it)if(auto s=it->lock();matches(s)) { pinned=s;return s; }
    return {};
}
double playback_position() { return playback_control::get()->playback_get_position(); }
std::string native_path(const char* path) { pfc::string8 out; if(!filesystem::g_get_native_path(path,out))return {};return out.c_str(); }
std::string profile_folder() {
    auto root=std::filesystem::u8path(native_path(core_api::get_profile_path()))/L"nsfplay";
    std::filesystem::create_directories(root);auto value=root.u8string();return {value.begin(),value.end()};
}
void show_settings() { ui_control::get()->show_preferences(preferences_guid); }
class PlaybackCallbacks : public play_callback_static {
public:
    unsigned get_flags() override { return flag_on_playback_starting|flag_on_playback_new_track|flag_on_playback_stop; }
    void on_playback_starting(play_control::t_track_command,bool) override { clear();stop_policy(false); }
    void on_playback_new_track(metadb_handle_ptr track) override {
        clear();
        const char* ext=strrchr(track->get_path(),'.');
        stop_policy(ext&&(!_stricmp(ext,".nsf")||!_stricmp(ext,".nsfe"))&&integer(host_settings(),"PLAY_ADVANCE")==2);
    }
    void on_playback_stop(play_control::t_stop_reason) override { clear();stop_policy(false); }
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_playback_time(double) override {}
    void on_volume_change(float) override {}
private:
    static void clear() { std::lock_guard guard(registry_mutex);pinned.reset(); }
};
static play_callback_static_factory_t<PlaybackCallbacks> callback_factory;
class Lifecycle : public initquit { public:void on_quit() override { close_inspector(); } };
static initquit_factory_t<Lifecycle> lifecycle;
}
