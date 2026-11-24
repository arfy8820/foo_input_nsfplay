#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nsfplay_component {
enum class ParamKind { number, boolean, text, color, choice, fixed };
struct Parameter {
    std::string key, label, group, initial, help;
    ParamKind kind = ParamKind::number;
    int64_t low = 0, high = 1;
    std::vector<std::pair<std::string, std::string>> choices;
    bool live = false;
};
using Settings = std::map<std::string, std::string>;
const std::vector<Parameter>& parameters();
const Parameter& parameter(const std::string& key);
Settings defaults();
int64_t integer(const Settings& settings, const std::string& key);
void set_value(Settings& settings, const std::string& key, std::string value);
std::string serialize(const Settings& settings);
Settings deserialize(const std::string& text, const Settings& base = defaults());
struct SettingsImport { Settings values;std::map<std::string,Settings> presets;std::vector<std::string> notices; };
SettingsImport import_settings(const std::string& text,const Settings& base=defaults());
std::string channel_key(unsigned channel, const char* field);
extern const std::array<const char*, 32> channel_names;
extern const std::array<const char*, 8> device_names;
extern const std::array<int, 32> channel_devices;
}
