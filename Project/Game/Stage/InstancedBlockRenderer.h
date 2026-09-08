#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "Vector3.h"

class Camera;
class DirectXCore;
class Object3DManager;
class ModelInstance;

/// <summary>
/// 同一メッシュを多数、GPU インスタンシングで一括描画する軽量レンダラ。
///
/// StageGrid の床タイル（壊れない床＝Block / 壊れる床＝woodBlock）用。1 種類につき 1 インスタンス。
/// ルートシグネチャ・PS は既存の Object3D パス（Object3DManager / Object3dNoEnv.PS）を流用し、
/// VS だけ専用の Object3dInstanced.VS（インスタンスごとの変換行列を頂点ストリーム slot1 で受け取る）。
///
/// 使い方（GameScene の Object3D パス内、Object3DManager::DrawSetting / LightManager::BindLights の後）:
///   Initialize(...) → SetInstances(...) → 毎フレーム Update() → DrawModels パスで Draw()
/// </summary>
class InstancedBlockRenderer {
public:
	InstancedBlockRenderer();
	~InstancedBlockRenderer();

	/// <summary>PSO・インスタンスバッファ・カメラ CB を作り、モデルをロードする。
	/// object3DManager / dxCore が null、またはモデルが読めない場合は「無効」状態になり Draw は何もしない。</summary>
	/// <param name="baseRotation">全インスタンス共通の姿勢（オイラー角・ラジアン）。
	/// モデルの前後が逆にエクスポートされている等の補正に使う。</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, Camera* camera,
		const std::string& modelDirectory, const std::string& modelFileName,
		const Vector3& baseRotation = { 0.0f, 0.0f, 0.0f });
	void Finalize();

	/// <summary>描画するインスタンスの中心ワールド座標一覧と共通スケールを設定する。
	/// 破壊などで並びが変わったら呼び直す。</summary>
	void SetInstances(const std::vector<Vector3>& worldPositions, const Vector3& scale);

	/// <summary>カメラから各インスタンスの WVP を再計算して GPU へアップロードする。毎フレーム呼ぶ。</summary>
	void Update();

	/// <summary>1 ドローコール（サブメッシュ数ぶん）で全インスタンスを描画する。</summary>
	void Draw(DirectXCore* dxCore);

	bool IsValid() const { return valid_; }
	int  GetInstanceCount() const { return static_cast<int>(instanceCount_); }

private:
	// 1 インスタンス分の変換行列（HLSL 側の float4x4 ×3 と 1:1。TransformationMatrix と同レイアウト）。
	struct InstanceMatrix {
		float wvp[16];
		float world[16];
		float worldInverseTranspose[16];
	};
	static constexpr uint32_t kMaxInstances = 600; // 32x18 グリッド上限を賄える

	bool CreatePipeline();

	Object3DManager* object3DManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	Camera* camera_ = nullptr;
	ModelInstance* model_ = nullptr;
	bool valid_ = false;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
	InstanceMatrix* instanceData_ = nullptr; // instanceResource_ を Map したポインタ
	D3D12_VERTEX_BUFFER_VIEW instanceVbv_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	void* cameraData_ = nullptr; // CameraForGPU を Map したポインタ

	std::vector<Vector3> positions_;
	Vector3 scale_{ 1.0f, 1.0f, 1.0f };
	Vector3 baseRotation_{ 0.0f, 0.0f, 0.0f };
	uint32_t instanceCount_ = 0;
};
