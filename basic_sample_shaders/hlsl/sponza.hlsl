cbuffer Scene : register(b0) {
    float4x4 currentViewProjection;
    float4x4 previousViewProjection;
    float4 jitter;
    float4 baseColor;
    float4 parameters;
};
Texture2D<float4> albedo : register(t0);
SamplerState textureSampler : register(s0);
struct VertexOutput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 current : TEXCOORD1;
    float4 previous : TEXCOORD2;
};
VertexOutput VSMain(float3 position : POSITION,float3 normal : NORMAL,float2 uv : TEXCOORD0) {
    VertexOutput o;
    o.current=mul(float4(position,1),currentViewProjection);
    o.previous=mul(float4(position,1),previousViewProjection);
    o.position=o.current;
    o.position.xy+=jitter.xy*o.position.w;
    o.normal=normal;o.uv=uv;
    return o;
}
struct PixelOutput { float4 color : SV_TARGET0; float2 velocity : SV_TARGET1; };
PixelOutput PSMain(VertexOutput i) {
    PixelOutput o;
    float4 tex=albedo.Sample(textureSampler,i.uv)*baseColor;
    clip(tex.a-parameters.x);
    float3 n=normalize(i.normal);
    float light=.35+.65*saturate(dot(n,normalize(float3(-.3,1,.4))));
    // Display-referred SDR color. The FSR dispatch declares NON_LINEAR_COLOR_SRGB.
    float3 litColor=tex.rgb*light;
    o.color=float4(lerp(12.92*litColor,1.055*pow(max(litColor,0),1.0/2.4)-.055,step(.0031308,litColor)),1);
    float2 now=i.current.xy/max(i.current.w,.00001);
    float2 old=i.previous.xy/max(i.previous.w,.00001);
    o.velocity=(old-now)*float2(.5,-.5);
    return o;
}
