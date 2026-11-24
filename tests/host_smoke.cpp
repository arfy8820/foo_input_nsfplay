// Test-only foobar2000 component. Never included in the release package.
// Exercises published host services in an isolated portable test profile.
#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/menu_helpers.h>
#include <foobar2000/SDK/cfg_var.h>
#include "../src/settings.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

DECLARE_COMPONENT_VERSION("NSFPlay host smoke tests","1","Test-only component; not distributed.");
VALIDATE_COMPONENT_FILENAME("foo_nsfplay_test.dll");
namespace {
std::filesystem::path folder;
void record(const std::string& line) {std::ofstream out(folder/L"host-smoke.txt",std::ios::app);out<<line<<'\n';out.flush();}
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);record(std::string("PASS: ")+message);}
std::string utf8(const std::filesystem::path& path) {const auto s=path.u8string();return {s.begin(),s.end()};}
class Tests : public initquit {
public:
    void on_init() override {
        wchar_t directory[32768]={};if(!GetEnvironmentVariableW(L"NSFPLAY_TEST_DIR",directory,32768))return;folder=directory;
        record("START: host services loaded");
        fb2k::inMainThread([]{run();});
    }
    static void run() {
        try {
            using namespace nsfplay_component;
            require(core_api::is_portable_mode_enabled(),"Test host is portable (normal profile is not used)");
            pfc::string8 profile;require(filesystem::g_get_native_path(core_api::get_profile_path(),profile),"Test profile has a native path");
            const auto expected=utf8(folder.parent_path());require(!_strnicmp(profile.c_str(),expected.c_str(),expected.size()),"Test profile stays inside isolated host folder");
            const GUID settings_guid={0x497e4aba,0x6ee1,0x4c71,{0x97,0x98,0xf2,0xa1,0xe1,0xdf,0x09,0x70}};
            cfg_string config(settings_guid,"");auto values=defaults();values["RATE"]="32000";values["NCH"]="1";values["PLAY_ADVANCE"]="1";
            config=serialize(values).c_str();
            input_entry::ptr entry,candidate;service_enum_t<input_entry> entries;
            while(entries.next(candidate))if(!strcmp(candidate->get_name_(),"NSFPlay"))entry=candidate;
            require(entry.is_valid(),"NSFPlay input service registered");
            require(entry->is_our_path("test.NSF","NSF")&&entry->is_our_path("test.NsFe","NsFe"),"Case-insensitive NSF/NSFe format registration");
            const auto file=utf8(folder/L"smoke.nsfe");abort_callback_impl abort;
            input_decoder::ptr decoder;entry->open(decoder,nullptr,file.c_str(),abort);
            require(decoder->get_subsong_count()==2,"Host sees both subsongs");
            file_info_impl info;decoder->get_info(1,info,abort);
            require(info.meta_get("title",0)&&!strcmp(info.meta_get("title",0),"Second"),"Host metadata respects NSFe playlist order");
            decoder->initialize(1,input_flag_no_looping,abort);audio_chunk_impl chunk;uint64_t frames=0;bool nonzero=false;
            while(decoder->run(chunk,abort)) {
                require_format(chunk);frames+=chunk.get_sample_count();if(frames>32000)throw std::runtime_error("Background infinite decode did not stop");
                for(size_t i=0;i<chunk.get_sample_count()*chunk.get_channels();++i)nonzero|=chunk.get_data()[i]!=0;
            }
            require(frames==8000&&nonzero,"Host audio chunks use configured 32 kHz mono and finite EOF");
            decoder->seek(0,abort);require(decoder->run(chunk,abort),"Host backward seek after EOF");
            abort_callback_impl cancelled;cancelled.set_state(true);bool stopped=false;
            try{decoder->seek(1,cancelled);}catch(const exception_aborted&){stopped=true;}
            require(stopped,"Host cancellation propagates as exception_aborted");
            decoder.release();
            const auto write_file=utf8(folder/L"host-write.nsf");
            input_info_writer::ptr writer;entry->open(writer,nullptr,write_file.c_str(),abort);writer->get_info(1,info,abort);
            info.meta_set("title","Host edit, Unicode: \xE9\x9F\xB3\xE6\xA5\xBD");info.meta_set("NSF_PLAY_MS","120");info.meta_set("NSF_LOOP_MS","0");info.meta_set("NSF_FADE_MS","50");
            info.meta_set("GENRE","Chiptune");info.meta_add("GENRE","Instrumental");auto rg=info.get_replaygain();rg.m_track_gain=-8.25f;rg.m_track_peak=0.75f;info.set_replaygain(rg);
            writer->set_info(1,info,abort);writer->commit(abort);file_info_impl updated;writer->get_info(1,updated,abort);
            require(updated.meta_get("title",0)&&!strcmp(updated.meta_get("title",0),info.meta_get("title",0)),"Host tag writer reads back Unicode edits");writer.release();
            require(updated.meta_enum_value_count(updated.meta_find("GENRE"))==2&&updated.get_replaygain().m_track_gain==-8.25f,"Multivalue tags and ReplayGain survive writing");
            entry->open(decoder,nullptr,write_file.c_str(),abort);decoder->get_info(1,updated,abort);
            require(std::abs(updated.get_length()-0.17)<1e-8,"Owned sidecar millisecond timing survives reopening");decoder.release();
            const GUID pref_guid={0xc95db8ed,0xc080,0x4f2b,{0x9c,0x15,0x86,0x4d,0xc2,0x3a,0x19,0x62}};
            preferences_page::ptr page,probe;service_enum_t<preferences_page> pages;
            while(pages.next(probe))if(probe->get_guid()==pref_guid)page=probe;
            require(page.is_valid(),"Native preferences service registered");
            const GUID inspector_guid={0xe1492d07,0x5172,0x446a,{0xa2,0x9a,0x76,0xb0,0x5a,0xb1,0x46,0x30}};
            ui_element::ptr element;require(ui_element::g_find(element,inspector_guid),"Default UI inspector element registered");
            config=serialize(defaults()).c_str();record("RESULT: PASS");
        } catch(const std::exception& e) {record(std::string("RESULT: FAIL: ")+e.what());}
        fb2k::inMainThread([]{standard_commands::main_exit();});
    }
private:
    static void require_format(const audio_chunk& c) {if(c.get_sample_rate()!=32000||c.get_channels()!=1)throw std::runtime_error("Configured audio format ignored");}
};
static initquit_factory_t<Tests> tests;
class Stages : public init_stage_callback {
public:
    void on_init_stage(t_uint32 stage) override {
        wchar_t directory[32768]={};if(!GetEnvironmentVariableW(L"NSFPLAY_TEST_DIR",directory,32768))return;folder=directory;record("STAGE: "+std::to_string(stage));
    }
};
static service_factory_single_t<Stages> stages;
}
