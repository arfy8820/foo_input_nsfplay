#include "settings.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace nsfplay_component {
const std::array<const char*,32> channel_names = {"Pulse 1","Pulse 2","Triangle","Noise","DPCM","FDS","MMC5 pulse 1","MMC5 pulse 2","MMC5 PCM","5B 1","5B 2","5B 3","VRC6 pulse 1","VRC6 pulse 2","VRC6 saw","VRC7 1","VRC7 2","VRC7 3","VRC7 4","VRC7 5","VRC7 6","N163 1","N163 2","N163 3","N163 4","N163 5","N163 6","N163 7","N163 8","VRC7 7","VRC7 8","VRC7 9"};
const std::array<const char*,8> device_names = {"APU1","APU2","5B","MMC5","N163","VRC6","VRC7","FDS"};
const std::array<int,32> channel_devices = {0,0,1,1,1,7,3,3,3,2,2,2,5,5,5,6,6,6,6,6,6,4,4,4,4,4,4,4,4,6,6,6};
std::string channel_key(unsigned channel,const char* field) { char key[48]; std::snprintf(key,sizeof(key),"CHANNEL_%02u_%s",channel,field); return key; }
const std::vector<Parameter>& parameters() {
    static const auto schema = [] {
        std::vector<Parameter> p;
        auto add=[&](const char* key,const char* label,const char* group,const char* value,ParamKind kind,int64_t low,int64_t high,bool live,const char* help) -> Parameter& {
            p.push_back({key,label,group,value,help,kind,low,high,{},live}); return p.back();
        };
        using K=ParamKind;
        auto& rate=add("RATE","Synthesis sample rate (Hz)","Playback","48000",K::choice,8000,96000,false,"New decoders use this rate. Restart playback after applying.");
        for(auto n:{8000,11025,16000,22050,32000,44100,48000,88200,96000}) rate.choices.push_back({std::to_string(n),std::to_string(n)});
        add("NCH","Output channels","Playback","2",K::choice,1,2,false,"Restart playback after changing.").choices={{"1","Mono"},{"2","Stereo"}};
        add("BPS","Engine sample precision","Playback","16",K::fixed,16,16,false,"NSFPlay renders signed 16-bit samples; foobar2000 receives floating-point audio. Hardware output precision is controlled by the host.");
        add("PLAY_TIME","Fallback duration (milliseconds)","Playback","300000",K::number,0,32767000,false,"Used when no embedded or sidecar time is available.");
        add("FADE_TIME","Fallback fade (milliseconds)","Playback","5000",K::number,0,32767000,false,"Fade after the fallback duration.");
        add("LOOP_NUM","Loop count","Playback","2",K::number,0,256,false,"Number of passes used with loop timings.");
        add("PLAY_ADVANCE","At end of track","Playback","0",K::choice,0,2,true,"Infinite looping is interactive only. Conversion and scanning always end. Single track uses the host stop-after-current feature.").choices={{"0","Advance next"},{"1","Infinite loop"},{"2","Stop after current track"}};
        add("AUTO_STOP","Stop on silence","Playback","1",K::boolean,0,1,true,"Use NSFPlay silence detection.");
        add("STOP_SEC","Silence threshold (seconds)","Playback","3",K::number,1,30,true,"Continuous silence before detecting an ending.");
        add("AUTO_DETECT","Detect loops automatically","Playback","0",K::boolean,0,1,true,"Let NSFPlay detect repeating register sequences.");
        add("DETECT_TIME","Loop detection delay (milliseconds)","Playback","30000",K::number,0,32767000,true,"Wait before testing for loops.");
        add("DETECT_INT","Loop detection interval (milliseconds)","Playback","5000",K::number,1,32767000,true,"Interval between loop comparisons.");
        add("DETECT_ALT","Alternate loop detector","Playback","0",K::boolean,0,1,false,"Restart playback to change the detector implementation.");
        add("NSFE_PLAYLIST","Honor embedded NSFe playlist","Playback","1",K::boolean,0,1,false,"Reload playlist entries after changing: subsong IDs are positions in the chosen ordering.");
        add("FAST_SEEK","Fast seek","Playback","1",K::boolean,0,1,true,"Use the upstream fast skip implementation; disable for render-and-discard seeking.");
        add("QUALITY","Synthesis quality","Audio","10",K::number,1,40,true,"Oversampling factor. Higher settings use more CPU.");
        add("MASTER_VOLUME","Master gain","Audio","128",K::number,0,256,true,"NSFPlay gain before host volume, DSP and ReplayGain. Default 128.");
        add("HPF","High-pass filter (stored value)","Audio","164",K::number,0,256,true,"Legacy HPF storage; the Winamp slider displayed 256 minus this value.");
        add("LPF","Low-pass filter","Audio","112",K::number,0,400,true,"NSFPlay low-pass filter parameter.");
        add("REGION","Region preference","Timing","0",K::choice,0,6,false,"Prefer respects file flags; force overrides them. Restart playback after changing.").choices={{"0","Automatic"},{"1","Prefer NTSC"},{"2","Prefer PAL"},{"3","Prefer Dendy"},{"4","Force NTSC"},{"5","Force PAL"},{"6","Force Dendy"}};
        add("VSYNC_ADJUST","Adjust playback to video refresh","Timing","0",K::boolean,0,1,false,"Use video refresh timing instead of file tick period.");
        add("MULT_SPEED","Clock speed multiplier / 256","Timing","256",K::number,32,2048,true,"256 is normal speed; 128 half speed; 512 double speed. Live changes retain state and alter pitch/timing.");
        add("NTSC_BASECYCLES","NTSC CPU clock (Hz)","Timing","1789773",K::number,100000,10000000,false,"CPU clock for the NTSC region.");
        add("PAL_BASECYCLES","PAL CPU clock (Hz)","Timing","1662607",K::number,100000,10000000,false,"CPU clock for the PAL region.");
        add("DENDY_BASECYCLES","Dendy CPU clock (Hz)","Timing","1773448",K::number,100000,10000000,false,"CPU clock for the Dendy region.");
        add("IRQ_ENABLE","Enable IRQ","Timing","0",K::boolean,0,1,false,"Allow emulated IRQ handling.");
        add("VRC7_PATCH","VRC7 instrument patch set","Chip VRC7","0",K::choice,0,9,false,"Restart playback to change patch set.").choices={{"0","Nuke"},{"1","Rainwarrior"},{"2","FamiTracker 0.3.6"},{"3","FamiTracker 0.3.5"},{"4","Mo"},{"5","KT1"},{"6","KT2"},{"7","YM2413"},{"8","YMF281B"},{"9","YMF281B alternative"}};
        add("MASK","Channel mute mask (unsigned decimal)","Channels","0",K::number,0,4294967295LL,true,"One bit per channel: bit 0 is Pulse 1; bits 29-31 are VRC7 channels 7-9. Use the inspector checkboxes for individual channels.");
        add("MASK_INIT","Clear channel mutes at playback start","Channels","1",K::boolean,0,1,false,"Clears the effective mask for newly initialized interactive tracks, matching legacy behavior. Saved mask remains available when this is disabled.");
        add("INFO_FREQ","Display refresh (Hz)","Display","30",K::number,1,60,true,"Refresh frequency of the playback inspector.");
        add("INFO_DELAY","Display delay (milliseconds)","Display","50",K::number,-1000,1000,true,"Additional offset relative to host playback time. Positive values delay chip information.");
        add("GRAPHIC_MODE","Graphical waveforms","Display","1",K::boolean,0,1,true,"Show sampled waveform shapes instead of hexadecimal waveform data.");
        add("FREQ_MODE","Frequency in Hz","Display","1",K::boolean,0,1,true,"Disable to display the chip register value in hexadecimal.");
        add("5B_ENVELOPE_COL","5B envelope color (RRGGBB)","Display","0000FF",K::color,0,0,true,"Color of the 5B envelope activity indicator.");
        add("5B_NOISE_COL","5B noise color (RRGGBB)","Display","000000",K::color,0,0,true,"Color of the 5B noise activity indicator.");
        add("TITLE_FORMAT","Legacy title format","Compatibility","%L (%n/%e) %T - %A",K::text,0,128,false,"Used for exported legacy playlists and sidecar default titles. Normal playlists use foobar2000 title formatting.");
        add("READ_TAGINFO","Read legacy tag sidecars","Compatibility","0",K::boolean,0,1,false,"Read file.tag first, then taginfo.tag. Reopen tracks after changing.");
        add("WRITE_TAGINFO","Allow automatic sidecar creation/update","Compatibility","0",K::boolean,0,1,false,"Opt-in writes only for interactive playback of local files. Background decoders never write sidecars.");
        add("UPDATE_PLAYLIST","Publish detected timing updates","Compatibility","0",K::boolean,0,1,true,"Publish dynamic playback length; when sidecar writes are also enabled, persist detected timing to the legacy sidecar.");
        add("LAST_PRESET","Last preset name","Compatibility","Default",K::text,0,128,false,"Name of the last imported or saved settings preset.");
        add("LOG_CPU","CPU log level","Diagnostics","0",K::number,0,4,false,"Logging is interactive only. Set an absolute log filename, then restart playback.");
        add("LOG_CPU_FILE","CPU log filename","Diagnostics","nsf_write.log",K::text,0,1024,false,"Relative names are resolved under this foobar2000 profile's nsfplay folder. Only interactive playback writes logs.");
        const char* colors[]={"FF0000","FF0000","00FF00","00FF00","000000","0080FF","FFC000","FFC000","000000","0000FF","0000FF","0000FF","FF8000","FF8000","FF8000","8000FF","8000FF","8000FF","8000FF","8000FF","8000FF","FF0080","FF0080","FF0080","FF0080","FF0080","FF0080","FF0080","FF0080","8000FF","8000FF","8000FF"};
        for(unsigned i=0;i<32;++i) for(auto suffix:{"PAN","VOL","COL"}) {
            const auto key=channel_key(i,suffix); const auto label=std::string(channel_names[i])+" / "+suffix;
            const bool color=std::string(suffix)=="COL";
            add(key.c_str(),label.c_str(),"Channels",color?colors[i]:"128",color?K::color:K::number,0,color?0:(std::string(suffix)=="PAN"?255:128),true,color?"Keyboard and waveform color (RRGGBB).":"Pan: 0 left, 128 center, 255 right. Volume: 0-128.");
        }
        for(auto device:device_names) for(auto suffix:{"VOLUME","MUTE"}) {
            const auto key=std::string(device)+"_"+suffix; const bool mute=std::string(suffix)=="MUTE";
            add(key.c_str(),key.c_str(),"Mixer",mute?"0":"128",mute?K::boolean:K::number,0,mute?1:255,true,"Per-device gain or mute, before channel mixing.");
        }
        const std::vector<std::vector<const char*>> options={{"Unmute on reset","Phase refresh","Nonlinear mixer","Duty swap","Negate sweep initialization"},{"Enable $4011 DAC","Periodic noise","Unmute on reset","DPCM anti-click","Nonlinear mixer","Randomize noise","Triangle mute","Randomize triangle","Reverse DPCM"},{},{"Nonlinear mixer","Phase refresh"},{"Serial mixing","Phase read-only","Limit wavelength"},{},{"OPLL mode"},{"Low-pass cutoff (Hz)","Reset on $4085","Write protect"}};
        const std::vector<std::vector<int>> values={{1,1,1,0,0},{1,1,1,0,1,1,1,1,0},{},{1,1},{0,0,0},{},{0},{2000,0,0}};
        for(unsigned d=0;d<8;++d) for(unsigned i=0;i<options[d].size();++i) {
            const auto key=std::string(device_names[d])+"_OPTION"+char('0'+i); const auto group="Chip "+std::string(device_names[d]); const bool cutoff=d==7&&i==0;
            add(key.c_str(),options[d][i],group.c_str(),std::to_string(values[d][i]).c_str(),cutoff?K::number:K::boolean,0,cutoff?99999:1,true,"NSFPlay device emulation option. Reset-related options take effect on the next playback initialization.");
        }
        return p;
    }();
    return schema;
}
const Parameter& parameter(const std::string& key) {
    const auto& p=parameters(); auto i=std::find_if(p.begin(),p.end(),[&](const auto& v){return v.key==key;});
    if(i==p.end()) throw std::runtime_error("Unknown NSFPlay setting: "+key); return *i;
}
Settings defaults() { Settings out; for(const auto& p:parameters()) out[p.key]=p.initial; return out; }
int64_t integer(const Settings& s,const std::string& key) { return std::stoll(s.at(key)); }
void set_value(Settings& s,const std::string& key,std::string value) {
    const auto& p=parameter(key);
    if(value.find_first_of("\r\n\0",0,3)!=std::string::npos) throw std::runtime_error("Value contains a line break or NUL");
    if(p.kind==ParamKind::text) { if(value.size()>size_t(p.high)) throw std::runtime_error("Text is too long"); }
    else if(p.kind==ParamKind::color) {
        if(value.size()!=6 || !std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isxdigit(c);})) throw std::runtime_error("Use six hexadecimal digits: RRGGBB");
        for(auto& c:value) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else {
        size_t end=0; int64_t n;
        try { n=std::stoll(value,&end); } catch(...) { throw std::runtime_error("Enter an integer"); }
        if(end!=value.size() || n<p.low || n>p.high) throw std::runtime_error("Allowed range: "+std::to_string(p.low)+" to "+std::to_string(p.high));
        value=std::to_string(n);
        if(p.kind==ParamKind::choice && std::none_of(p.choices.begin(),p.choices.end(),[&](auto& c){return c.first==value;})) throw std::runtime_error("Choose a supported value");
    }
    s[key]=std::move(value);
}
std::string serialize(const Settings& s) { std::string out="[NSFplug]\n"; for(const auto& p:parameters()) out+=p.key+"="+s.at(p.key)+"\n"; return out; }
Settings deserialize(const std::string& input,const Settings& base) {
    Settings out=base; std::istringstream stream(input); std::string line; bool enabled=true;
    while(std::getline(stream,line)) {
        if(!line.empty()&&line.back()=='\r') line.pop_back();
        if(line.rfind("\xEF\xBB\xBF",0)==0) line.erase(0,3);
        if(line.empty() || line[0]==';' || line[0]=='#') continue;
        if(line[0]=='[') { enabled=line=="[NSFplug]"||line=="[NSFPlay]"; continue; }
        if(!enabled) continue;
        const auto at=line.find('='); if(at==std::string::npos) throw std::runtime_error("Invalid settings line: "+line);
        auto key=line.substr(0,at); while(!key.empty()&&key.back()==' ')key.pop_back();
        if(!out.count(key)) throw std::runtime_error("Unknown or obsolete setting: "+key);
        try { set_value(out,key,line.substr(at+1)); } catch(const std::exception& e) { throw std::runtime_error(key+": "+e.what()); }
    }
    return out;
}
SettingsImport import_settings(const std::string& input,const Settings& base) {
    SettingsImport result;result.values=base;
    std::map<std::string,std::vector<std::pair<std::string,std::string>>> sections;std::string section="NSFplug",line;std::istringstream stream(input);
    while(std::getline(stream,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.rfind("\xEF\xBB\xBF",0)==0)line.erase(0,3);
        if(line.empty()||line[0]==';'||line[0]=='#')continue;
        if(line[0]=='['&&line.back()==']') {section=line.substr(1,line.size()-2);continue;}
        const auto split=line.find('=');if(split==std::string::npos)throw std::runtime_error("Invalid INI line: "+line);
        auto key=line.substr(0,split);while(!key.empty()&&key.back()==' ')key.pop_back();sections[section].push_back({key,line.substr(split+1)});
    }
    auto apply=[&](Settings& target,const auto& entries) {
        for(const auto& [key,value]:entries) {
            if(key=="NAME")continue;
            if(!target.count(key)) {result.notices.push_back("Skipped obsolete or unknown key: "+key);continue;}
            try {set_value(target,key,value);}catch(const std::exception& e){throw std::runtime_error(key+": "+e.what());}
        }
    };
    bool recognized=false;
    for(const auto& [name,entries]:sections)if(name=="NSFplug"||name=="NSFPlay") {apply(result.values,entries);recognized=true;}
    for(const auto& [name,entries]:sections)if(name.rfind("PRESET",0)==0) {
        Settings preset=result.values;std::string title=name;
        for(const auto& [key,value]:entries)if(key=="NAME")title=value;
        apply(preset,entries);preset["LAST_PRESET"]=title;result.presets[title]=std::move(preset);recognized=true;
    }
    if(!recognized)throw std::runtime_error("No NSFplug settings or PRESET sections found");
    return result;
}
}
