#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace nsfplay_component {
struct SongOverride {
    std::string title, album, artist, copyright;
    int play_ms=-1, loop_ms=-1, fade_ms=-1;
    bool complete_metadata=false;
    std::map<std::string,std::vector<std::string>> metadata;
};
struct LegacyTags {
    std::map<unsigned,SongOverride> songs;
    bool found=false;
    bool owned=false;
};
LegacyTags read_tags(const std::string& native_file);
void write_tags(const std::string& native_file,const std::map<unsigned,SongOverride>& changes,bool clear=false);
std::string read_text(const std::filesystem::path& path);
void write_text(const std::filesystem::path& path,const std::string& text);
std::string escape_legacy(const std::string& value);
}
