#include "host.h"
#include "tags.h"
#include <foobar2000/SDK/preferences_page.h>
#include <foobar2000/SDK/coreDarkMode.h>
#include <foobar2000/SDK/menu.h>
#include <foobar2000/SDK/ui_element.h>
#include <foobar2000/SDK/playback_control.h>
#include <foobar2000/SDK/ui.h>
#include <foobar2000/SDK/modeless_dialog.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>

namespace nsfplay_component {
namespace {
std::wstring wide(const std::string& text) {
    const int n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n);return out;
}
std::string narrow(const std::wstring& text) {
    const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr);return out;
}
std::string text_of(HWND wnd) { std::wstring text(GetWindowTextLengthW(wnd)+1,0);text.resize(GetWindowTextW(wnd,text.data(),static_cast<int>(text.size())));return narrow(text); }
void text_to(HWND wnd,const std::string& text) { SetWindowTextW(wnd,wide(text).c_str()); }
void error_box(HWND parent,const std::exception& e) { MessageBoxW(parent,wide(e.what()).c_str(),L"NSFPlay",MB_OK|MB_ICONERROR); }
std::string lower(std::string s) { for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s; }
std::filesystem::path file_dialog(HWND parent,bool save,const wchar_t* filter,const wchar_t* extension,const wchar_t* initial=L"") {
    wchar_t path[32768]={};wcsncpy_s(path,initial,_TRUNCATE);OPENFILENAMEW ofn={sizeof(ofn)};ofn.hwndOwner=parent;ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.lpstrFilter=filter;ofn.lpstrDefExt=extension;ofn.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    return (save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn))?std::filesystem::path(path):std::filesystem::path{};
}
COLORREF color(const std::string& hex) { const auto c=std::stoul(hex,nullptr,16);return RGB((c>>16)&255,(c>>8)&255,c&255); }
std::string hexcolor(COLORREF c) { char buf[8];std::snprintf(buf,sizeof(buf),"%02X%02X%02X",GetRValue(c),GetGValue(c),GetBValue(c));return buf; }
unsigned window_dpi(HWND hwnd) {
    const auto function=reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));if(function)return function(hwnd);
    const auto dc=GetDC(hwnd);const auto dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(hwnd,dc);return dpi?dpi:96;
}

class NativeWindow {
public:
    HWND window=nullptr;
    virtual ~NativeWindow() { if(window)DestroyWindow(window);if(font_)DeleteObject(font_); }
    void create(HWND parent,bool child,const wchar_t* title,int width=860,int height=700) {
        static const wchar_t* name=L"NSFPlay.Native.8DB45EDA";
        WNDCLASSW wc={};wc.lpfnWndProc=procedure;wc.hInstance=core_api::get_my_instance();wc.lpszClassName=name;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&wc);
        INITCOMMONCONTROLSEX common={sizeof(common),ICC_BAR_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&common);
        if(!CreateWindowExW(WS_EX_CONTROLPARENT,name,title,(child?WS_CHILD|WS_VISIBLE:WS_OVERLAPPEDWINDOW)|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,width,height,parent,nullptr,wc.hInstance,this))throw std::runtime_error("Cannot create NSFPlay window");
    }
protected:
    fb2k::CCoreDarkModeHooks dark_;
    HFONT font_=nullptr;
    unsigned dpi_=96;
    int scale(int n) const { return MulDiv(n,dpi_,96); }
    HWND control(const wchar_t* cls,const wchar_t* text,int id,DWORD style=0,DWORD ex=0) {
        const auto child=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),core_api::get_my_instance(),nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);return child;
    }
    void move(HWND child,int x,int y,int w,int h) { MoveWindow(child,scale(x),scale(y),std::max(1,scale(w)),std::max(1,scale(h)),TRUE); }
    virtual LRESULT dispatch(UINT msg,WPARAM wp,LPARAM lp) {
        if(msg==WM_DPICHANGED) {
            dpi_=HIWORD(wp);const auto old=font_;font_=CreateFontW(-MulDiv(9,dpi_,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            EnumChildWindows(window,[](HWND child,LPARAM font)->BOOL {SendMessageW(child,WM_SETFONT,font,TRUE);return TRUE;},reinterpret_cast<LPARAM>(font_));DeleteObject(old);
            if(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD)) {const auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}return 0;
        }
        if(msg==WM_CLOSE) { DestroyWindow(window);return 0; }
        return DefWindowProcW(window,msg,wp,lp);
    }
    void theme() { dark_.AddDialogWithControls(window); }
private:
    static LRESULT CALLBACK procedure(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
        auto self=reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(wnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            self=static_cast<NativeWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=wnd;SetWindowLongPtrW(wnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
            self->dpi_=window_dpi(wnd);self->font_=CreateFontW(-MulDiv(9,self->dpi_,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        }
        if(!self)return DefWindowProcW(wnd,msg,wp,lp);
        try {
            const auto result=self->dispatch(msg,wp,lp);
            if(msg==WM_NCDESTROY) { SetWindowLongPtrW(wnd,GWLP_USERDATA,0);self->window=nullptr; }
            return result;
        } catch(const std::exception& e) { error_box(wnd,e);return 0; }
    }
};

enum { Category=100,Search,Params,Value,Choices,PickColor,Help,Import,Export,Inspect,PresetName,PresetSave,PresetLoad,PresetDelete,ValueLabel,ChoiceLabel,CategoryLabel,SearchLabel,ParamsLabel,PresetLabel };
class Preferences : public preferences_page_instance, public NativeWindow {
public:
    explicit Preferences(preferences_page_callback::ptr callback):callback_(callback),draft_(host_settings()),baseline_(draft_) {}
    // Controls synchronously send WM_COMMAND while their initial values are set.
    // Only create the HWND after service_impl_t has installed its reference-count
    // vtable and the factory holds an owning service_ptr.
    void initialize_window(HWND parent) { create(parent,true,L"NSFPlay preferences"); }
    HWND get_wnd() override { return window; }
    t_uint32 get_state() override {
        t_uint32 state=preferences_state::resettable|preferences_state::dark_mode_supported;
        if(draft_!=baseline_&&invalid_.empty()) {
            state|=preferences_state::changed;
            for(const auto& p:parameters())if(!p.live&&p.key!="LAST_PRESET"&&p.key!="TITLE_FORMAT"&&draft_.at(p.key)!=baseline_.at(p.key))state|=preferences_state::needs_restart_playback;
            if(draft_.at("NSFE_PLAYLIST")!=baseline_.at("NSFE_PLAYLIST"))state|=preferences_state::needs_rescan_library;
        }
        return state;
    }
    void apply() override {
        if(!invalid_.empty())return;auto current=host_settings();for(const auto& p:parameters())if(draft_.at(p.key)!=baseline_.at(p.key))current[p.key]=draft_.at(p.key);
        save_settings(current);baseline_=draft_=current;rebuild();callback_->on_state_changed();
    }
    void reset() override { draft_=defaults();invalid_.clear();rebuild();callback_->on_state_changed(); }
private:
    preferences_page_callback::ptr callback_;
    Settings draft_,baseline_;
    std::vector<const Parameter*> matches_;
    std::string selected_,invalid_;
    bool updating_=false;
    bool initialized_=false;
    HWND category_=nullptr,search_=nullptr,params_=nullptr,value_=nullptr,choices_=nullptr,pick_=nullptr,help_=nullptr,preset_=nullptr;
    std::filesystem::path preset_path() {
        const auto name=text_of(preset_);
        if(name.empty()||name.size()>128||name.find_first_of("<>:\"/\\|?*.")!=std::string::npos)throw std::runtime_error("Use a preset name without dots or path punctuation");
        auto folder=std::filesystem::u8path(profile_folder())/L"presets";std::filesystem::create_directories(folder);return folder/(std::filesystem::u8path(name).wstring()+L".ini");
    }
    void presets() {
        const auto current=text_of(preset_);SendMessageW(preset_,CB_RESETCONTENT,0,0);
        const auto folder=std::filesystem::u8path(profile_folder())/L"presets";std::filesystem::create_directories(folder);
        for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.path().extension()==L".ini")SendMessageW(preset_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(entry.path().stem().c_str()));
        text_to(preset_,current.empty()?draft_.at("LAST_PRESET"):current);
    }
    void layout() {
        RECT rc;GetClientRect(window,&rc);const int w=MulDiv(rc.right,96,dpi_),h=MulDiv(rc.bottom,96,dpi_);
        const int category_width=std::min(200,std::max(90,(w-30)/2));
        const int search_x=category_width+20;
        move(GetDlgItem(window,CategoryLabel),10,10,category_width,18);
        move(category_,10,30,category_width,240);
        move(GetDlgItem(window,SearchLabel),search_x,10,w-search_x-10,18);
        move(search_,search_x,30,w-search_x-10,24);
        move(GetDlgItem(window,ParamsLabel),10,64,w-20,18);
        move(params_,10,84,w-20,280);
        for(int id:{ValueLabel,ChoiceLabel})move(GetDlgItem(window,id),10,118,w-20,18);
        move(value_,10,138,w-112,24);move(choices_,10,138,w-112,240);move(pick_,w-94,138,84,24);
        const int actions_y=std::max(230,h-90);
        move(help_,10,170,w-20,actions_y-178);
        move(GetDlgItem(window,Import),10,actions_y,90,24);move(GetDlgItem(window,Export),106,actions_y,90,24);move(GetDlgItem(window,Inspect),202,actions_y,w-212,24);
        move(GetDlgItem(window,PresetLabel),10,actions_y+30,w-20,18);
        move(preset_,10,actions_y+48,std::max(90,w-240),240);move(GetDlgItem(window,PresetSave),w-222,actions_y+48,66,24);move(GetDlgItem(window,PresetLoad),w-150,actions_y+48,66,24);move(GetDlgItem(window,PresetDelete),w-78,actions_y+48,68,24);
    }
    void rebuild() {
        updating_=true;
        SendMessageW(params_,CB_RESETCONTENT,0,0);
        matches_.clear();
        const auto group=text_of(category_),query=lower(text_of(search_));
        int select=0;
        for(const auto& p:parameters()) {
            if(group!="All parameters"&&p.group!=group)continue;
            if(!query.empty()&&lower(p.key+" "+p.label+" "+p.help).find(query)==std::string::npos)continue;
            const auto label=wide(group=="All parameters"?p.group+" / "+p.label:p.label);
            const auto item=SendMessageW(params_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if(item<0)continue;
            matches_.push_back(&p);
            if(p.key==selected_)select=static_cast<int>(item);
        }
        EnableWindow(params_,!matches_.empty());
        SendMessageW(params_,CB_SETCURSEL,select,0);
        updating_=false;
        select_parameter();
    }
    void select_parameter() {
        const auto index=SendMessageW(params_,CB_GETCURSEL,0,0);
        const auto p=index==CB_ERR?nullptr:matches_[index];
        const bool combo=p&&(p->kind==ParamKind::choice||p->kind==ParamKind::boolean);
        updating_=true;
        invalid_.clear();
        selected_=p?p->key:"";
        ShowWindow(GetDlgItem(window,ValueLabel),p&&!combo?SW_SHOW:SW_HIDE);
        ShowWindow(value_,p&&!combo?SW_SHOW:SW_HIDE);
        ShowWindow(GetDlgItem(window,ChoiceLabel),combo?SW_SHOW:SW_HIDE);
        ShowWindow(choices_,combo?SW_SHOW:SW_HIDE);
        ShowWindow(pick_,p&&p->kind==ParamKind::color?SW_SHOW:SW_HIDE);
        SendMessageW(choices_,CB_RESETCONTENT,0,0);
        if(p) {
            text_to(GetDlgItem(window,combo?ChoiceLabel:ValueLabel),p->label);
            show_help(*p);
            const auto& value=draft_.at(selected_);
            if(combo) {
                auto entries=p->choices;
                if(p->kind==ParamKind::boolean)entries={{"0","Disabled"},{"1","Enabled"}};
                for(size_t i=0;i<entries.size();++i) {
                    const auto label=wide(entries[i].second);
                    SendMessageW(choices_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
                    if(entries[i].first==value)SendMessageW(choices_,CB_SETCURSEL,i,0);
                }
            } else {
                SendMessageW(value_,EM_SETREADONLY,p->kind==ParamKind::fixed,0);
                text_to(value_,value);
            }
        } else {
            text_to(help_,"No parameters match the search.");
        }
        updating_=false;
    }
    void show_help(const Parameter& p) {
        text_to(help_,p.key+"\r\n"+p.help+(p.live?" Changes can be applied to current playback.":" Takes effect when a track is reopened."));
    }
    void edit() {
        if(updating_||selected_.empty())return;const auto& p=parameter(selected_);std::string value=text_of(value_);
        if(p.kind==ParamKind::boolean||p.kind==ParamKind::choice) {
            const auto index=SendMessageW(choices_,CB_GETCURSEL,0,0);
            if(index==CB_ERR)return;
            value=p.kind==ParamKind::boolean?(index==1?"1":"0"):p.choices[index].first;
        }
        try { set_value(draft_,selected_,value);invalid_.clear();show_help(p); }
        catch(const std::exception& e) { invalid_=e.what();text_to(help_,"Invalid value: "+invalid_); }
        callback_->on_state_changed();
    }
    void command(int id) {
        if(id==IDCANCEL) {if(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD))DestroyWindow(window);return;}
        if(id==Inspect) {show_inspector();return;}
        if(id==PickColor) {
            if(selected_.empty())return;static COLORREF custom[16]={};CHOOSECOLORW c={sizeof(c)};c.hwndOwner=window;c.rgbResult=color(draft_.at(selected_));c.lpCustColors=custom;c.Flags=CC_FULLOPEN|CC_RGBINIT;
            if(ChooseColorW(&c))text_to(value_,hexcolor(c.rgbResult));return;
        }
        if(!invalid_.empty())throw std::runtime_error("Correct the invalid value first: "+invalid_);
        if(id==Import) {
            const auto path=file_dialog(window,false,L"NSFPlay settings (*.ini)\0*.ini\0All files\0*.*\0",L"ini");
            if(!path.empty()) {
                auto imported=import_settings(read_text(path),draft_);draft_=std::move(imported.values);
                for(const auto& [name,preset]:imported.presets) {text_to(preset_,name);write_text(preset_path(),serialize(preset));}presets();rebuild();
                std::string summary="Imported settings and "+std::to_string(imported.presets.size())+" presets. Apply to save settings.";
                for(const auto& note:imported.notices)summary+="\r\n"+note;
                text_to(help_,summary);
            }
        }
        if(id==Export) { const auto path=file_dialog(window,true,L"NSFPlay settings (*.ini)\0*.ini\0",L"ini",L"nsfplay.ini");if(!path.empty())write_text(path,serialize(draft_)); }
        if(id==PresetSave) { const auto path=preset_path();draft_["LAST_PRESET"]=text_of(preset_);write_text(path,serialize(draft_));presets(); }
        if(id==PresetLoad) { const auto name=text_of(preset_);draft_=deserialize(read_text(preset_path()));draft_["LAST_PRESET"]=name;rebuild(); }
        if(id==PresetDelete) { const auto path=preset_path();if(MessageBoxW(window,L"Delete this preset?",L"NSFPlay",MB_YESNO|MB_ICONQUESTION)==IDYES) {std::filesystem::remove(path);presets();} }
        callback_->on_state_changed();
    }
    LRESULT dispatch(UINT msg,WPARAM wp,LPARAM lp) override {
        if(msg==WM_CREATE) {
            control(L"STATIC",L"Category",CategoryLabel);
            category_=control(WC_COMBOBOXW,L"",Category,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
            control(L"STATIC",L"Search parameters",SearchLabel);
            search_=control(L"EDIT",L"",Search,WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);
            control(L"STATIC",L"Parameter",ParamsLabel);
            params_=control(WC_COMBOBOXW,L"",Params,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
            control(L"STATIC",L"",ValueLabel,SS_NOPREFIX);
            value_=control(L"EDIT",L"",Value,WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);SendMessageW(value_,EM_SETLIMITTEXT,1024,0);
            control(L"STATIC",L"",ChoiceLabel,SS_NOPREFIX);
            choices_=control(WC_COMBOBOXW,L"",Choices,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);pick_=control(L"BUTTON",L"&Color...",PickColor,WS_TABSTOP);help_=control(L"STATIC",L"",Help,SS_NOPREFIX);
            control(L"BUTTON",L"&Import...",Import,WS_TABSTOP);control(L"BUTTON",L"&Export...",Export,WS_TABSTOP);control(L"BUTTON",L"Playback &inspector",Inspect,WS_TABSTOP);
            control(L"STATIC",L"Preset name",PresetLabel);
            preset_=control(WC_COMBOBOXW,L"",PresetName,CBS_DROPDOWN|WS_TABSTOP|WS_VSCROLL);control(L"BUTTON",L"&Save",PresetSave,WS_TABSTOP);control(L"BUTTON",L"&Load",PresetLoad,WS_TABSTOP);control(L"BUTTON",L"&Delete",PresetDelete,WS_TABSTOP);
            SendMessageW(category_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All parameters"));std::vector<std::string> groups;for(const auto& p:parameters())if(std::find(groups.begin(),groups.end(),p.group)==groups.end()) {groups.push_back(p.group);SendMessageW(category_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(p.group).c_str()));}
            SendMessageW(category_,CB_SETCURSEL,0,0);presets();rebuild();theme();layout();initialized_=true;return 0;
        }
        if(msg==WM_SIZE) {if(initialized_)layout();return 0;}
        if(msg==WM_COMMAND) {
            if(!initialized_||updating_)return 0;
            // A temporary service reference keeps this object alive across native modal dialogs.
            preferences_page_instance::ptr keep(this);
            const int id=LOWORD(wp),event=HIWORD(wp);
            if(id==Category&&event==CBN_SELCHANGE || id==Search&&event==EN_CHANGE) {invalid_.clear();rebuild();callback_->on_state_changed();}
            else if(id==Params&&event==CBN_SELCHANGE) {const bool was_invalid=!invalid_.empty();select_parameter();if(was_invalid)callback_->on_state_changed();}
            else if(id==Value&&event==EN_CHANGE || id==Choices&&event==CBN_SELCHANGE)edit();
            else if(event==BN_CLICKED)command(id);return 0;
        }
        return NativeWindow::dispatch(msg,wp,lp);
    }
};
class PreferencesPage : public preferences_page_v3 {
public:
    const char* get_name() override {return "NSFPlay";}
    GUID get_guid() override {return preferences_guid;}
    GUID get_parent_guid() override {return guid_input;}
    preferences_page_instance::ptr instantiate(HWND parent,preferences_page_callback::ptr cb) override {
        auto instance=fb2k::service_new<Preferences>(cb);
        instance->initialize_window(parent);
        return instance;
    }
};
static preferences_page_factory_t<PreferencesPage> preferences_factory;

enum { Status=1000,Previous,Pause,Next,SettingsButton,Tab,MixerItem,AllOn,AllOff,Invert,Solo,ResetMix,MonoPan,SpreadPan,RandomPan,ChannelColor,SaveDefaults,Volume,Pan,Master,VolumeLabel,PanLabel,MasterLabel,Wave,Keyboard,Details,MemoryAddress,MemoryValue,MemoryGo,MemoryWrite,MemorySave,ExportInfo,ExportPlaylist,MixerItemLabel,MuteItem,Activity,ActivityLabel,MixerValues };
class Inspector;
std::shared_ptr<Inspector> hold_popup(Inspector*);
class Inspector : public NativeWindow {
public:
    explicit Inspector(HWND parent,bool child) { create(parent,child,L"NSFPlay playback inspector",960,760);if(!child)ShowWindow(window,SW_SHOWNORMAL); }
    std::function<service_ptr()> owner_lease;
private:
    HWND status_=nullptr,tab_=nullptr,selector_=nullptr,mute_=nullptr,activity_=nullptr,details_=nullptr,wave_=nullptr,keyboard_=nullptr,volume_=nullptr,pan_=nullptr,master_=nullptr;
    std::shared_ptr<PlaybackSession> session_;
    std::optional<Snapshot> snapshot_;
    Settings live_=defaults();
    int page_=0,selected_=0;
    std::string shown_mixer_values_,shown_activity_,shown_details_;
    std::filesystem::path dump_path_;
    void layout() {
        RECT rc;GetClientRect(window,&rc);const int w=MulDiv(rc.right,96,dpi_),h=MulDiv(rc.bottom,96,dpi_);
        move(status_,10,8,w-20,32);
        move(GetDlgItem(window,Previous),10,44,70,25);move(GetDlgItem(window,Pause),86,44,90,25);move(GetDlgItem(window,Next),182,44,70,25);move(GetDlgItem(window,SettingsButton),w-160,44,150,25);
        move(tab_,10,78,w-20,27);
        const int selector_width=std::max(100,(w-30)/2);
        move(GetDlgItem(window,MixerItemLabel),10,111,selector_width,18);
        move(selector_,10,131,selector_width,280);
        move(mute_,20+selector_width,131,w-30-selector_width,24);
        move(details_,10,111,w-20,std::max(70,h-193));
        const int third=(w-20)/3;
        int x=10;
        for(auto [label,slider]:{std::pair{VolumeLabel,volume_},{PanLabel,pan_},{MasterLabel,master_}}) {
            move(GetDlgItem(window,label),x,164,third-6,18);
            move(slider,x,184,third-6,26);
            x+=third;
        }
        move(GetDlgItem(window,MixerValues),10,211,w-20,18);
        move(GetDlgItem(window,ActivityLabel),10,234,w-20,18);
        move(activity_,10,254,w-20,24);
        const int bw=std::max(54,(w-20-4*5)/5);
        const int actions_y=page_==0?290:234;
        int k=0;for(int id:{AllOn,AllOff,Invert,Solo,ResetMix,MonoPan,SpreadPan,RandomPan,ChannelColor,SaveDefaults}) {move(GetDlgItem(window,id),10+(k%5)*(bw+5),actions_y+(k/5)*28,bw,24);++k;}
        const int wave_y=std::max(actions_y+62,h-125);
        move(wave_,10,wave_y,w-20,52);move(keyboard_,10,wave_y+57,w-20,58);
        move(GetDlgItem(window,ExportInfo),10,h-70,130,25);move(GetDlgItem(window,ExportPlaylist),146,h-70,160,25);
        move(GetDlgItem(window,MemoryAddress),10,h-70,85,25);move(GetDlgItem(window,MemoryGo),100,h-70,85,25);move(GetDlgItem(window,MemoryValue),190,h-70,60,25);move(GetDlgItem(window,MemoryWrite),255,h-70,100,25);move(GetDlgItem(window,MemorySave),360,h-70,145,25);
    }
    void visibility() {
        const bool mixer=page_<2;
        for(int id:{MixerItemLabel,MixerItem,MuteItem,AllOn,AllOff,Invert,Solo,ResetMix,MonoPan,SpreadPan,RandomPan,ChannelColor,SaveDefaults,Volume,Master,VolumeLabel,MasterLabel,MixerValues,Wave,Keyboard})ShowWindow(GetDlgItem(window,id),mixer?SW_SHOW:SW_HIDE);
        for(int id:{Pan,PanLabel,Activity,ActivityLabel})ShowWindow(GetDlgItem(window,id),page_==0?SW_SHOW:SW_HIDE);
        ShowWindow(details_,mixer?SW_HIDE:SW_SHOW);
        for(int id:{ExportInfo,ExportPlaylist})ShowWindow(GetDlgItem(window,id),page_==2?SW_SHOW:SW_HIDE);
        for(int id:{MemoryAddress,MemoryValue,MemoryGo,MemoryWrite,MemorySave})ShowWindow(GetDlgItem(window,id),page_==3?SW_SHOW:SW_HIDE);
    }
    void populate() {
        visibility();
        if(page_<2) {
            const bool device=page_==1;
            SendMessageW(selector_,CB_RESETCONTENT,0,0);
            const auto count=device?device_names.size():channel_names.size();
            for(size_t i=0;i<count;++i) {
                const auto name=wide(device?device_names[i]:channel_names[i]);
                if(SendMessageW(selector_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()))<0)break;
            }
            SetWindowTextW(GetDlgItem(window,MixerItemLabel),device?L"Sound device":L"Sound channel");
            SetWindowTextW(GetDlgItem(window,VolumeLabel),device?L"Device volume":L"Channel volume");
            SetWindowTextW(mute_,device?L"Mute selected device":L"Mute selected channel");
            EnableWindow(GetDlgItem(window,ChannelColor),!device);
            SendMessageW(volume_,TBM_SETRANGE,TRUE,MAKELPARAM(0,device?255:128));
            SendMessageW(selector_,CB_SETCURSEL,0,0);
            select_mixer_item();
        }
        shown_details_.clear();
    }
    std::string selected_volume() const { return page_==1?std::string(device_names[selected_])+"_VOLUME":channel_key(selected_,"VOL"); }
    std::string device_mute_key() const { return std::string(device_names[selected_])+"_MUTE"; }
    static void set_slider(HWND slider,int position) {
        if(SendMessageW(slider,TBM_GETPOS,0,0)!=position)SendMessageW(slider,TBM_SETPOS,TRUE,position);
    }
    void refresh_mixer() {
        const auto mask=static_cast<uint32_t>(integer(live_,"MASK"));
        const bool muted=page_==1?integer(live_,device_mute_key())!=0:(mask&(uint32_t(1)<<selected_))!=0;
        const auto checked=muted?BST_CHECKED:BST_UNCHECKED;
        if(SendMessageW(mute_,BM_GETCHECK,0,0)!=checked)SendMessageW(mute_,BM_SETCHECK,checked,0);
        const bool can_mute=session_!=nullptr;
        if((IsWindowEnabled(mute_)!=FALSE)!=can_mute)EnableWindow(mute_,can_mute);
        const int volume=static_cast<int>(integer(live_,selected_volume()));
        const int master=static_cast<int>(integer(live_,"MASTER_VOLUME"));
        set_slider(volume_,volume);
        set_slider(master_,master);
        std::string values="Volume "+std::to_string(volume);
        if(page_==0) {
            const int pan=static_cast<int>(integer(live_,channel_key(selected_,"PAN")));
            set_slider(pan_,pan);
            values+="  |  Pan "+std::to_string(pan);
        }
        values+="  |  Master "+std::to_string(master);
        if(values!=shown_mixer_values_) {text_to(GetDlgItem(window,MixerValues),values);shown_mixer_values_=std::move(values);}
    }
    void refresh_activity() {
        // Keep the text steady while it has keyboard focus so it can be read or copied.
        if(GetFocus()==activity_)return;
        std::string activity="No data for the selected channel.";
        if(snapshot_&&snapshot_->channels[selected_].present) {
            const auto& channel=snapshot_->channels[selected_];
            std::ostringstream text;
            text<<"Chip volume "<<channel.volume<<", frequency ";
            if(integer(live_,"FREQ_MODE"))text<<std::fixed<<std::setprecision(1)<<channel.frequency<<" Hz";
            else text<<"$"<<std::hex<<std::uppercase<<channel.period<<std::dec;
            static const char* notes[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            text<<", note ";
            if(channel.key&&channel.note>0)text<<notes[channel.note%12]<<channel.note/12-1;
            else text<<"off";
            if(channel.tone>=0)text<<", tone "<<channel.tone;
            text<<", output "<<channel.output;
            activity=text.str();
        }
        if(activity!=shown_activity_) { text_to(activity_,activity);shown_activity_=std::move(activity); }
    }
    void select_mixer_item() {
        const auto index=SendMessageW(selector_,CB_GETCURSEL,0,0);
        if(index==CB_ERR)return;
        selected_=static_cast<int>(index);
        refresh_mixer();
        if(page_==0)refresh_activity();
        InvalidateRect(wave_,nullptr,FALSE);
    }
    void send() { if(session_)session_->configure(live_);refresh_mixer(); }
    static unsigned hexnumber(HWND wnd,unsigned maximum) {
        const auto text=text_of(wnd);size_t used=0;const auto n=std::stoul(text,&used,16);if(used!=text.size()||n>maximum)throw std::runtime_error("Hexadecimal value is out of range");return static_cast<unsigned>(n);
    }
    std::string memory_text() const {
        if(!snapshot_)return "Play an NSF/NSFe track to inspect memory.";
        std::ostringstream out;out<<"Memory page from emulated RAM / mapped program storage. Device registers are not read (avoids read side effects).\r\nWrites take effect when decoding resumes; buffered audio may delay audible changes.\r\n\r\n"<<std::hex<<std::uppercase<<std::setfill('0');
        for(unsigned row=0;row<16;++row) {
            out<<std::setw(4)<<snapshot_->memory_address+row*16<<": ";
            for(unsigned i=0;i<16;++i)out<<std::setw(2)<<unsigned(snapshot_->memory[row*16+i])<<" ";out<<" ";
            for(unsigned i=0;i<16;++i) {const auto c=snapshot_->memory[row*16+i];out<<char(c>=32&&c<127?c:'.');}out<<"\r\n";
        }
        return out.str();
    }
    void tick() {
        auto current=active_session();
        if(current!=session_) {session_=std::move(current);snapshot_.reset();shown_details_.clear();dump_path_.clear();live_=host_settings();}
        if(!dump_path_.empty()&&session_) {
            std::vector<uint8_t> bytes;
            {std::lock_guard guard(session_->mutex);bytes.swap(session_->memory_dump);}
            if(!bytes.empty()) {write_text(dump_path_,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});dump_path_.clear();std::lock_guard guard(session_->mutex);session_->error.clear();}
        }
        if(session_) {
            const double position=playback_position();
            const double delay=double(integer(live_,"INFO_DELAY"))/1000.0;
            snapshot_=session_->at(std::max(0.0,position-delay));
            if(snapshot_) {
                std::lock_guard guard(session_->mutex);
                if(session_->pending)live_=*session_->pending;else if(!session_->effective.empty())live_=session_->effective;
            }
            std::ostringstream out;out<<session_->path<<"  ["<<session_->subsong<<"]\r\n"<<std::fixed<<std::setprecision(2)<<position<<" s";
            if(snapshot_)out<<" / "<<snapshot_->length<<" s"<<(snapshot_->detected?"  (ending detected)":"");
            {std::lock_guard guard(session_->mutex);if(!session_->error.empty())out<<" | "<<session_->error;if(!session_->alive)out<<" | decoder finished";}
            out<<" | live edits affect playback only";text_to(status_,out.str());
        } else text_to(status_,"No NSFPlay track is playing. Add an NSF or NSFe file to a playlist.\r\nUse Preferences > Playback > Decoding > NSFPlay for saved settings.");
        if(page_<2) {
            refresh_mixer();
            if(page_==0)refresh_activity();
            InvalidateRect(wave_,nullptr,FALSE);InvalidateRect(keyboard_,nullptr,FALSE);
        } else {
            std::string value;
            if(page_==3)value=memory_text();
            else if(session_) {std::lock_guard guard(session_->mutex);value=session_->file_info+"\r\n\r\nMetadata can be edited in foobar2000 Properties. Optional NSF_PLAY_MS, NSF_LOOP_MS and NSF_FADE_MS fields set per-song timing in milliseconds. Writes use a sibling .tag sidecar; the original music file is unchanged.";}
            else value="No NSFPlay track is playing.";
            if(value!=shown_details_) {text_to(details_,value);shown_details_=std::move(value);}
        }
        SetTimer(window,1,static_cast<UINT>(1000/integer(live_,"INFO_FREQ")),nullptr);
    }
    void update_mute() {
        if(!session_||page_>=2)return;
        const bool muted=SendMessageW(mute_,BM_GETCHECK,0,0)==BST_CHECKED;
        if(page_==1)live_[device_mute_key()]=muted?"1":"0";
        else {
            const auto mask=static_cast<uint32_t>(integer(live_,"MASK"));
            const auto bit=uint32_t(1)<<selected_;
            live_["MASK"]=std::to_string(muted?mask|bit:mask&~bit);
        }
        send();
    }
    void command(int id) {
        if(id==IDCANCEL) {if(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD))DestroyWindow(window);return;}
        if(id==SettingsButton){show_settings();return;}if(id==Previous){playback_control::get()->previous();return;}if(id==Next){playback_control::get()->next();return;}if(id==Pause){playback_control::get()->play_or_pause();return;}
        if(id==ExportInfo) {const auto path=file_dialog(window,true,L"Text (*.txt)\0*.txt\0",L"txt",L"nsfplay-info.txt");if(!path.empty())write_text(path,text_of(details_));return;}
        if(!session_)throw std::runtime_error("Play an NSFPlay track first");
        if(id==ExportPlaylist) {
            const auto path=file_dialog(window,true,L"Legacy NSF playlist (*.m3u)\0*.m3u\0",L"m3u",L"nsfplay.m3u");if(path.empty())return;
            const auto file=native_path(session_->path.c_str());if(file.empty())throw std::runtime_error("Legacy playlist export requires a local file");
            write_text(path,session_->playlist);return;
        }
        if(id==MemoryGo) {const auto address=hexnumber(GetDlgItem(window,MemoryAddress),65535);std::lock_guard guard(session_->mutex);session_->memory_address=address;return;}
        if(id==MemoryWrite) {session_->write(static_cast<uint16_t>(hexnumber(GetDlgItem(window,MemoryAddress),65535)),static_cast<uint8_t>(hexnumber(GetDlgItem(window,MemoryValue),255)));return;}
        if(id==MemorySave) {const auto path=file_dialog(window,true,L"Memory dump (*.bin)\0*.bin\0",L"bin",L"nsfplay-memory.bin");if(!path.empty()) {dump_path_=path;std::lock_guard guard(session_->mutex);session_->memory_dump.clear();session_->want_dump=true;session_->error="Memory dump queued; resume playback if paused.";}return;}
        auto mask=static_cast<uint32_t>(integer(live_,"MASK"));
        if(id==AllOn) { mask=0;for(auto device:device_names)live_[std::string(device)+"_MUTE"]="0"; }
        if(id==AllOff) {mask=0xffffffff;for(auto device:device_names)live_[std::string(device)+"_MUTE"]="1";}
        if(id==Invert) { if(page_==1)for(auto device:device_names) {const auto key=std::string(device)+"_MUTE";live_[key]=integer(live_,key)?"0":"1";}else mask=~mask; }
        if(id==Solo) { if(page_==1) {for(int i=0;i<8;++i)live_[std::string(device_names[i])+"_MUTE"]=i==selected_?"0":"1";}else mask=~(uint32_t(1)<<selected_); }
        live_["MASK"]=std::to_string(mask);
        if(id==ResetMix) {
            const auto d=defaults();for(const auto& p:parameters())if(p.group=="Mixer"||p.key=="MASTER_VOLUME"||p.key.ends_with("_VOL")||p.key.ends_with("_PAN"))live_[p.key]=d.at(p.key);
        }
        if(id==MonoPan||id==SpreadPan||id==RandomPan) {
            std::mt19937 rng(std::random_device{}());for(unsigned i=0;i<32;++i)live_[channel_key(i,"PAN")]=std::to_string(id==MonoPan?128:id==SpreadPan?(i%2?224:32):int(rng()%256));
        }
        if(id==ChannelColor) {static COLORREF custom[16]={};CHOOSECOLORW choose={sizeof(choose)};choose.hwndOwner=window;choose.rgbResult=color(live_.at(channel_key(selected_,"COL")));choose.lpCustColors=custom;choose.Flags=CC_RGBINIT|CC_FULLOPEN;if(ChooseColorW(&choose))live_[channel_key(selected_,"COL")]=hexcolor(choose.rgbResult);}
        if(id==SaveDefaults) {auto saved=host_settings();for(const auto& p:parameters())if(p.live)saved[p.key]=live_.at(p.key);save_settings(saved);}
        send();
    }
    void paint_pane(const DRAWITEMSTRUCT& item) {
        const auto dc=item.hDC;RECT rc=item.rcItem;const bool dark=bool(dark_);const auto bg=CreateSolidBrush(dark?RGB(28,28,28):RGB(250,250,250));FillRect(dc,&rc,bg);DeleteObject(bg);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,dark?RGB(230,230,230):RGB(20,20,20));SelectObject(dc,font_);
        if(item.CtlID==Keyboard) {
            const int whiteCount=35;const int keyw=std::max<int>(1,(rc.right-rc.left)/whiteCount);int white=0;
            auto white_key=[](int note){const int n=note%12;return n!=1&&n!=3&&n!=6&&n!=8&&n!=10;};
            for(bool black:{false,true}) {white=0;for(int note=36;note<96;++note) {
                const bool iswhite=white_key(note);if(iswhite)++white;if(black==iswhite)continue;
                RECT key={rc.left+(white-(iswhite?1:0))*keyw-(iswhite?0:keyw/3),rc.top,rc.left+(white-(iswhite?0:0))*keyw+(iswhite?0:keyw/3),rc.bottom};
                if(!iswhite)key.bottom=rc.top+(rc.bottom-rc.top)*3/5;
                COLORREF fill=iswhite?RGB(245,245,245):RGB(30,30,30);
                if(snapshot_)for(unsigned i=0;i<32;++i) {const auto& c=snapshot_->channels[i];if(c.key&&c.note==note && !(static_cast<uint32_t>(integer(live_,"MASK"))&(uint32_t(1)<<i))&&!integer(live_,std::string(device_names[channel_devices[i]])+"_MUTE"))fill=color(live_.at(channel_key(i,"COL")));}
                const auto brush=CreateSolidBrush(fill);FillRect(dc,&key,brush);DeleteObject(brush);FrameRect(dc,&key,reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
                if(iswhite&&note%12==0) {SetTextColor(dc,RGB(80,80,80));const auto label=wide("C"+std::to_string(note/12-1));RECT text=key;text.top=text.bottom-scale(17);DrawTextW(dc,label.c_str(),-1,&text,DT_CENTER|DT_BOTTOM|DT_SINGLELINE);}
            }}
            return;
        }
        std::vector<int16_t> wave;std::string title="No chip data";
        if(snapshot_&&page_==0) {wave=snapshot_->channels[selected_].wave;title=wave.empty()?"Output history (not an audio waveform)":"Chip waveform shape";}
        if(wave.empty()&&session_) {const double time=snapshot_?snapshot_->seconds:0;std::lock_guard guard(session_->mutex);for(const auto& s:session_->history)if(s.seconds<=time)wave.push_back(static_cast<int16_t>(std::clamp(s.channels[page_==0?selected_:0].output,-32768,32767)));}
        RECT label=rc;label.bottom=label.top+scale(18);DrawTextW(dc,wide(title).c_str(),-1,&label,DT_SINGLELINE|DT_LEFT|DT_NOPREFIX);
        if(snapshot_&&snapshot_->envelope.present) {
            int x=rc.right-scale(205);
            for(const auto& pair:{std::pair{&snapshot_->envelope,"5B_ENVELOPE_COL"},std::pair{&snapshot_->noise,"5B_NOISE_COL"}}) {
                const auto brush=CreateSolidBrush(pair.first->key?color(live_.at(pair.second)):(dark?RGB(70,70,70):RGB(190,190,190)));
                RECT dot={x,rc.top+2,x+scale(12),rc.top+scale(14)};FillRect(dc,&dot,brush);DeleteObject(brush);
                RECT text={x+scale(15),rc.top,x+scale(100),rc.top+scale(18)};DrawTextW(dc,pair.second==std::string("5B_ENVELOPE_COL")?L"5B envelope":L"5B noise",-1,&text,DT_SINGLELINE|DT_LEFT);x+=scale(105);
            }
        }
        if(wave.size()<2)return;
        if(!integer(live_,"GRAPHIC_MODE")) {std::ostringstream out;out<<std::hex<<std::uppercase;for(auto v:wave)out<<std::setw(2)<<std::setfill('0')<<(uint16_t(v)&255)<<" ";RECT data=rc;data.top+=scale(18);DrawTextW(dc,wide(out.str()).c_str(),-1,&data,DT_SINGLELINE|DT_END_ELLIPSIS);return;}
        const auto [lo,hi]=std::minmax_element(wave.begin(),wave.end());const int span=std::max(1,int(*hi)-int(*lo));
        const auto pen=CreatePen(PS_SOLID,scale(2),color(live_.at(channel_key(page_==0?selected_:0,"COL"))));const auto old=SelectObject(dc,pen);
        for(size_t i=0;i<wave.size();++i) {const int x=rc.left+static_cast<int>(i)*(rc.right-rc.left-1)/static_cast<int>(wave.size()-1);const int y=rc.bottom-3-(int(wave[i])-int(*lo))*std::max<int>(1,rc.bottom-rc.top-scale(22))/span;if(!i)MoveToEx(dc,x,y,nullptr);else LineTo(dc,x,y);}
        SelectObject(dc,old);DeleteObject(pen);
    }
    LRESULT dispatch(UINT msg,WPARAM wp,LPARAM lp) override {
        if(msg==WM_CREATE) {
            status_=control(L"STATIC",L"",Status);control(L"BUTTON",L"&Previous",Previous,WS_TABSTOP);control(L"BUTTON",L"&Play / pause",Pause,WS_TABSTOP);control(L"BUTTON",L"&Next",Next,WS_TABSTOP);control(L"BUTTON",L"&Preferences...",SettingsButton,WS_TABSTOP);
            tab_=control(WC_TABCONTROLW,L"Inspector pages",Tab,WS_TABSTOP);int i=0;for(auto title:{L"Channels",L"Device mixer",L"File information",L"Memory"}) {TCITEMW t={};t.mask=TCIF_TEXT;t.pszText=const_cast<wchar_t*>(title);SendMessageW(tab_,TCM_INSERTITEMW,i++,reinterpret_cast<LPARAM>(&t));}
            control(L"STATIC",L"Sound channel",MixerItemLabel);
            selector_=control(WC_COMBOBOXW,L"",MixerItem,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
            mute_=control(L"BUTTON",L"Mute selected channel",MuteItem,BS_AUTOCHECKBOX|WS_TABSTOP);
            control(L"STATIC",L"Channel volume",VolumeLabel);
            volume_=control(TRACKBAR_CLASSW,L"",Volume,WS_TABSTOP|TBS_HORZ);
            control(L"STATIC",L"Channel pan",PanLabel);
            pan_=control(TRACKBAR_CLASSW,L"",Pan,WS_TABSTOP|TBS_HORZ);
            control(L"STATIC",L"Master volume",MasterLabel);
            master_=control(TRACKBAR_CLASSW,L"",Master,WS_TABSTOP|TBS_HORZ);
            control(L"STATIC",L"Channel activity",ActivityLabel);
            activity_=control(L"EDIT",L"",Activity,WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);
            control(L"STATIC",L"",MixerValues);
            for(auto [id,title]:{std::pair{AllOn,L"All on"},{AllOff,L"Mute all"},{Invert,L"Invert"},{Solo,L"Solo"},{ResetMix,L"Reset mix"},{MonoPan,L"Center pan"},{SpreadPan,L"Spread"},{RandomPan,L"Random pan"},{ChannelColor,L"Color..."},{SaveDefaults,L"Save as default"}})control(L"BUTTON",title,id,WS_TABSTOP);
            SendMessageW(pan_,TBM_SETRANGE,TRUE,MAKELPARAM(0,255));SendMessageW(master_,TBM_SETRANGE,TRUE,MAKELPARAM(0,256));
            wave_=control(L"STATIC",L"Waveform",Wave,SS_OWNERDRAW);keyboard_=control(L"STATIC",L"Chip notes keyboard",Keyboard,SS_OWNERDRAW);
            details_=control(L"EDIT",L"",Details,WS_TABSTOP|ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_HSCROLL,WS_EX_CLIENTEDGE);
            control(L"BUTTON",L"Export &information...",ExportInfo,WS_TABSTOP);control(L"BUTTON",L"Export legacy &playlist...",ExportPlaylist,WS_TABSTOP);
            control(L"EDIT",L"0000",MemoryAddress,WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);control(L"BUTTON",L"&View page",MemoryGo,WS_TABSTOP);control(L"EDIT",L"00",MemoryValue,WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);control(L"BUTTON",L"&Write byte",MemoryWrite,WS_TABSTOP);control(L"BUTTON",L"Save 64 KiB dump...",MemorySave,WS_TABSTOP);
            populate();theme();layout();if(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD))modeless_dialog_manager::g_add(window);SetTimer(window,1,33,nullptr);return 0;
        }
        if(msg==WM_SIZE) {if(details_)layout();return 0;}
        if(msg==WM_GETMINMAXINFO) {auto mm=reinterpret_cast<MINMAXINFO*>(lp);mm->ptMinTrackSize={scale(620),scale(520)};return 0;}
        if(msg==WM_TIMER) {tick();return 0;}
        if(msg==WM_DRAWITEM) {paint_pane(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
        if(msg==WM_HSCROLL&&session_&&page_<2) {
            const auto slider=reinterpret_cast<HWND>(lp);
            const auto value=std::to_string(SendMessageW(slider,TBM_GETPOS,0,0));
            if(slider==volume_)live_[selected_volume()]=value;
            else if(slider==pan_&&page_==0)live_[channel_key(selected_,"PAN")]=value;
            else if(slider==master_)live_["MASTER_VOLUME"]=value;
            else return 0;
            send();return 0;
        }
        if(msg==WM_COMMAND) {
            const int id=LOWORD(wp),event=HIWORD(wp);
            if(id==MixerItem&&event==CBN_SELCHANGE&&page_<2) {select_mixer_item();return 0;}
            if(id==MuteItem&&event==BN_CLICKED) {update_mute();return 0;}
            if(event==BN_CLICKED) {auto popup_keep=hold_popup(this);auto owner_keep=owner_lease?owner_lease():service_ptr{};command(id);return 0;}
        }
        if(msg==WM_NOTIFY) {
            const auto n=reinterpret_cast<NMHDR*>(lp);
            if(n->idFrom==Tab&&n->code==TCN_SELCHANGE) {page_=TabCtrl_GetCurSel(tab_);populate();layout();tick();return 0;}
        }
        if(msg==WM_DESTROY) {KillTimer(window,1);if(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD))modeless_dialog_manager::g_remove(window);session_.reset();return 0;}
        return NativeWindow::dispatch(msg,wp,lp);
    }
};
std::shared_ptr<Inspector> popup;
std::shared_ptr<Inspector> hold_popup(Inspector* ptr) {return popup.get()==ptr?popup:nullptr;}
const GUID inspector_guid={0xe1492d07,0x5172,0x446a,{0xa2,0x9a,0x76,0xb0,0x5a,0xb1,0x46,0x30}};
class InspectorElementInstance : public ui_element_instance {
public:
    InspectorElementInstance(ui_element_config::ptr config,ui_element_instance_callback::ptr callback):config_(config),callback_(callback) {}
    void initialize_window(HWND parent) {ui_=std::make_unique<Inspector>(parent,true);ui_->owner_lease=[this]()->service_ptr{return service_ptr_t<ui_element_instance>(this);};}
    HWND get_wnd() override {return ui_?ui_->window:nullptr;}
    void set_configuration(ui_element_config::ptr config) override {config_=config;}
    ui_element_config::ptr get_configuration() override {return config_;}
    GUID get_guid() override {return inspector_guid;}
    GUID get_subclass() override {return ui_element_subclass_playback_information;}
private:ui_element_config::ptr config_;ui_element_instance_callback::ptr callback_;std::unique_ptr<Inspector> ui_;
};
class InspectorElement : public ui_element_v2 {
public:
    GUID get_guid() override {return inspector_guid;}
    GUID get_subclass() override {return ui_element_subclass_playback_information;}
    void get_name(pfc::string_base& out) override {out="NSFPlay inspector";}
    ui_element_instance::ptr instantiate(HWND parent,ui_element_config::ptr cfg,ui_element_instance_callback::ptr cb) override {
        auto instance=fb2k::service_new<InspectorElementInstance>(cfg,cb);
        instance->initialize_window(parent);
        return instance;
    }
    ui_element_config::ptr get_default_configuration() override {return ui_element_config::g_create_empty(inspector_guid);}
    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr) override {return nullptr;}
    t_uint32 get_flags() override {return KFlagHavePopupCommand|KFlagSupportsBump;}
    bool bump() override {show_inspector();return true;}
};
static service_factory_single_t<InspectorElement> inspector_element_factory;
const GUID menu_group={0x98322bb5,0xf3ec,0x4807,{0x86,0xe1,0xe4,0xc6,0x1e,0xcd,0x20,0x13}};
static mainmenu_group_popup_factory menu_popup(menu_group,mainmenu_groups::view,mainmenu_commands::sort_priority_dontcare,"NSFPlay");
class Menu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override {return 2;}
    GUID get_command(t_uint32 index) override {auto id=inspector_guid;id.Data1+=index+1;return id;}
    void get_name(t_uint32 index,pfc::string_base& out) override {out=index?"Preferences":"Playback inspector";}
    bool get_description(t_uint32 index,pfc::string_base& out) override {out=index?"Edit NSFPlay settings and presets.":"Inspect chip notes, mixer, masks and memory.";return true;}
    GUID get_parent() override {return menu_group;}
    void execute(t_uint32 index,service_ptr_t<service_base>) override {if(index)show_settings();else show_inspector();}
};
static mainmenu_commands_factory_t<Menu> menu_factory;
}
void show_inspector() { if(!popup||!popup->window)popup=std::make_shared<Inspector>(core_api::get_main_window(),false);ShowWindow(popup->window,SW_RESTORE);SetForegroundWindow(popup->window); }
void close_inspector() {popup.reset();}
}
