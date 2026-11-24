#pragma once
#include "session.h"
#include <foobar2000/SDK/foobar2000-lite.h>
namespace nsfplay_component {
inline const GUID preferences_guid={0xc95db8ed,0xc080,0x4f2b,{0x9c,0x15,0x86,0x4d,0xc2,0x3a,0x19,0x62}};
Settings host_settings();
void save_settings(const Settings& settings);
void register_session(const std::shared_ptr<PlaybackSession>& session);
std::shared_ptr<PlaybackSession> active_session();
double playback_position();
std::string native_path(const char* path);
std::string profile_folder();
void show_inspector();
void close_inspector();
void show_settings();
}
