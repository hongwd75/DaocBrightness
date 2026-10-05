#pragma once
#include "Shared.h"
#include <d3d9.h>
#include <d3d10.h>
#include <dxgi.h>
namespace Brightness {
template<class T> inline void Drop(T*& p) { if(p) { p->Release(); p=nullptr; } }
class Render9 {
    IDirect3DDevice9* owner=nullptr;
    IDirect3DTexture9* texture=nullptr;
    IDirect3DPixelShader9* shader=nullptr;
    UINT width=0,height=0; D3DFORMAT format=D3DFMT_UNKNOWN;
public:
    ~Render9(){Reset();}
    void Reset();
    HRESULT Apply(IDirect3DDevice9* device, const Settings& settings);
};
class Render10 {
    ID3D10Device* owner=nullptr;
    ID3D10Texture2D* texture=nullptr;
    ID3D10ShaderResourceView* view=nullptr;
    ID3D10VertexShader* vertex=nullptr;
    ID3D10PixelShader* pixel=nullptr;
    ID3D10Buffer* constants=nullptr;
    ID3D10SamplerState* sampler=nullptr;
    ID3D10RasterizerState* raster=nullptr;
    ID3D10BlendState* blend=nullptr;
    ID3D10DepthStencilState* depth=nullptr;
    UINT width=0,height=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
public:
    ~Render10(){Reset();}
    void Reset();
    HRESULT Apply(IDXGISwapChain* chain,const Settings& settings);
};
}
