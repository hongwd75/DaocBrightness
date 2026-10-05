#include "Render.h"
#include "UiText.h"
#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_dx10.h"
#include <mutex>
#include <string>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace {
using namespace Brightness;
using Ui::Id;
Channel* channel=nullptr; HANDLE mapping=nullptr;
std::recursive_mutex mutex;
Render9 r9; Render10 r10;
HWND window=nullptr; WNDPROC previous=nullptr;
ImGuiContext* gui=nullptr; void* device=nullptr; int backend=0;
bool shown=false, hotkey=false;
Settings settings;
LONG commandSerial=0;
using Present9=HRESULT (WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using Reset9=HRESULT (WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using Present10=HRESULT (WINAPI*)(IDXGISwapChain*,UINT,UINT);
using Resize10=HRESULT (WINAPI*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
Present9 original9=nullptr; Reset9 reset9=nullptr;
Present10 original10=nullptr; Resize10 resize10=nullptr;
bool Alive() {
    return channel && channel->protocol==Protocol && InterlockedCompareExchange(&channel->enabled,0,0) &&
        DWORD(GetTickCount()-DWORD(InterlockedCompareExchange(&channel->heartbeat,0,0)))<5000;
}
bool Target(HWND w) {
    DWORD pid=0; GetWindowThreadProcessId(w,&pid);
    return w && pid==GetCurrentProcessId() && IsWindowVisible(w);
}
bool Focused() { return window && GetAncestor(GetForegroundWindow(),GA_ROOT)==GetAncestor(window,GA_ROOT); }
LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    std::unique_lock<std::recursive_mutex> guard(mutex);
    if(gui && shown && Alive()) {
        ImGui::SetCurrentContext(gui);
        ImGui_ImplWin32_WndProcHandler(w,msg,wp,lp);
        if((msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN) && wp==VK_HOME && (GetKeyState(VK_CONTROL)&0x8000)) return 0;
        if(msg>=WM_MOUSEFIRST && msg<=WM_MOUSELAST) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(w,&p);
            if(p.x>=12 && p.x<=420 && p.y>=12 && p.y<=350) return 0;
        }
        if((msg>=WM_KEYFIRST && msg<=WM_KEYLAST) && ImGui::GetIO().WantCaptureKeyboard) return 0;
    }
    WNDPROC call=previous; guard.unlock();
    return CallWindowProcW(call,w,msg,wp,lp);
}
void ShutdownUi() {
    shown=false;
    if(window && previous && IsWindow(window) && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC))==WindowProc)
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
    if(gui){ImGui::SetCurrentContext(gui); if(backend==9)ImGui_ImplDX9_Shutdown(); else if(backend==10)ImGui_ImplDX10_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(gui);}
    gui=nullptr; window=nullptr; previous=nullptr; device=nullptr; backend=0; hotkey=false;
}
bool Init(HWND w,void* d,int version) {
    if(gui && window==w && device==d && backend==version) return true;
    ShutdownUi();
    if(!Target(w))return false;
    gui=ImGui::CreateContext(); ImGui::SetCurrentContext(gui);
    ImGuiIO& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    wchar_t path[MAX_PATH]{}; GetWindowsDirectoryW(path,MAX_PATH);
    const bool korean=Ui::Current()==Ui::Language::Korean;
    std::wstring font=std::wstring(path)+(korean?L"\\Fonts\\malgun.ttf":L"\\Fonts\\segoeui.ttf");
    char utf8[MAX_PATH*3]{}; WideCharToMultiByte(CP_UTF8,0,font.c_str(),-1,utf8,sizeof(utf8),nullptr,nullptr);
    if(GetFileAttributesW(font.c_str())!=INVALID_FILE_ATTRIBUTES) io.Fonts->AddFontFromFileTTF(utf8,18.f,nullptr,korean?io.Fonts->GetGlyphRangesKorean():io.Fonts->GetGlyphRangesDefault());
    if(io.Fonts->Fonts.empty())io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark(); ImGui::GetStyle().WindowRounding=8;
    if(!ImGui_ImplWin32_Init(w)) {ImGui::DestroyContext(gui); gui=nullptr; return false;}
    bool ok=version==9?ImGui_ImplDX9_Init(static_cast<IDirect3DDevice9*>(d)):ImGui_ImplDX10_Init(static_cast<ID3D10Device*>(d));
    if(!ok){ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(gui);gui=nullptr;return false;}
    window=w; device=d; backend=version;
    SetLastError(0); previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(WindowProc)));
    if(!previous){ShutdownUi();return false;} return true;
}
void Row(const char* label,const char* id,float& value,float step,float low,float high) {
    ImGui::PushID(id); ImGui::TextUnformatted(label); ImGui::SameLine(175);
    if(ImGui::Button("-",ImVec2(32,28))) value=std::max(low,value-step);
    ImGui::SameLine(); ImGui::Text("%5.2f",value); ImGui::SameLine();
    if(ImGui::Button("+",ImVec2(32,28))) value=std::min(high,value+step);
    ImGui::PopID();
}
void Menu() {
    ImGui::SetCurrentContext(gui);
    bool down=Focused() && (GetAsyncKeyState(VK_CONTROL)&0x8000) && (GetAsyncKeyState(VK_HOME)&0x8000);
    if(down && !hotkey) shown=!shown;
    hotkey=down; if(!Focused())shown=false;
    if(backend==9)ImGui_ImplDX9_NewFrame(); else ImGui_ImplDX10_NewFrame();
    ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    if(shown){
        ImGui::SetNextWindowPos(ImVec2(12,12),ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(400,330),ImGuiCond_Always);
        ImGui::Begin(Ui::N(Id::MainTitle),&shown,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);
        ImGui::Text("DirectX %d | Ctrl+Home",backend); ImGui::Separator();
        Row(Ui::N(Id::Gamma),"gamma",settings.gamma,.1f,.5f,3.f);
        Row(Ui::N(Id::Exposure),"exposure",settings.exposure,.1f,-2.f,2.f);
        Row(Ui::N(Id::Contrast),"contrast",settings.contrast,.1f,.5f,2.f);
        Row(Ui::N(Id::Saturation),"saturation",settings.saturation,.1f,0.f,2.f);
        Row(Ui::N(Id::Shadows),"shadows",settings.shadows,.05f,0.f,1.f);
        ImGui::TextUnformatted(Ui::N(Id::ShadowHelp));
        ImGui::Separator();
        if(ImGui::Button(Ui::N(Id::Reset),ImVec2(-1,30)))settings=Settings{};
        ImGui::End();
    }
    ImGui::GetIO().MouseDrawCursor=shown;
    ImGui::Render();
    PublishSettings(channel,settings,commandSerial);
    InterlockedExchange(&channel->visible,shown?1:0);
}
void ReadSettings() {
    ReadRequestedSettings(channel,commandSerial,settings);
}
HRESULT WINAPI OnPresent9(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND overrideWindow,const RGNDATA* dirty) {
    std::unique_lock<std::recursive_mutex> guard(mutex);
    D3DDEVICE_CREATION_PARAMETERS creation{}; d->GetCreationParameters(&creation);
    HWND w=overrideWindow?overrideWindow:creation.hFocusWindow;
    if(!overrideWindow){IDirect3DSwapChain9* swap=nullptr;if(SUCCEEDED(d->GetSwapChain(0,&swap))){D3DPRESENT_PARAMETERS p{};if(SUCCEEDED(swap->GetPresentParameters(&p)) && p.hDeviceWindow)w=p.hDeviceWindow;Drop(swap);}}
    if(Alive() && Target(w) && Init(w,d,9)) {
        ReadSettings(); Menu(); HRESULT hr=r9.Apply(d,settings);
        if(shown) {
            IDirect3DStateBlock9* saved=nullptr;
            if(SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&saved)) && SUCCEEDED(saved->Capture()) && SUCCEEDED(d->BeginScene())) {
                IDirect3DSurface9* back=nullptr; IDirect3DSurface9* old[4]{}; IDirect3DSurface9* depth=nullptr;
                D3DCAPS9 caps{};d->GetDeviceCaps(&caps);UINT slots=std::min<UINT>(caps.NumSimultaneousRTs,4);
                for(UINT i=0;i<slots;i++)d->GetRenderTarget(i,&old[i]);
                d->GetDepthStencilSurface(&depth); d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
                d->SetDepthStencilSurface(nullptr);for(UINT i=1;i<slots;i++)d->SetRenderTarget(i,nullptr);
                if(back)d->SetRenderTarget(0,back);
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                for(UINT i=0;i<slots;i++){d->SetRenderTarget(i,old[i]);Drop(old[i]);}
                d->SetDepthStencilSurface(depth);Drop(back);Drop(depth);d->EndScene();saved->Apply();
            }
            Drop(saved);
        }
        InterlockedExchange(&channel->renderer,9); InterlockedExchange(&channel->error,hr); InterlockedIncrement(&channel->frames);
    } else if(gui && !Alive()){ShutdownUi();r9.Reset();r10.Reset();}
    guard.unlock(); return original9(d,a,b,overrideWindow,dirty);
}
HRESULT WINAPI OnReset9(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    std::unique_lock<std::recursive_mutex> guard(mutex);
    r9.Reset(); if(gui && backend==9 && device==d)ImGui_ImplDX9_InvalidateDeviceObjects();
    guard.unlock();return reset9(d,p);
}
HRESULT WINAPI OnPresent10(IDXGISwapChain* chain,UINT interval,UINT flags) {
    std::unique_lock<std::recursive_mutex> guard(mutex);
    DXGI_SWAP_CHAIN_DESC desc{}; chain->GetDesc(&desc);
    ID3D10Device* d=nullptr;
    if(!(flags&DXGI_PRESENT_TEST) && Alive() && Target(desc.OutputWindow) && SUCCEEDED(chain->GetDevice(__uuidof(ID3D10Device),reinterpret_cast<void**>(&d))) && Init(desc.OutputWindow,d,10)) {
        ReadSettings(); Menu(); HRESULT hr=r10.Apply(chain,settings);
        if(shown){
            ID3D10Texture2D* back=nullptr; ID3D10RenderTargetView* target=nullptr;
            ID3D10RenderTargetView* old[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT]{}; ID3D10DepthStencilView* depth=nullptr;
            d->OMGetRenderTargets(D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT,old,&depth);
            if(SUCCEEDED(chain->GetBuffer(0,__uuidof(ID3D10Texture2D),reinterpret_cast<void**>(&back))) && SUCCEEDED(d->CreateRenderTargetView(back,nullptr,&target))) {
                d->OMSetRenderTargets(1,&target,nullptr); ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());
            }
            d->OMSetRenderTargets(D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT,old,depth);
            for(auto& v:old)Drop(v);Drop(depth);Drop(target);Drop(back);
        }
        InterlockedExchange(&channel->renderer,10);InterlockedExchange(&channel->error,hr);InterlockedIncrement(&channel->frames);
    } else if(gui && !Alive()){ShutdownUi();r9.Reset();r10.Reset();}
    Drop(d);guard.unlock();return original10(chain,interval,flags);
}
HRESULT WINAPI OnResize10(IDXGISwapChain* chain,UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags) {
    std::unique_lock<std::recursive_mutex> guard(mutex);
    r10.Reset(); if(gui && backend==10)ImGui_ImplDX10_InvalidateDeviceObjects();
    guard.unlock();return resize10(chain,count,w,h,format,flags);
}
bool HookPair(void* a,void* da,void** oa,void* b,void* db,void** ob) {
    if(MH_CreateHook(a,da,oa)!=MH_OK)return false;
    if(MH_CreateHook(b,db,ob)!=MH_OK){MH_RemoveHook(a);return false;}
    if(MH_EnableHook(b)==MH_OK && MH_EnableHook(a)==MH_OK)return true;
    MH_DisableHook(a);MH_DisableHook(b);MH_RemoveHook(a);MH_RemoveHook(b);return false;
}
DWORD WINAPI Start(void*) {
    wchar_t name[96]{};Name(name,_countof(name),GetCurrentProcessId());
    mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    if(!mapping)return 0;
    channel=static_cast<Channel*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Channel)));
    if(!channel || channel->protocol!=Protocol)return 0;
    MH_STATUS initialized=MH_Initialize();
    if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED){InterlockedExchange(&channel->error,E_FAIL);return 0;}
    // Dummy devices locate runtime vtables; no game memory offsets are used.
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DaocBrightnessProbe";
    RegisterClassW(&wc); HWND probe=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    bool have9=false,have10=false;
    for(int attempt=0;attempt<120 && (!have9 || !have10);attempt++) {
        if(!have9 && GetModuleHandleW(L"d3d9.dll")) {
            IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);IDirect3DDevice9* d=nullptr;
            D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=probe;
            HRESULT result=api?api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,probe,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d):E_FAIL;
            if(FAILED(result) && api)result=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_NULLREF,probe,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);
            if(SUCCEEDED(result)) {
                void** v=*reinterpret_cast<void***>(d);
                have9=HookPair(v[17],reinterpret_cast<void*>(OnPresent9),reinterpret_cast<void**>(&original9),
                    v[16],reinterpret_cast<void*>(OnReset9),reinterpret_cast<void**>(&reset9));
            } else InterlockedExchange(&channel->error,result);
            Drop(d);Drop(api);
        }
        if(!have10 && GetModuleHandleW(L"d3d10.dll")) {
            DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=1;sd.BufferDesc.Width=64;sd.BufferDesc.Height=64;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=probe;sd.SampleDesc.Count=1;sd.Windowed=TRUE;
            IDXGISwapChain* swap=nullptr;ID3D10Device* d=nullptr;
            HRESULT hr=D3D10CreateDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,&sd,&swap,&d);
            if(FAILED(hr))hr=D3D10CreateDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_WARP,nullptr,0,D3D10_SDK_VERSION,&sd,&swap,&d);
            if(SUCCEEDED(hr)){void** v=*reinterpret_cast<void***>(swap);
                have10=HookPair(v[8],reinterpret_cast<void*>(OnPresent10),reinterpret_cast<void**>(&original10),
                    v[13],reinterpret_cast<void*>(OnResize10),reinterpret_cast<void**>(&resize10));
            }Drop(swap);Drop(d);
        }
        if(!have9 || !have10)Sleep(500);
    }
    DestroyWindow(probe);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    if(!have9 && !have10)InterlockedExchange(&channel->error,E_NOINTERFACE);
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module); HANDLE thread=CreateThread(nullptr,0,Start,nullptr,0,nullptr);if(thread)CloseHandle(thread);}
    return TRUE;
}
