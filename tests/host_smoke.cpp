// Test-only foobar2000 component. Never included in the release package.
// Exercises published host services in an isolated portable test profile.
#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/menu_helpers.h>
#include <foobar2000/SDK/cfg_var.h>
#include <foobar2000/SDK/configStore.h>
#include <commctrl.h>
#include "../src/settings.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <string_view>
#include <stdexcept>

DECLARE_COMPONENT_VERSION("NSFPlay host smoke tests","1","Test-only component; not distributed.");
VALIDATE_COMPONENT_FILENAME("foo_nsfplay_test.dll");
namespace {
std::filesystem::path folder;
void record(const std::string& line) {std::ofstream out(folder/L"host-smoke.txt",std::ios::app);out<<line<<'\n';out.flush();}
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);record(std::string("PASS: ")+message);}
std::string utf8(const std::filesystem::path& path) {const auto s=path.u8string();return {s.begin(),s.end()};}
HTREEITEM find_node(HWND tree,HTREEITEM first,const wchar_t* name) {
    for(auto item=first;item;item=TreeView_GetNextSibling(tree,item)) {
        wchar_t label[128]{};
        TVITEMW data{};
        data.mask=TVIF_TEXT;
        data.hItem=item;
        data.pszText=label;
        data.cchTextMax=128;
        if(SendMessageW(tree,TVM_GETITEMW,0,reinterpret_cast<LPARAM>(&data)) && !wcscmp(label,name))return item;
        if(const auto child=find_node(tree,TreeView_GetChild(tree,item),name))return child;
    }
    return nullptr;
}
HWND find_control(HWND parent,const wchar_t* type,const wchar_t* caption=nullptr) {
    struct Find {const wchar_t* type;const wchar_t* caption;HWND result=nullptr;} find{type,caption};
    EnumChildWindows(parent,[](HWND child,LPARAM arg)->BOOL {
        auto& find=*reinterpret_cast<Find*>(arg);
        wchar_t type[128]{},text[128]{};
        if(!IsWindowVisible(child)) return TRUE;
        GetClassNameW(child,type,128);
        if(_wcsicmp(type,find.type)) return TRUE;
        if(find.caption) {
            GetWindowTextW(child,text,128);
            if(wcscmp(text,find.caption)) return TRUE;
        }
        find.result=child;
        return FALSE;
    },reinterpret_cast<LPARAM>(&find));
    return find.result;
}
void finish() {fb2k::inMainThread([]{standard_commands::main_exit();});}
struct TreeTest {
    int step=0, ticks=0;
    HWND dialog=nullptr, tree=nullptr;
    HTREEITEM fds=nullptr, audio=nullptr, playback=nullptr;
    static void CALLBACK poll(HWND,UINT,UINT_PTR timer,DWORD) {
        static TreeTest test;
        try {
            if(++test.ticks>80) throw std::runtime_error("Preferences dialog timed out at step "+std::to_string(test.step));
            if(!test.advance()) return;
            require(true,"Real preferences tree supports radio arrow keys and applies edits across pages");
            record("RESULT: PASS");
        } catch(const std::exception& e) {record(std::string("RESULT: FAIL: ")+e.what());}
        KillTimer(nullptr,timer);
        if(test.dialog) PostMessageW(test.dialog,WM_CLOSE,0,0);
        finish();
    }
    bool advance() {
        if(step==0) {
            EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL {
                auto& test=*reinterpret_cast<TreeTest*>(data);
                const auto tree=find_control(window,WC_TREEVIEWW);
                if(!tree || !find_node(tree,TreeView_GetRoot(tree),L"NSFPlay")) return TRUE;
                test.tree=tree;
                test.dialog=window;
                return FALSE;
            },reinterpret_cast<LPARAM>(this));
            if(!tree) return false;
            const auto nsf=find_node(tree,TreeView_GetRoot(tree),L"NSFPlay");
            const auto chips=find_node(tree,TreeView_GetChild(tree,nsf),L"Chips");
            fds=find_node(tree,TreeView_GetChild(tree,chips),L"FDS");
            audio=find_node(tree,TreeView_GetChild(tree,nsf),L"Audio");
            playback=find_node(tree,TreeView_GetChild(tree,nsf),L"Playback");
            if(!fds || !audio || !playback) throw std::runtime_error("Foobar2000 did not show NSFPlay child pages");
            TreeView_SelectItem(tree,playback);
            ++step;
            return false;
        }
        if(step==1) {
            const auto current=find_control(dialog,L"Button",L"48000");
            const auto previous=find_control(dialog,L"Button",L"44100");
            if(!current || !previous) return false;
            SetFocus(current);
            MSG left{current,WM_KEYDOWN,VK_LEFT,1};
            if(!IsDialogMessageW(dialog,&left) || SendMessageW(previous,BM_GETCHECK,0,0)!=BST_CHECKED)
                throw std::runtime_error("Arrow navigation did not select the previous radio option");
            MSG right{previous,WM_KEYDOWN,VK_RIGHT,1};
            if(!IsDialogMessageW(dialog,&right) || SendMessageW(current,BM_GETCHECK,0,0)!=BST_CHECKED)
                throw std::runtime_error("Arrow navigation did not restore the original radio option");
            TreeView_SelectItem(tree,fds);
            ++step;
            return false;
        }
        if(step==2) {
            const auto button=find_control(dialog,L"Button",L"Write protect");
            if(!button) return false;
            SendMessageW(button,BM_CLICK,0,0);
            TreeView_SelectItem(tree,audio);
            ++step;
            return false;
        }
        const auto label=find_control(dialog,L"Static",L"Master gain");
        if(!label) return false;
        const auto edit=GetWindow(label,GW_HWNDNEXT);
        if(!edit) throw std::runtime_error("Audio page is missing the master gain editor");
        SetWindowTextW(edit,L"170");
        auto apply=find_control(dialog,L"Button",L"&Apply");
        if(!apply) apply=find_control(dialog,L"Button",L"Apply");
        if(!apply || !IsWindowEnabled(apply)) throw std::runtime_error("Foobar2000 did not enable Apply after cross-page edits");
        SendMessageW(apply,BM_CLICK,0,0);
        const GUID guid={0x497e4aba,0x6ee1,0x4c71,{0x97,0x98,0xf2,0xa1,0xe1,0xdf,0x09,0x70}};
        const auto key=pfc::format("cfg_var.",pfc::print_guid(guid));
        auto config=fb2k::configStore::get();
        const auto saved=nsfplay_component::deserialize(config->getConfigString(key)->c_str());
        if(saved.at("FDS_OPTION2")!="1" || saved.at("MASTER_VOLUME")!="170")
            throw std::runtime_error("Foobar2000 Apply did not save edits from both pages");
        return true;
    }
};
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
            componentversion::ptr component;
            service_enum_t<componentversion> components;
            bool identity=false;
            while(components.next(component)) {
                pfc::string8 name, version, file;
                component->get_component_name(name);
                if(strcmp(name.c_str(),"NSFPlay")) continue;
                component->get_component_version(version);
                component->get_file_name(file);
                auto file_name=std::string_view(file.c_str());
                file_name.remove_prefix(file_name.find_last_of("/\\")+1);
                if(file_name.ends_with(".dll")) file_name.remove_suffix(4);
                identity=!strcmp(version.c_str(),"0.3.0") && file_name=="foo_input_nsfplay";
                record(std::string("Component metadata: ")+name.c_str()+" "+version.c_str()+" ("+file.c_str()+")");
                if(identity) break;
            }
            require(identity,"Components lists NSFPlay 0.3.0 from foo_input_nsfplay.dll");
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
            std::set<std::string> pages_found, chips_found;
            const GUID chips_guid={0x9abe8e74,0xc28e,0x432b,{0xa4,0x7d,0x70,0x9c,0x79,0xff,0x4b,0xff}};
            bool root=false, chips=false;
            preferences_page::ptr probe;service_enum_t<preferences_page> pages;
            while(pages.next(probe)) {
                if(probe->get_guid()==pref_guid)
                    root=probe->get_parent_guid()==preferences_page::guid_input && !strcmp(probe->get_name(),"NSFPlay");
                if(probe->get_parent_guid()==pref_guid) pages_found.insert(probe->get_name());
                if(probe->get_parent_guid()==chips_guid) chips_found.insert(probe->get_name());
            }
            preferences_branch::ptr branch;service_enum_t<preferences_branch> branches;
            while(branches.next(branch)) if(branch->get_guid()==chips_guid)
                chips=branch->get_parent_guid()==pref_guid && !strcmp(branch->get_name(),"Chips");
            require(root && chips && pages_found==std::set<std::string>{"Audio","Channels","Compatibility","Diagnostics","Display","Playback","Timing"} &&
                    chips_found==std::set<std::string>{"APU","FDS","MMC5","N163","Sunsoft 5B","VRC6","VRC7"},
                    "NSFPlay registers each preference page and chip under the correct tree node");
            const GUID inspector_guid={0xe1492d07,0x5172,0x446a,{0xa2,0x9a,0x76,0xb0,0x5a,0xb1,0x46,0x30}};
            ui_element::ptr element;require(ui_element::g_find(element,inspector_guid),"Default UI inspector element registered");
            config=serialize(defaults()).c_str();
            if(!SetTimer(nullptr,0,100,TreeTest::poll)) throw std::runtime_error("Cannot start real preferences test");
            ui_control::get()->show_preferences(pref_guid);
            return;
        } catch(const std::exception& e) {record(std::string("RESULT: FAIL: ")+e.what());}
        finish();
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
