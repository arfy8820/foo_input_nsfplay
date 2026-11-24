#include "tags.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <windows.h>
namespace nsfplay_component {
namespace {
std::mutex tags_mutex;
std::string encode(const std::string& value) {
    static const char* hex="0123456789ABCDEF";std::string out;
    for(unsigned char c:value) {if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c==' ')out+=c;else {out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;
}
std::string decode(const std::string& value) {
    std::string out;for(size_t i=0;i<value.size();++i)if(value[i]=='%'&&i+2<value.size()) {size_t used=0;const auto n=std::stoul(value.substr(i+1,2),&used,16);if(used!=2)throw std::runtime_error("Invalid encoded metadata");out+=static_cast<char>(n);i+=2;}else out+=value[i];return out;
}
std::string utf8(const std::wstring& text) {
    const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr);return out;
}
std::vector<std::string> fields(const std::string& value) {
    std::vector<std::string> out(1);
    for(size_t i=0;i<value.size();++i) { if(value[i]=='\\'&&i+1<value.size())out.back()+=value[++i]; else if(value[i]==',')out.emplace_back(); else out.back()+=value[i]; }
    return out;
}
int milliseconds(const std::string& value) {
    if(value.empty())return -1;
    std::istringstream stream(value); std::string part; int64_t time=0;
    while(std::getline(stream,part,':')) { size_t used=0; const auto n=std::stoll(part,&used); if(used!=part.size()||n<0)throw std::runtime_error("Invalid sidecar time"); time=time*60+n; if(time>32767)throw std::runtime_error("Sidecar time exceeds 32767 seconds"); }
    return static_cast<int>(time*1000);
}
std::string section(const std::filesystem::path& path) { auto v=path.filename().u8string();return {v.begin(),v.end()}; }
}
std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); if(!in)throw std::runtime_error("Cannot read file");
    in.seekg(0,std::ios::end);const auto size=in.tellg(); if(size<0||size>4*1024*1024)throw std::runtime_error("Text file exceeds 4 MiB");
    in.seekg(0);std::string text(static_cast<size_t>(size),'\0');in.read(text.data(),text.size());if(!in)throw std::runtime_error("Cannot read text file");
    if(text.rfind("\xEF\xBB\xBF",0)==0)text.erase(0,3);
    else if(text.size()>=2&&uint8_t(text[0])==255&&uint8_t(text[1])==254) {
        std::wstring wide;for(size_t i=2;i+1<text.size();i+=2)wide.push_back(wchar_t(uint8_t(text[i])|(uint16_t(uint8_t(text[i+1]))<<8)));text=utf8(wide);
    } else if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0)&&!text.empty()) {
        const auto n=MultiByteToWideChar(CP_ACP,0,text.data(),static_cast<int>(text.size()),nullptr,0);std::wstring wide(n,0);MultiByteToWideChar(CP_ACP,0,text.data(),static_cast<int>(text.size()),wide.data(),n);text=utf8(wide);
    }
    return text;
}
void write_text(const std::filesystem::path& path,const std::string& text) {
    auto temp=path;temp+=L".nsfplay.tmp";
    { std::ofstream out(temp,std::ios::binary|std::ios::trunc); if(!out)throw std::runtime_error("Cannot create output file");out.write(text.data(),text.size());out.flush();if(!out)throw std::runtime_error("Cannot write output file"); }
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { const auto error=GetLastError(); DeleteFileW(temp.c_str());throw std::runtime_error("Cannot replace output file (Windows error "+std::to_string(error)+")"); }
}
std::string escape_legacy(const std::string& value) { std::string out; for(char c:value) { if(c=='\\'||c==',')out+='\\'; if(c!='\r'&&c!='\n')out+=c; } return out; }
LegacyTags read_tags(const std::string& native_file) {
    LegacyTags result;if(native_file.empty())return result;
    std::lock_guard lock(tags_mutex);
    const auto source=std::filesystem::u8path(native_file);auto local=source;local.replace_extension(L".tag");
    for(const auto& path:{local,source.parent_path()/L"taginfo.tag"}) {
        if(!std::filesystem::exists(path))continue;
        std::istringstream stream(read_text(path));std::string line;bool active=false;SongOverride common;
        while(std::getline(stream,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
            if(line[0]=='[') { active=line=="["+section(source)+"]";if(active)result.found=true;continue; }
            if(!active)continue;auto at=line.find('=');if(at==std::string::npos)continue;
            const auto key=line.substr(0,at),value=line.substr(at+1);
            if(key=="NSFPlayComponent"&&value=="1")result.owned=true;
            if(key=="Title")common.album=value;else if(key=="Artist")common.artist=value;else if(key=="Comment")common.copyright=value;
            else if(key.rfind("@Song",0)==0) {
                size_t used=0;const auto n=std::stoul(key.substr(5),&used);if(!n||n>255)continue;
                const auto suffix=key.substr(5+used);
                if(!suffix.empty()) {
                    auto& song=result.songs[static_cast<unsigned>(n)];
                    if(suffix==".MetaComplete") {song.complete_metadata=value=="1";continue;}
                    if(suffix.rfind(".Meta.",0)==0) {
                        auto& values=song.metadata[decode(suffix.substr(6))];std::istringstream list(value);std::string v;
                        while(std::getline(list,v,'|'))values.push_back(decode(v));if(value.empty())values.push_back("");continue;
                    }
                    if(suffix!=".PlayMs"&&suffix!=".LoopMs"&&suffix!=".FadeMs")continue;
                    const auto ms=std::stoll(value);if(ms<0||ms>32767000)throw std::runtime_error("Invalid sidecar millisecond timing");
                    if(suffix==".PlayMs")song.play_ms=static_cast<int>(ms);else if(suffix==".LoopMs")song.loop_ms=static_cast<int>(ms);else if(suffix==".FadeMs")song.fade_ms=static_cast<int>(ms);continue;
                }
                auto f=fields(value);auto& song=result.songs[static_cast<unsigned>(n)];song.title=f[0];
                if(f.size()>1)song.play_ms=milliseconds(f[1]);if(f.size()>2)song.loop_ms=milliseconds(f[2]);if(f.size()>3)song.fade_ms=milliseconds(f[3]);
            }
        }
        if(result.found) { for(unsigned i=1;i<=255;++i) { auto& s=result.songs[i];s.album=common.album;s.artist=common.artist;s.copyright=common.copyright; } return result; }
    }
    return result;
}
void write_tags(const std::string& native_file,const std::map<unsigned,SongOverride>& changes,bool clear) {
    if(native_file.empty())throw std::runtime_error("Sidecars require a local filesystem file");
    std::lock_guard lock(tags_mutex);
    const auto source=std::filesystem::u8path(native_file);auto path=source;path.replace_extension(L".tag");
    const auto section_name="["+section(source)+"]";
    std::vector<std::string> lines;std::string input;
    if(std::filesystem::exists(path))input=read_text(path);
    std::istringstream stream(input);std::string line; bool active=false,inserted=false;
    auto append=[&] {
        if(inserted)return;inserted=true;
        if(!clear) {
            lines.push_back("NSFPlayComponent=1");
            if(changes.size()>1) {
                const auto& c=changes.begin()->second;
                lines.push_back("Title="+c.album);lines.push_back("Artist="+c.artist);lines.push_back("Comment="+c.copyright);
            }
            for(const auto& [n,s]:changes) {
                auto time=[](int ms){return ms<0?std::string{}:std::to_string(ms/1000);};
                const auto key="@Song"+std::to_string(n);lines.push_back(key+"="+escape_legacy(s.title)+","+time(s.play_ms)+","+time(s.loop_ms)+","+time(s.fade_ms));
                if(s.play_ms>=0)lines.push_back(key+".PlayMs="+std::to_string(s.play_ms));if(s.loop_ms>=0)lines.push_back(key+".LoopMs="+std::to_string(s.loop_ms));if(s.fade_ms>=0)lines.push_back(key+".FadeMs="+std::to_string(s.fade_ms));
                if(s.complete_metadata)lines.push_back(key+".MetaComplete=1");
                for(const auto& [name,values]:s.metadata) {std::string encoded;for(size_t i=0;i<values.size();++i){if(i)encoded+='|';encoded+=encode(values[i]);}lines.push_back(key+".Meta."+encode(name)+"="+encoded);}
            }
            lines.push_back("NSFplugWriteEnable=1");
        }
    };
    while(std::getline(stream,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(!line.empty()&&line[0]=='[') { if(active)append();active=line==section_name;lines.push_back(line);continue; }
        if(active) {
            if(clear)continue;
            const auto at=line.find('=');const auto key=line.substr(0,at);
            bool replace=key=="NSFplugWriteEnable"||key=="NSFPlayComponent";
            if(changes.size()>1) replace|=key=="Title"||key=="Artist"||key=="Comment";
            for(const auto& [n,s]:changes) {const auto prefix="@Song"+std::to_string(n);replace|=key==prefix||key.rfind(prefix+".",0)==0;}
            if(replace)continue;
        }
        lines.push_back(line);
    }
    if(!inserted) { if(!active)lines.push_back(section_name);append(); }
    std::string output="\xEF\xBB\xBF";for(const auto& l:lines)output+=l+"\r\n";write_text(path,output);
}
}
