// b0 に入るシーン共通データ。C++ 側の実体は Scene.h の SceneData。
// PMD / Gregory / 地面 と複数のシェーダから読むので、宣言はここ 1 箇所にまとめる
// （以前は各 hlsli が個別に宣言していて、Gregory 側の並びがずれていた）。
//
// float3 は 16 バイト境界をまたげないので eye と lightVec の間に 1 行空きができる。
// C++ 側はそこに詰め物を入れて合わせてある。
cbuffer SceneBuffer : register(b0)
{
    matrix view;
    matrix proj;
    matrix lightCamera; // ライトから見たビュー×プロジェクション
    float3 eye;
    float nearZ;
    float3 lightVec;    // 平行光線の向き（正規化済み）
    float farZ;
};
