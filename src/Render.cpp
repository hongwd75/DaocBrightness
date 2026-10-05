#include "Render.h"
#include <d3dcompiler.h>
#include <d3d10effect.h>
#include <string>
namespace Brightness {
static const char* effect=R"(
float3 correct(float3 c) {
 c=max(0,c*exp2(values.y));
 c+=extra.x*.45*pow(1-saturate(c),2);
 c=(pow(max(c,0),1/values.x)-.5)*values.z+.5;
 float l=dot(c,float3(.2126,.7152,.0722));
 return saturate(lerp(l.xxx,c,values.w));
}
)";
static HRESULT Compile(const char* suffix,const char* profile,ID3DBlob** result) {
    std::string text=profile[0]=='p' && profile[3]=='3' ?
        "float4 values:register(c0); float4 extra:register(c1);" :
        "cbuffer Adjust:register(b0) {float4 values; float4 extra;};";
    text+=effect; text+=suffix;
    ID3DBlob* errors=nullptr;
    HRESULT hr=D3DCompile(text.data(),text.size(),"ColorAdjustment",nullptr,nullptr,"main",profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,result,&errors);
    Drop(errors); return hr;
}
void Render9::Reset(){ Drop(texture); Drop(shader); owner=nullptr; width=height=0; }
HRESULT Render9::Apply(IDirect3DDevice9* d,const Settings& input) {
    if(Default(input)) return S_OK;
    if(owner!=d) { Reset(); owner=d; }
    IDirect3DSurface9* back=nullptr; HRESULT hr=d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(FAILED(hr)) return hr;
    D3DSURFACE_DESC desc{}; back->GetDesc(&desc);
    if(width!=desc.Width || height!=desc.Height || format!=desc.Format) {
        Drop(texture); width=desc.Width; height=desc.Height; format=desc.Format;
    }
    if(!texture) hr=d->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&texture,nullptr);
    if(SUCCEEDED(hr) && !shader) {
        ID3DBlob* code=nullptr;
        hr=Compile("sampler2D scene:register(s0); float4 main(float2 uv:TEXCOORD0):COLOR0 { return float4(correct(tex2D(scene,uv).rgb),1); }","ps_3_0",&code);
        if(SUCCEEDED(hr)) hr=d->CreatePixelShader(static_cast<DWORD*>(code->GetBufferPointer()),&shader);
        Drop(code);
    }
    IDirect3DSurface9* copy=nullptr;
    if(SUCCEEDED(hr)) hr=texture->GetSurfaceLevel(0,&copy);
    if(SUCCEEDED(hr)) hr=d->StretchRect(back,nullptr,copy,nullptr,D3DTEXF_NONE);
    Drop(copy);
    if(FAILED(hr)) {Drop(back); return hr;}
    IDirect3DStateBlock9* state=nullptr;
    hr=d->CreateStateBlock(D3DSBT_ALL,&state);
    if(SUCCEEDED(hr)) hr=state->Capture();
    if(FAILED(hr)) {Drop(state); Drop(back); return hr;}
    IDirect3DSurface9* targets[4]{}; IDirect3DSurface9* oldDepth=nullptr;
    D3DCAPS9 caps{}; d->GetDeviceCaps(&caps);
    UINT slots=std::min<UINT>(caps.NumSimultaneousRTs,4);
    for(UINT i=0;i<slots;i++) d->GetRenderTarget(i,&targets[i]);
    d->GetDepthStencilSurface(&oldDepth);
    hr=d->BeginScene();
    if(SUCCEEDED(hr)) {
        d->SetDepthStencilSurface(nullptr);
        for(UINT i=1;i<slots;i++) d->SetRenderTarget(i,nullptr);
        d->SetRenderTarget(0,back);
        D3DVIEWPORT9 vp{0,0,width,height,0,1}; d->SetViewport(&vp);
        d->SetVertexShader(nullptr); d->SetPixelShader(shader);
        d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
        d->SetTexture(0,texture);
        Settings s=Clamp(input); float data[8]={s.gamma,s.exposure,s.contrast,s.saturation,s.shadows,0,0,0};
        d->SetPixelShaderConstantF(0,data,2);
        d->SetRenderState(D3DRS_ZENABLE,FALSE); d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE); d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        d->SetRenderState(D3DRS_STENCILENABLE,FALSE); d->SetRenderState(D3DRS_FOGENABLE,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15); d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID); d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT); d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE); d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP); d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        struct V{float x,y,z,w,u,v;};
        V v[4]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},
                {-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
        hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V));
        d->EndScene();
    }
    d->SetDepthStencilSurface(nullptr);
    for(UINT i=0;i<slots;i++){d->SetRenderTarget(i,targets[i]); Drop(targets[i]);}
    d->SetDepthStencilSurface(oldDepth); Drop(oldDepth);
    HRESULT restore=state->Apply(); Drop(state); Drop(back);
    return FAILED(restore)?restore:hr;
}
void Render10::Reset(){
    Drop(texture); Drop(view); Drop(vertex); Drop(pixel); Drop(constants); Drop(sampler);
    Drop(raster); Drop(blend); Drop(depth); Drop(owner); width=height=0;
}
HRESULT Render10::Apply(IDXGISwapChain* chain,const Settings& input) {
    if(Default(input)) return S_OK;
    ID3D10Device* d=nullptr;
    HRESULT hr=chain->GetDevice(__uuidof(ID3D10Device),reinterpret_cast<void**>(&d));
    if(FAILED(hr)) return hr;
    if(owner!=d) {Reset(); owner=d;} else Drop(d);
    d=owner;
    ID3D10Texture2D* back=nullptr;
    hr=chain->GetBuffer(0,__uuidof(ID3D10Texture2D),reinterpret_cast<void**>(&back));
    if(FAILED(hr)) return hr;
    D3D10_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
    if(width!=desc.Width || height!=desc.Height || format!=desc.Format) {
        Drop(view); Drop(texture); width=desc.Width; height=desc.Height; format=desc.Format;
    }
    if(!texture) {
        D3D10_TEXTURE2D_DESC td=desc; td.MipLevels=1; td.ArraySize=1; td.SampleDesc={1,0};
        td.Usage=D3D10_USAGE_DEFAULT; td.BindFlags=D3D10_BIND_SHADER_RESOURCE; td.CPUAccessFlags=td.MiscFlags=0;
        hr=d->CreateTexture2D(&td,nullptr,&texture);
        if(SUCCEEDED(hr)) hr=d->CreateShaderResourceView(texture,nullptr,&view);
    }
    if(SUCCEEDED(hr) && !vertex) {
        ID3DBlob* code=nullptr;
        hr=Compile("struct V {float4 p:SV_POSITION;float2 uv:TEXCOORD0;}; V main(uint id:SV_VertexID){V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}","vs_4_0",&code);
        if(SUCCEEDED(hr)) hr=d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),&vertex); Drop(code);
    }
    if(SUCCEEDED(hr) && !pixel) {
        ID3DBlob* code=nullptr;
        hr=Compile("Texture2D scene:register(t0); SamplerState smp:register(s0); float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_Target {return float4(correct(scene.Sample(smp,uv).rgb),1);}","ps_4_0",&code);
        if(SUCCEEDED(hr)) hr=d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),&pixel); Drop(code);
    }
    if(SUCCEEDED(hr) && !constants){D3D10_BUFFER_DESC bd{32,D3D10_USAGE_DEFAULT,D3D10_BIND_CONSTANT_BUFFER,0,0}; hr=d->CreateBuffer(&bd,nullptr,&constants);}
    if(SUCCEEDED(hr) && !sampler){D3D10_SAMPLER_DESC sd{}; sd.Filter=D3D10_FILTER_MIN_MAG_MIP_POINT; sd.AddressU=sd.AddressV=sd.AddressW=D3D10_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D10_FLOAT32_MAX; hr=d->CreateSamplerState(&sd,&sampler);}
    if(SUCCEEDED(hr) && !raster){D3D10_RASTERIZER_DESC rd{}; rd.FillMode=D3D10_FILL_SOLID; rd.CullMode=D3D10_CULL_NONE; rd.DepthClipEnable=TRUE; hr=d->CreateRasterizerState(&rd,&raster);}
    if(SUCCEEDED(hr) && !blend){D3D10_BLEND_DESC bd{}; bd.RenderTargetWriteMask[0]=D3D10_COLOR_WRITE_ENABLE_ALL; hr=d->CreateBlendState(&bd,&blend);}
    if(SUCCEEDED(hr) && !depth){D3D10_DEPTH_STENCIL_DESC dd{}; hr=d->CreateDepthStencilState(&dd,&depth);}
    ID3D10RenderTargetView* target=nullptr;
    if(SUCCEEDED(hr)) hr=d->CreateRenderTargetView(back,nullptr,&target);
    if(FAILED(hr)){Drop(target); Drop(back);return hr;}
    D3D10_STATE_BLOCK_MASK mask{}; D3D10StateBlockMaskEnableAll(&mask);
    ID3D10StateBlock* saved=nullptr; hr=D3D10CreateStateBlock(d,&mask,&saved);
    if(SUCCEEDED(hr)) hr=saved->Capture();
    if(FAILED(hr)){Drop(saved); Drop(target);Drop(back);return hr;}
    d->OMSetRenderTargets(0,nullptr,nullptr);
    ID3D10ShaderResourceView* empty=nullptr; d->PSSetShaderResources(0,1,&empty);
    if(desc.SampleDesc.Count>1) d->ResolveSubresource(texture,0,back,0,format); else d->CopyResource(texture,back);
    d->OMSetRenderTargets(1,&target,nullptr);
    D3D10_VIEWPORT vp{0,0,width,height,0,1}; d->RSSetViewports(1,&vp); d->RSSetState(raster);
    float factors[4]{}; d->OMSetBlendState(blend,factors,~0u); d->OMSetDepthStencilState(depth,0);
    d->IASetInputLayout(nullptr); d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d->VSSetShader(vertex); d->GSSetShader(nullptr); d->PSSetShader(pixel);
    Settings s=Clamp(input); float data[8]={s.gamma,s.exposure,s.contrast,s.saturation,s.shadows,0,0,0};
    d->UpdateSubresource(constants,0,nullptr,data,0,0); d->PSSetConstantBuffers(0,1,&constants);
    d->PSSetShaderResources(0,1,&view); d->PSSetSamplers(0,1,&sampler); d->Draw(3,0);
    d->PSSetShaderResources(0,1,&empty);
    hr=saved->Apply(); Drop(saved);Drop(target);Drop(back); return hr;
}
}
