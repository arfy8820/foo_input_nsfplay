#include "../src/engine.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <set>

using nsfplay_component::Engine;
using Bytes = std::vector<uint8_t>;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static const Bytes program = {
    0xa9,0x01,0x8d,0x15,0x40, 0xa9,0xbf,0x8d,0x00,0x40,
    0xa9,0xfd,0x8d,0x02,0x40, 0xa9,0x08,0x8d,0x03,0x40, 0x60, 0x60
};
static Bytes nsf() {
    Bytes b(128);
    const Bytes signature = {'N','E','S','M',0x1a,1,2,1,0,0x80,0,0x80,0x15,0x80};
    std::copy(signature.begin(), signature.end(), b.begin());
    const std::string title = "NSFPlay component smoke test";
    std::copy(title.begin(), title.end(), b.begin()+14);
    b.insert(b.end(), program.begin(), program.end());
    return b;
}
static void chunk(Bytes& b, const char* id, const Bytes& payload) {
    for (unsigned shift=0; shift<32; shift+=8) b.push_back(static_cast<uint8_t>(payload.size() >> shift));
    b.insert(b.end(), id, id+4);
    b.insert(b.end(), payload.begin(), payload.end());
}
static Bytes nsfe() {
    Bytes b={'N','S','F','E'};
    chunk(b,"INFO",{0,0x80,0,0x80,0x15,0x80,0,0,2,0});
    chunk(b,"DATA",program);
    chunk(b,"plst",{1,0});
    chunk(b,"time",{120,0,0,0,200,0,0,0});
    chunk(b,"fade",{50,0,0,0,50,0,0,0});
    chunk(b,"tlbl",{'F','i','r','s','t',0,'S','e','c','o','n','d',0});
    chunk(b,"NEND",{});
    return b;
}
static uint64_t decode(Engine& engine) {
    std::array<int16_t,Engine::block_frames*2> pcm;
    uint64_t frames=0;
    bool audible=false;
    while (const auto got=engine.render(pcm.data(),Engine::block_frames,[]{})) {
        frames+=got;
        audible |= std::any_of(pcm.begin(),pcm.begin()+got*2,[](int16_t s){return s!=0;});
        require(frames<48000,"Unexpectedly unbounded fixture");
    }
    require(audible,"Synthetic square wave rendered silence");
    return frames;
}
int main(int argc,char** argv) {
    try {
        using namespace nsfplay_component;
        auto config=defaults();require(config.size()==177&&parameters().size()==177,"Parameter inventory differs from legacy baseline");
        require(deserialize(serialize(config))==config,"Settings do not round-trip");
        const auto imported=import_settings("[NSFplug]\nMASTER_VOLUME=99\n[PRESET0000]\nNAME=Legacy\nAPU1_VOLUME=77\nAPU1_FILTER=32\n");
        require(imported.values.at("MASTER_VOLUME")=="99"&&imported.presets.at("Legacy").at("APU1_VOLUME")=="77"&&!imported.notices.empty(),"Legacy presets and obsolete-key reporting failed");
        for(const auto& p:parameters())set_value(config,p.key,p.initial);
        for(const auto& pair:{std::pair{"RATE","0"},{"CHANNEL_31_COL","GG0000"},{"MASK","4294967296"},{"BPS","24"},{"STOP_SEC","0"}}) {
            bool rejected=false;try{set_value(config,pair.first,pair.second);}catch(const std::exception&){rejected=true;}require(rejected,"Invalid parameter was accepted");
        }
        set_value(config,"MASK","4294967295");require(integer(config,"MASK")==4294967295LL,"High mask bits were lost");
        const auto a=nsf(), b=nsfe();
        Engine plain(a,[]{});
        require(plain.track_count()==2,"NSF subsong count");
        require(plain.info(0,[]{}).seconds==305.0,"Default length/fade changed");
        plain.initialize(1,[]{});
        std::array<int16_t,960> pcm;
        require(plain.render(pcm.data(),480,[]{})==480,"NSF render failed");
        plain.seek(0.03,[]{});
        plain.seek(0,[]{});
        require(plain.render(pcm.data(),480,[]{})==480,"NSF backward seek failed");
        Engine extended(b,[]{});
        require(extended.track_count()==2,"NSFe subsong count");
        require(extended.info(0,[]{}).title=="Second","NSFe playlist order/label");
        require(extended.info(1,[]{}).seconds==0.17,"NSFe duration mapping");
        extended.initialize(0,[]{});
        require(decode(extended)==12000,"NSFe EOF was not sample bounded");
        extended.seek(0,[]{});
        require(decode(extended)==12000,"NSFe decode after EOF seek");
        extended.seek(999,[]{});
        require(extended.render(pcm.data(),480,[]{})==0,"Seek beyond EOF");
        auto worker=[b] { Engine e(b,[]{}); e.initialize(1,[]{}); return decode(e); };
        auto f=std::async(std::launch::async,worker);
        require(worker()==8160 && f.get()==8160,"Concurrent engines failed");
        bool cancelled=false;
        try { plain.seek(1,[]{throw std::runtime_error("cancelled");}); }
        catch (const std::runtime_error&) { cancelled=true; }
        require(cancelled,"Seek did not check abort");
        bool invalid=false;
        try { Engine bad({'N','S','F','E'},[]{}); }
        catch (const std::runtime_error&) { invalid=true; }
        require(invalid,"Truncated file was accepted");
        auto muted=defaults();muted["MASK"]="4294967295";muted["MASK_INIT"]="0";
        Engine silence(b,[]{},muted);silence.initialize(0,[]{});
        require(silence.render(pcm.data(),480,[]{})==480,"Muted render failed");
        require(std::all_of(pcm.begin(),pcm.end(),[](auto s){return s==0;}),"Channel mask did not silence audio");
        auto mono=defaults();mono["RATE"]="32000";mono["NCH"]="1";
        Engine single(b,[]{},mono);single.initialize(0,[]{});
        require(single.sample_rate()==32000&&single.output_channels()==1,"Sample rate/mono setting ignored");
        uint64_t mono_frames=0;while(const auto got=single.render(pcm.data(),320,[]{}))mono_frames+=got;require(mono_frames==8000,"Configured-rate EOF is inaccurate");
        auto no_playlist=defaults();no_playlist["NSFE_PLAYLIST"]="0";Engine physical(b,[]{},no_playlist);require(physical.info(0,[]{}).title=="First","NSFe playlist disable ignored");
        auto looping=defaults();looping["PLAY_ADVANCE"]="1";Engine loop(b,[]{},looping);loop.initialize(0,[]{},true,false);
        for(int i=0;i<40;++i)require(loop.render(pcm.data(),480,[]{})==480,"Interactive infinite loop stopped");
        Engine bounded(b,[]{},looping);bounded.initialize(0,[]{},false,true);require(decode(bounded)==12000,"Background decode honored infinite playback");
        auto speed=defaults();speed["MULT_SPEED"]="512";Engine faster(b,[]{},speed);require(faster.info(0,[]{}).seconds==0.125,"Speed-adjusted metadata duration is wrong");faster.initialize(0,[]{});require(decode(faster)==6000,"Speed-adjusted EOF is inaccurate");
        auto session=std::make_shared<PlaybackSession>();Engine live(a,[]{});live.attach_session(session);live.initialize(0,[]{},true,false);
        for(int i=0;i<5;++i)live.render(pcm.data(),480,[]{});
        auto telemetry=session->at(10);require(telemetry&&telemetry->channels[0].frequency>400&&telemetry->channels[0].note==69,"Chip telemetry/note mapping failed");
        auto edit=telemetry->settings;edit["MASK"]="2147483648";edit["MASTER_VOLUME"]="64";session->configure(edit);
        for(int i=0;i<5;++i)live.render(pcm.data(),480,[]{});
        telemetry=session->at(10);require(telemetry&&telemetry->settings.at("MASK")=="2147483648"&&telemetry->settings.at("MASTER_VOLUME")=="64","Live mask/gain update failed");
        session->write(0x20,0x7b);for(int i=0;i<5;++i)live.render(pcm.data(),480,[]{});
        telemetry=session->at(10);require(telemetry&&telemetry->memory[0x20]==0x7b,"Queued memory write failed");
        if(argc>1) {
            const std::filesystem::path dir=argv[1];
            std::filesystem::create_directories(dir);
            for(const auto& item : {std::pair{"smoke.nsf",a},std::pair{"smoke.nsfe",b}}) {
                std::ofstream out(dir/item.first,std::ios::binary);
                out.write(reinterpret_cast<const char*>(item.second.data()),item.second.size());
                require(out.good(),"Cannot write fixture");
            }
            write_text(dir/L"settings.ini",serialize(defaults()));
            const auto utf=dir/L"unicode-音楽.nsf";std::ofstream fixture(utf,std::ios::binary);fixture.write(reinterpret_cast<const char*>(a.data()),a.size());fixture.close();
            const auto utf8= utf.u8string();const std::string native(utf8.begin(),utf8.end());
            SongOverride tag;tag.title="Test, title\\with escape";tag.album="Album";tag.artist="Artist";tag.play_ms=120;tag.loop_ms=0;tag.fade_ms=50;
            tag.complete_metadata=true;tag.metadata["GENRE"]={"Chiptune","Instrumental"};tag.metadata["COMMENT"]={"Percent % / pipe | / newline\nsecond line"};tag.metadata["REPLAYGAIN_TRACK_GAIN"]={"-8.25 dB"};
            write_tags(native,{{1,tag}});auto read=read_tags(native);require(read.owned&&read.songs.at(1).title==tag.title&&read.songs.at(1).play_ms==120&&read.songs.at(1).fade_ms==50,"Unicode sidecar/escaping/millisecond round-trip failed");
            require(read.songs.at(1).metadata==tag.metadata&&read.songs.at(1).complete_metadata,"Multivalue/escaped metadata extensions did not round-trip");
            tag.title="Second song";write_tags(native,{{2,tag}});read=read_tags(native);require(read.songs.at(1).title!="Second song"&&read.songs.at(2).title=="Second song","Sidecar edit replaced another song");
            Engine tagged(b,[]{});tagged.set_overrides(read);require(tagged.info(0,[]{}).seconds==0.17,"Sidecar timing override ignored");
            write_tags(native,{},true);require(read_tags(native).songs.at(1).title.empty(),"Sidecar clear failed");
        }
        std::cout<<"PASS: 177 settings, validation, persistence, NSF/NSFe, subsongs, PCM, EOF, seek, concurrency, cancellation, masks, mono/rate, infinite/finite policy, telemetry, live controls, memory writes, Unicode sidecars and precise timing\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
