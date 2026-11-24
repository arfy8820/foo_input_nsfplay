// Runs the real preferences implementation with in-memory host configuration.
// No running foobar2000 instance, existing profile, or desktop automation is used.
#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/component.h>
#include <crtdbg.h>
#include <oleacc.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "../src/host.h"

namespace {
nsfplay_component::Settings saved;
std::shared_ptr<nsfplay_component::PlaybackSession> playing;
std::string test_profile;
unsigned saves=0;
}
namespace nsfplay_component {
Settings host_settings() {return saved;}
void save_settings(const Settings& value) {saved=value;++saves;}
void register_session(const std::shared_ptr<PlaybackSession>&) {}
std::shared_ptr<PlaybackSession> active_session() {return playing;}
double playback_position() {return 0;}
std::string native_path(const char* path) {return path;}
std::string profile_folder() {return test_profile;}
void show_settings() {}
}

// Include the actual window classes and factory rather than a mock UI.
#include "../src/ui.cpp"
extern "C" foobar2000_client* __cdecl foobar2000_get_interface(foobar2000_api*,HINSTANCE);

namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::wstring accessible_name(HWND control,long child=CHILDID_SELF) {
    IAccessible* object=nullptr;
    require(SUCCEEDED(AccessibleObjectFromWindow(control,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void**>(&object))),"Could not get the native accessibility object");
    VARIANT id={};
    id.vt=VT_I4;
    id.lVal=child;
    BSTR name=nullptr;
    const auto result=object->get_accName(id,&name);
    const std::wstring text=name?name:L"";
    SysFreeString(name);
    object->Release();
    require(result==S_OK,"Control or item has no accessible name");
    return text;
}
void select_combo(HWND combo,int index) {
    require(index>=0&&SendMessageW(combo,CB_SETCURSEL,index,0)==index,"Could not select combo-box item");
    SendMessageW(GetParent(combo),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(combo),CBN_SELCHANGE),reinterpret_cast<LPARAM>(combo));
}
class Callback : public preferences_page_callback {
public:
    unsigned notifications=0;
    std::function<void()> on_change;
    void on_state_changed() override {++notifications;if(auto action=std::move(on_change))action();}
};
void drain() {
    MSG message;
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
}
void run(HWND parent) {
    using namespace nsfplay_component;
    service_impl_single_t<PreferencesPage> factory;
    auto callback=fb2k::service_new<Callback>();
    const auto original=saved;
    auto page=factory.instantiate(parent,callback);
    require(page.is_valid()&&IsWindow(page->get_wnd()),"Preferences factory failed to create a window");
    const auto window=page->get_wnd();
    const auto selector=GetDlgItem(window,Params);
    const auto category=GetDlgItem(window,Category);
    const auto search=GetDlgItem(window,Search);
    require(SendMessageW(selector,CB_GETCOUNT,0,0)==177,"Preferences did not populate all parameters");
    require(FindWindowExW(window,nullptr,WC_LISTVIEWW,nullptr)==nullptr,"Preferences still contains a list view");
    require(accessible_name(category)==L"Category","Category combo box has the wrong accessible name");
    require(accessible_name(search)==L"Search parameters","Search field has the wrong accessible name");
    require(accessible_name(selector)==L"Parameter","Parameter combo box has the wrong accessible name");
    require(!(page->get_state()&preferences_state::changed),"Opening preferences changed configuration");
    require(saved==original&&saves==0,"Opening preferences wrote configuration");
    require(callback->notifications==0,"Initialization notified the host about a user edit");

    // These native controls send synchronous EN_CHANGE and selection notifications.
    SetWindowTextW(search,L"MASTER_VOLUME");
    require(SendMessageW(selector,CB_GETCOUNT,0,0)==1,"Parameter search did not rebuild the selector");
    SetWindowTextW(GetDlgItem(window,Value),L"99");
    require(page->get_state()&preferences_state::changed,"Editing master volume did not mark preferences dirty");
    page->apply();
    require(saved.at("MASTER_VOLUME")=="99"&&saves==1,"Apply did not persist the edited setting");
    require(!(page->get_state()&preferences_state::changed),"Apply did not clear the dirty state");

    SetWindowTextW(GetDlgItem(window,Value),L"9999");
    require(!(page->get_state()&preferences_state::changed),"Invalid input left Apply enabled");
    require(saved.at("MASTER_VOLUME")=="99","Invalid input changed configuration");
    page->reset();
    require(page->get_state()&preferences_state::changed,"Reset did not preview defaults");
    page->apply();require(saved.at("MASTER_VOLUME")=="128","Reset/Apply did not restore the default");
    require(callback->notifications>0,"Preferences did not notify the host callback");

    SetWindowTextW(search,L"no-matching-parameter");
    require(!IsWindowEnabled(selector),"Empty search left a focusable parameter selector");
    for(int id:{Value,Choices,PickColor})require(!(GetWindowLongPtrW(GetDlgItem(window,id),GWL_STYLE)&WS_VISIBLE),"Empty search left an unrelated editor visible");
    require(text_of(GetDlgItem(window,Help))=="No parameters match the search.","Empty search has no feedback");

    SetWindowTextW(search,L"");
    for(int i=0;i<177;++i) {
        select_combo(selector,i);
        const auto& parameter=parameters()[i];
        const bool combo=parameter.kind==ParamKind::choice||parameter.kind==ParamKind::boolean;
        require(text_of(GetDlgItem(window,combo?ChoiceLabel:ValueLabel))==parameter.label,"Selected parameter has the wrong editor label");
    }
    const auto fds=SendMessageW(category,CB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(L"Chip FDS"));
    select_combo(category,static_cast<int>(fds));
    require(SendMessageW(selector,CB_GETCOUNT,0,0)==3,"FDS category does not expose all three options");
    COMBOBOXINFO info={sizeof(info)};
    require(GetComboBoxInfo(selector,&info)!=FALSE,"Could not get the FDS dropdown");
    require(accessible_name(info.hwndList,1)==L"Low-pass cutoff (Hz)","FDS cutoff is not exposed to accessibility");
    require(accessible_name(info.hwndList,2)==L"Reset on $4085","FDS reset option is not exposed to accessibility");
    require(accessible_name(info.hwndList,3)==L"Write protect","FDS write protect is not exposed to accessibility");

    select_combo(selector,0);
    const auto value=GetDlgItem(window,Value);
    require(accessible_name(value)==L"Low-pass cutoff (Hz)","FDS cutoff editor has the wrong accessible name");
    require(GetNextDlgTabItem(window,selector,FALSE)==value,"Tab did not reach the FDS cutoff editor");
    SetWindowTextW(value,L"2400");
    const auto choices=GetDlgItem(window,Choices);
    for(const auto* name:{L"Reset on $4085",L"Write protect"}) {
        SendMessageW(selector,WM_KEYDOWN,VK_DOWN,0);
        require(accessible_name(choices)==name,"Arrow key did not select and label the next FDS option");
        require(GetNextDlgTabItem(window,selector,FALSE)==choices,"Tab did not reach the FDS option editor");
        select_combo(choices,1);
    }
    page->apply();
    require(saved.at("FDS_OPTION0")=="2400"&&saved.at("FDS_OPTION1")=="1"&&saved.at("FDS_OPTION2")=="1","Apply did not persist all three FDS options");
    drain();
    // This is the SDK contract: the host destroys the window before releasing
    // its final reference to preferences_page_instance.
    DestroyWindow(window);page.release();drain();
    require(!IsWindow(window),"Preferences window survived destruction");
}
void check_inspector_pages(HWND window) {
    using namespace nsfplay_component;
    const auto tabs=GetDlgItem(window,Tab);
    const auto selector=GetDlgItem(window,MixerItem);
    const auto mute=GetDlgItem(window,MuteItem);
    const auto activity=GetDlgItem(window,Activity);
    const auto details=GetDlgItem(window,Details);
    require(FindWindowExW(window,nullptr,WC_LISTVIEWW,nullptr)==nullptr,"Inspector still contains a list view");
    auto switch_tab=[&](int index) {
        TabCtrl_SetCurSel(tabs,index);
        NMHDR change={tabs,Tab,TCN_SELCHANGE};
        SendMessageW(window,WM_NOTIFY,Tab,reinterpret_cast<LPARAM>(&change));
    };
    auto change_slider=[&](int id,int value) {
        const auto slider=GetDlgItem(window,id);
        SendMessageW(slider,TBM_SETPOS,TRUE,value);
        SendMessageW(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(slider));
    };
    playing=std::make_shared<PlaybackSession>();
    auto& channel=playing->history.emplace_back().channels.back();
    channel.present=channel.key=true;
    channel.volume=10;
    channel.frequency=440;
    channel.note=69;
    channel.tone=2;
    channel.output=256;
    const auto original=saved;
    const auto save_count=saves;
    for(int index:{0,1}) {
        switch_tab(index);
        const bool device=index==1;
        const auto count=device?device_names.size():channel_names.size();
        require(SendMessageW(selector,CB_GETCOUNT,0,0)==count,"Inspector populated the wrong selector items");
        require(GetNextDlgTabItem(window,tabs,FALSE)==selector,"Tab did not reach the inspector selector");
        require(GetNextDlgTabItem(window,selector,FALSE)==mute,"Tab did not reach the selected item's mute checkbox");
        require(accessible_name(selector)==(device?L"Sound device":L"Sound channel"),"Inspector selector has the wrong accessible name");
        require(accessible_name(mute)==(device?L"Mute selected device":L"Mute selected channel"),"Mute checkbox has the wrong accessible name");
        require(accessible_name(GetDlgItem(window,Volume))==(device?L"Device volume":L"Channel volume"),"Volume slider has the wrong accessible name");
        require(accessible_name(GetDlgItem(window,Master))==L"Master volume","Master slider has the wrong accessible name");
        COMBOBOXINFO info={sizeof(info)};
        require(GetComboBoxInfo(selector,&info)!=FALSE,"Could not get the mixer dropdown");
        for(size_t item=0;item<count;++item) {
            require(accessible_name(info.hwndList,static_cast<long>(item+1))==wide(device?device_names[item]:channel_names[item]),"Dropdown item is missing from native accessibility");
            if(item>0)SendMessageW(selector,WM_KEYDOWN,VK_DOWN,0);
            require(SendMessageW(selector,CB_GETCURSEL,0,0)==item,"Keyboard did not reach the next mixer item");
        }
        if(!device)require(!playing->pending,"Changing selection queued an unrelated mixer edit");
        SendMessageW(mute,BM_CLICK,0,0);
        require(playing->pending.has_value(),"Mixer edit did not reach the playback session");
        const auto& pending=*playing->pending;
        if(device) {
            require(pending.at("FDS_MUTE")=="1"&&pending.at("APU1_MUTE")=="0","Mute did not target the selected device");
            change_slider(Volume,230);
            require(pending.at("FDS_VOLUME")=="230"&&pending.at("APU1_VOLUME")==original.at("APU1_VOLUME"),"Volume did not target the selected device");
        } else {
            require(integer(pending,"MASK")==0x80000000LL,"Mute did not target the highest channel bit");
            require(accessible_name(GetDlgItem(window,Pan))==L"Channel pan","Pan slider has the wrong accessible name");
            require(accessible_name(activity)==L"Channel activity","Telemetry has the wrong accessible name");
            require(GetNextDlgTabItem(window,GetDlgItem(window,Master),FALSE)==activity,"Tab did not reach the channel telemetry");
            require(text_of(activity)=="Chip volume 10, frequency 440.0 Hz, note A4, tone 2, output 256","Inspector did not show the selected channel telemetry");
            change_slider(Volume,93);
            change_slider(Pan,42);
            require(pending.at(channel_key(31,"VOL"))=="93"&&pending.at(channel_key(31,"PAN"))=="42","Volume or pan edited the wrong channel");
        }
        change_slider(Master,160);
        require(pending.at("MASTER_VOLUME")=="160","Master volume did not reach the playback session");
        SendMessageW(selector,WM_KEYDOWN,VK_UP,0);
        require(SendMessageW(mute,BM_GETCHECK,0,0)==BST_UNCHECKED,"Changing selection left another item's mute state visible");
        SendMessageW(selector,WM_KEYDOWN,VK_DOWN,0);
        require(SendMessageW(mute,BM_GETCHECK,0,0)==BST_CHECKED,"Returning to an item did not restore its mute state");
        SendMessageW(mute,BM_CLICK,0,0);
        require(device?pending.at("FDS_MUTE")=="0":integer(pending,"MASK")==0,"Unmute did not target the selected item");
    }
    require(saved==original&&saves==save_count,"Live mixer edits unexpectedly saved configuration");
    playing.reset();
    for(int index:{2,3}) {
        switch_tab(index);
        require(GetNextDlgTabItem(window,tabs,FALSE)==details,"Inspector text tab includes a mixer control in its tab order");
        require(!(GetWindowLongPtrW(selector,GWL_STYLE)&WS_VISIBLE),"Inspector exposed its selector on a text-only tab");
    }
    switch_tab(0);
    require(GetNextDlgTabItem(window,tabs,FALSE)==selector,"Returning to Channels did not restore the selector");
    require(!IsWindowEnabled(mute),"Inspector enabled mute with no active playback");
}
}

int main(int argc,char** argv) {
    _set_purecall_handler([] {std::cerr<<"FAIL: pure virtual call during preferences construction\n";std::cerr.flush();ExitProcess(71);});
    try {
        require(argc>1,"Pass an isolated test-profile directory");test_profile=argv[1];
        std::filesystem::create_directories(std::filesystem::u8path(test_profile));
        foobar2000_get_interface(nullptr,GetModuleHandleW(nullptr));
        const auto parent=CreateWindowExW(0,L"STATIC",L"NSFPlay preferences regression test",WS_OVERLAPPED,0,0,800,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent!=nullptr,"Cannot create the test parent window");
        for(int i=0;i<12;++i) {saved=nsfplay_component::defaults();saves=0;run(parent);}
        {
            using namespace nsfplay_component;
            service_impl_single_t<PreferencesPage> factory;
            auto callback=fb2k::service_new<Callback>();
            auto page=factory.instantiate(parent,callback);const auto window=page->get_wnd();
            callback->on_change=[&]{DestroyWindow(window);page.release();};
            SetWindowTextW(GetDlgItem(window,Search),L"LPF");drain();
            require(!page.is_valid()&&!IsWindow(window),"Reentrant host closure was not handled safely");
            service_impl_single_t<InspectorElement> element;
            for(int i=0;i<8;++i) {
                auto panel=element.instantiate(parent,element.get_default_configuration(),{});
                const auto child=panel->get_wnd();require(IsWindow(child),"Inspector factory did not create its child window");
                check_inspector_pages(child);
                if(i%2)DestroyWindow(child);panel.release();
                require(!IsWindow(child),"Inspector window survived owner destruction");
            }
            drain();
        }
        DestroyWindow(parent);
        std::cout<<"PASS: preferences and FDS accessibility, all 32 channel and 8 device selections, live mixer edits, channel activity and inspector tab order\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
