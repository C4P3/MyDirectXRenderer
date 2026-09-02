cbuffer SceneBuffer : register(b0)
{
    matrix view;
    matrix proj;
    float3 eye;
};

cbuffer Transform : register(b2)
{
    matrix world;
};

struct Output
{
    float4 svpos : SV_POSITION;
    float3 normal : NORMAL;
};