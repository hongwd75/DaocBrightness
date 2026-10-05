#include "Render.h"
#include <cstdio>
#include <cstdlib>
using namespace Brightness;
int failures=0;
bool fullscreen=false;
void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)failures++;}
bool Pixel(const unsigned char* p,bool bgra,Settings s){
    float c[3]={64.f/255,96.f/255,128.f/255};Correct(c,s);
    return std::abs(int(p[bgra?2:0])-int(std::round(c[0]*255)))<=3 &&
        std::abs(int(p[1])-int(std::round(c[1]*255)))<=3 &&
        std::abs(int(p[bgra?0:2])-int(std::round(c[2]*255)))<=3;
}
void Cpu(){
    float c[3]={.1f,.4f,.8f};Correct(c,{});Check(std::abs(c[0]-.1f)<.00001f && std::abs(c[2]-.8f)<.00001f,"default identity");
    Settings s;s.gamma=2;float g[3]={.25f,.25f,.25f};Correct(g,s);Check(std::abs(g[0]-.5f)<.00001f,"gamma brightens");
    s={};s.shadows=1;float dark[3]={0,0,0};Correct(dark,s);Check(dark[0]>.44f,"shadow lift");
    s={};s.saturation=0;float color[3]={.1f,.4f,.8f};Correct(color,s);Check(color[0]==color[1] && color[1]==color[2],"desaturation");
    s={};s.exposure=2;float clip[3]={.5f,.5f,.5f};Correct(clip,s);Check(clip[0]==1,"output clamp");
}
void Test9(HWND w){
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);IDirect3DDevice9* d=nullptr;
    D3DDISPLAYMODE mode{};if(api)api->GetAdapterDisplayMode(0,&mode);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=!fullscreen;pp.hDeviceWindow=w;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=fullscreen?mode.Width:64;pp.BackBufferHeight=fullscreen?mode.Height:64;pp.BackBufferFormat=fullscreen?mode.Format:D3DFMT_A8R8G8B8;
    HRESULT hr=api?api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d):E_FAIL;
    Check(SUCCEEDED(hr),"DX9 device");if(FAILED(hr)){Drop(api);return;}
    Render9 render;Settings cases[6]{};cases[1].gamma=1.8f;cases[2].exposure=.5f;cases[3].contrast=1.3f;cases[4].saturation=0;cases[5].shadows=.7f;
    for(int i=0;i<6;i++){
        d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(64,96,128),1,0);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
        hr=render.Apply(d,cases[i]);Check(SUCCEEDED(hr),"DX9 shader apply");if(FAILED(hr))printf("HRESULT %08lX\n",hr);
        DWORD blend=0,scissor=0;d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
        Check(blend==TRUE && scissor==TRUE,"DX9 state restore");
        IDirect3DSurface9* back=nullptr;IDirect3DSurface9* staging=nullptr;d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
        d->CreateOffscreenPlainSurface(pp.BackBufferWidth,pp.BackBufferHeight,pp.BackBufferFormat,D3DPOOL_SYSTEMMEM,&staging,nullptr);
        hr=d->GetRenderTargetData(back,staging);D3DLOCKED_RECT locked{};
        bool ok=SUCCEEDED(hr) && SUCCEEDED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        if(ok){ok=Pixel(static_cast<unsigned char*>(locked.pBits)+32*locked.Pitch+32*4,true,cases[i]);staging->UnlockRect();}
        Check(ok,"DX9 GPU pixels match formula");Drop(staging);Drop(back);
    }
    render.Reset();Check(SUCCEEDED(d->Reset(&pp)),"DX9 reset");
    d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(64,96,128),1,0);Check(SUCCEEDED(render.Apply(d,cases[1])),"DX9 after reset");
    render.Reset();Drop(d);Drop(api);
}
void Test10(HWND w){
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=1;sd.BufferDesc.Width=64;sd.BufferDesc.Height=64;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=w;sd.SampleDesc.Count=1;sd.Windowed=TRUE;
    IDXGISwapChain* chain=nullptr;ID3D10Device* d=nullptr;
    HRESULT hr=D3D10CreateDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,&sd,&chain,&d);
    if(FAILED(hr))hr=D3D10CreateDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_WARP,nullptr,0,D3D10_SDK_VERSION,&sd,&chain,&d);
    Check(SUCCEEDED(hr),"DX10 device");if(FAILED(hr))return;
    if(fullscreen){hr=chain->SetFullscreenState(TRUE,nullptr);Check(SUCCEEDED(hr),"DX10 enter exclusive fullscreen");BOOL exclusive=FALSE;chain->GetFullscreenState(&exclusive,nullptr);Check(exclusive==TRUE,"DX10 exclusive state");}
    Render10 render;Settings cases[6]{};cases[1].gamma=1.8f;cases[2].exposure=.5f;cases[3].contrast=1.3f;cases[4].saturation=0;cases[5].shadows=.7f;
    for(int i=0;i<6;i++){
        ID3D10Texture2D* back=nullptr;ID3D10RenderTargetView* target=nullptr;
        chain->GetBuffer(0,__uuidof(ID3D10Texture2D),reinterpret_cast<void**>(&back));d->CreateRenderTargetView(back,nullptr,&target);
        float color[4]={64.f/255,96.f/255,128.f/255,1};d->ClearRenderTargetView(target,color);d->OMSetRenderTargets(1,&target,nullptr);
        d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
        hr=render.Apply(chain,cases[i]);Check(SUCCEEDED(hr),"DX10 shader apply");if(FAILED(hr))printf("HRESULT %08lX\n",hr);
        D3D10_PRIMITIVE_TOPOLOGY topology{};d->IAGetPrimitiveTopology(&topology);ID3D10RenderTargetView* restored=nullptr;d->OMGetRenderTargets(1,&restored,nullptr);
        Check(topology==D3D10_PRIMITIVE_TOPOLOGY_LINELIST && restored==target,"DX10 state restore");Drop(restored);
        D3D10_TEXTURE2D_DESC td{};back->GetDesc(&td);td.Usage=D3D10_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
        ID3D10Texture2D* staging=nullptr;hr=d->CreateTexture2D(&td,nullptr,&staging);bool ok=false;
        if(SUCCEEDED(hr)){d->CopyResource(staging,back);D3D10_MAPPED_TEXTURE2D map{};if(SUCCEEDED(staging->Map(0,D3D10_MAP_READ,0,&map))){ok=Pixel(static_cast<unsigned char*>(map.pData)+32*map.RowPitch+32*4,false,cases[i]);staging->Unmap(0);}}
        Check(ok,"DX10 GPU pixels match formula");d->OMSetRenderTargets(0,nullptr,nullptr);Drop(staging);Drop(target);Drop(back);
    }
    render.Reset();Check(SUCCEEDED(chain->ResizeBuffers(1,80,80,DXGI_FORMAT_UNKNOWN,0)),"DX10 resize");Check(SUCCEEDED(render.Apply(chain,cases[1])),"DX10 after resize");
    render.Reset();if(fullscreen)chain->SetFullscreenState(FALSE,nullptr);Drop(chain);Drop(d);
}
void Integration(HWND w,int version){
    wchar_t name[96]{};Name(name,_countof(name),GetCurrentProcessId());
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Channel),name);
    Channel* ch=static_cast<Channel*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Channel)));
    Check(ch!=nullptr,"hook channel");if(!ch)return;
    ZeroMemory(ch,sizeof(Channel));ch->protocol=Protocol;ch->settings=Settings{};ch->owner=GetCurrentProcessId();ch->enabled=1;ch->heartbeat=GetTickCount();
    IDirect3D9* api=nullptr;IDirect3DDevice9* d9=nullptr;ID3D10Device* d10=nullptr;IDXGISwapChain* chain=nullptr;
    D3DPRESENT_PARAMETERS pp{};
    HRESULT hr=E_FAIL;
    if(version==9){
        api=Direct3DCreate9(D3D_SDK_VERSION);D3DDISPLAYMODE mode{};api->GetAdapterDisplayMode(0,&mode);
        pp.Windowed=!fullscreen;pp.hDeviceWindow=w;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=fullscreen?mode.Width:64;pp.BackBufferHeight=fullscreen?mode.Height:64;pp.BackBufferFormat=fullscreen?mode.Format:D3DFMT_A8R8G8B8;
        hr=api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d9);
    }else{
        DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=1;sd.BufferDesc.Width=64;sd.BufferDesc.Height=64;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=w;sd.SampleDesc.Count=1;sd.Windowed=TRUE;
        hr=D3D10CreateDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,&sd,&chain,&d10);
        if(SUCCEEDED(hr) && fullscreen){
            MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            SetForegroundWindow(w);Sleep(100);
            hr=chain->SetFullscreenState(TRUE,nullptr);BOOL exclusive=FALSE;chain->GetFullscreenState(&exclusive,nullptr);Check(exclusive==TRUE,"hook DX10 exclusive state");printf("Fullscreen HRESULT %08lX\n",hr);
        }
    }
    Check(SUCCEEDED(hr),"hook test device");
    if(SUCCEEDED(hr)){
        wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);wchar_t* slash=wcsrchr(path,L'\\');if(slash)wcscpy_s(slash+1,MAX_PATH-size_t(slash+1-path),L"DaocBrightnessHook.dll");
        HMODULE loaded=LoadLibraryW(path);Check(loaded!=nullptr,"load hook DLL");
        auto present=[&](){
            MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            if(d9){d9->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(64,96,128),1,0);d9->Present(nullptr,nullptr,nullptr,nullptr);}else chain->Present(0,0);
        };
        for(int i=0;i<50 && ch->renderer!=version;i++){ch->heartbeat=GetTickCount();present();Sleep(100);}
        Check(ch->renderer==version && ch->frames>0,"injected Present callback");printf("Hook renderer=%ld error=%08lX frames=%ld\n",ch->renderer,ch->error,ch->frames);
        Settings restored{1.7f,.4f,1.2f,.8f,.35f},published;
        RequestSettings(ch,restored);ch->heartbeat=GetTickCount();present();
        Check(ReadPublishedSettings(ch,published) && Same(published,restored),"hook restores saved controls");
        RequestSettings(ch,Settings{});present();
        Check(ReadPublishedSettings(ch,published) && Default(published),"hook applies defaults reset");
        hr=d9?d9->Reset(&pp):chain->ResizeBuffers(1,80,80,DXGI_FORMAT_UNKNOWN,0);
        Check(SUCCEEDED(hr),"hook reset or resize");
        LONG beforeReset=ch->frames;ch->heartbeat=GetTickCount();present();Check(ch->frames>beforeReset,"callback after reset or resize");
        LONG frames=ch->frames;ch->heartbeat=GetTickCount()-6000;present();Check(ch->frames==frames,"heartbeat expiry stops correction");
        ch->heartbeat=GetTickCount();present();Check(ch->frames>frames,"reconnect resumes callback");
        ch->enabled=0;frames=ch->frames;present();Check(ch->frames==frames,"disconnect stops callback");
    }
    if(chain && fullscreen)chain->SetFullscreenState(FALSE,nullptr);Drop(chain);Drop(d10);Drop(d9);Drop(api);
    UnmapViewOfFile(ch);CloseHandle(mapping);
}
int main(int argc,char** argv){
    int hookVersion=0;for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--fullscreen"))fullscreen=true;if(!strcmp(argv[i],"--hook-dx9"))hookVersion=9;if(!strcmp(argv[i],"--hook-dx10"))hookVersion=10;}
    Cpu();HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=instance;wc.lpszClassName=L"BrightnessRenderTests";RegisterClassW(&wc);
    HWND w=CreateWindowW(wc.lpszClassName,L"Render verification",fullscreen?WS_POPUP:WS_OVERLAPPEDWINDOW,0,0,fullscreen?GetSystemMetrics(SM_CXSCREEN):100,fullscreen?GetSystemMetrics(SM_CYSCREEN):100,nullptr,nullptr,instance,nullptr);
    ShowWindow(w,SW_SHOWNORMAL);SetForegroundWindow(w);
    // DXGI requires an activated output window. DX9's exclusive test establishes
    // activation for command-line runs where foreground focus is restricted.
    if(hookVersion==10 && fullscreen)Test9(w);
    if(hookVersion)Integration(w,hookVersion);else{Test9(w);Test10(w);}DestroyWindow(w);printf("Failures: %d\n",failures);return failures?1:0;
}
