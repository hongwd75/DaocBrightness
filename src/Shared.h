#pragma once
#include <windows.h>
#include <cmath>
#include <algorithm>
namespace Brightness {
constexpr DWORD Protocol = 2;
struct Settings { float gamma=1.f, exposure=0.f, contrast=1.f, saturation=1.f, shadows=0.f; };
struct Channel {
    DWORD protocol;
    volatile LONG enabled, owner, heartbeat, sequence;
    Settings settings;
    volatile LONG renderer, frames, error, visible;
    Settings requested;
    volatile LONG settingsSequence, settingsApplied;
};
inline void Name(wchar_t* buffer, size_t count, DWORD pid) { swprintf_s(buffer,count,L"Local\\DaocBrightness-%lu",pid); }
inline Settings Clamp(Settings s) {
    s.gamma=std::clamp(s.gamma,.5f,3.f); s.exposure=std::clamp(s.exposure,-2.f,2.f);
    s.contrast=std::clamp(s.contrast,.5f,2.f); s.saturation=std::clamp(s.saturation,0.f,2.f);
    s.shadows=std::clamp(s.shadows,0.f,1.f); return s;
}
inline bool Default(const Settings& s) { return s.gamma==1 && s.exposure==0 && s.contrast==1 && s.saturation==1 && s.shadows==0; }
inline bool Same(const Settings& a,const Settings& b){return a.gamma==b.gamma && a.exposure==b.exposure && a.contrast==b.contrast && a.saturation==b.saturation && a.shadows==b.shadows;}
inline void RequestSettings(Channel* channel,Settings value){
    InterlockedIncrement(&channel->sequence);channel->requested=Clamp(value);MemoryBarrier();InterlockedIncrement(&channel->sequence);
}
inline void ReadRequestedSettings(Channel* channel,LONG& serial,Settings& value){
    LONG next=InterlockedCompareExchange(&channel->sequence,0,0);
    if((next&1) || next==serial)return;
    Settings requested=channel->requested;MemoryBarrier();
    if(next==InterlockedCompareExchange(&channel->sequence,0,0)){value=Clamp(requested);serial=next;}
}
inline void PublishSettings(Channel* channel,const Settings& value,LONG serial){
    InterlockedIncrement(&channel->settingsSequence);channel->settings=value;MemoryBarrier();
    InterlockedIncrement(&channel->settingsSequence);InterlockedExchange(&channel->settingsApplied,serial);
}
inline bool ReadPublishedSettings(Channel* channel,Settings& value){
    LONG command=InterlockedCompareExchange(&channel->sequence,0,0);
    LONG revision=InterlockedCompareExchange(&channel->settingsSequence,0,0);
    if(!revision || (revision&1) || (command&1) || command!=InterlockedCompareExchange(&channel->settingsApplied,0,0))return false;
    Settings snapshot=channel->settings;MemoryBarrier();
    if(revision!=InterlockedCompareExchange(&channel->settingsSequence,0,0) || command!=InterlockedCompareExchange(&channel->sequence,0,0) || command!=InterlockedCompareExchange(&channel->settingsApplied,0,0))return false;
    value=snapshot;return true;
}
inline void Correct(float* rgb, Settings s) {
    s=Clamp(s); for(int i=0;i<3;i++) {
        float c=std::max(0.f,rgb[i]*std::exp2(s.exposure));
        c += s.shadows*.45f*std::pow(1.f-std::clamp(c,0.f,1.f),2.f);
        rgb[i]=(std::pow(std::max(0.f,c),1.f/s.gamma)-.5f)*s.contrast+.5f;
    }
    float l=rgb[0]*.2126f+rgb[1]*.7152f+rgb[2]*.0722f;
    for(int i=0;i<3;i++) rgb[i]=std::clamp(l+(rgb[i]-l)*s.saturation,0.f,1.f);
}
}
