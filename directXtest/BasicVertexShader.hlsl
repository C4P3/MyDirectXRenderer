#include "BasicShaderHeader.hlsli"
Output BasicVS( float4 pos : POSITION, float2 uv : TEXCOORD )
{
    Output output; // ピクセルシェーダーに渡す値
    output.svpos = pos;
    output.uv = uv;
	return output;
}