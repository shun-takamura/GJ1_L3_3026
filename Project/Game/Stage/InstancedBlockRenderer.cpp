#include "Stage/InstancedBlockRenderer.h"

#include <cstring>

#include <dxcapi.h>

#include "Camera.h"
#include "CameraForGPU.h"
#include "DirectXCore.h"
#include "Log.h"
#include "MathUtility.h"
#include "Matrix4x4.h"
#include "ModelInstance.h"
#include "ModelManager.h"
#include "Object3DManager.h"
#include "TextureManager.h"
#include "Transform.h"

namespace {
	const std::wstring kInstancedVsPath = L"Resources/Shaders/Object3D/Object3dInstanced.VS.hlsl";
	const std::wstring kNoEnvPsPath = L"Resources/Shaders/Object3D/Object3dNoEnv.PS.hlsl";
	const std::string  kWhiteTexPath = "Resources/Textures/white1x1.dds";

	void CopyMatrix(float dst[16], const Matrix4x4& m) {
		std::memcpy(dst, &m.m[0][0], sizeof(float) * 16);
	}
}

InstancedBlockRenderer::InstancedBlockRenderer() = default;
InstancedBlockRenderer::~InstancedBlockRenderer() = default;

void InstancedBlockRenderer::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, Camera* camera,
	const std::string& modelDirectory, const std::string& modelFileName, const Vector3& baseRotation) {
	object3DManager_ = object3DManager;
	dxCore_ = dxCore;
	camera_ = camera;
	baseRotation_ = baseRotation;
	valid_ = false;

	if (!object3DManager_ || !dxCore_) {
		Log("InstancedBlockRenderer: 描画コンテキスト未設定のため無効化します\n");
		return;
	}

	// モデルをロード（ModelManager がキャッシュする。キーはファイル名）。
	ModelManager::GetInstance()->LoadModel(modelDirectory, modelFileName);
	model_ = ModelManager::GetInstance()->FindModel(modelFileName);
	if (!model_) {
		Log("InstancedBlockRenderer: モデルが読めません: " + modelDirectory + "/" + modelFileName + "\n");
		return;
	}

	// テクスチャ無しマテリアル向けのフォールバック白テクスチャを確保しておく。
	TextureManager::GetInstance()->LoadTexture(kWhiteTexPath);

	if (!CreatePipeline()) {
		return;
	}

	// インスタンスバッファ（upload heap、常時 Map）。
	instanceResource_ = dxCore_->CreateBufferResource(sizeof(InstanceMatrix) * kMaxInstances);
	instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));
	instanceVbv_.BufferLocation = instanceResource_->GetGPUVirtualAddress();
	instanceVbv_.SizeInBytes = 0; // SetInstances で確定
	instanceVbv_.StrideInBytes = sizeof(InstanceMatrix);

	// カメラ CB（b2 = ルートパラメータ4）。
	cameraResource_ = dxCore_->CreateBufferResource(sizeof(CameraForGPU));
	cameraResource_->Map(0, nullptr, &cameraData_);
	*static_cast<CameraForGPU*>(cameraData_) = CameraForGPU{};

	valid_ = true;
}

bool InstancedBlockRenderer::CreatePipeline() {
	Microsoft::WRL::ComPtr<IDxcBlob> vs;
	Microsoft::WRL::ComPtr<IDxcBlob> ps;
	vs.Attach(dxCore_->LoadShaderBlob(kInstancedVsPath, L"vs_6_0"));
	ps.Attach(dxCore_->LoadShaderBlob(kNoEnvPsPath, L"ps_6_0"));
	if (!vs || !ps) {
		Log("InstancedBlockRenderer: シェーダの読み込みに失敗しました\n");
		return false;
	}

	// 入力レイアウト：slot0 = メッシュ頂点（Object3D と同じ4要素）、slot1 = インスタンス変換行列（float4 ×12）。
	D3D12_INPUT_ELEMENT_DESC elems[16] = {};
	elems[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	elems[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	elems[2] = { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	elems[3] = { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
	const char* instSemantics[3] = { "INSTWVP", "INSTWORLD", "INSTWIT" };
	int e = 4;
	for (int s = 0; s < 3; ++s) {
		for (UINT r = 0; r < 4; ++r) {
			elems[e++] = { instSemantics[s], r, DXGI_FORMAT_R32G32B32A32_FLOAT, 1,
				D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 };
		}
	}

	D3D12_INPUT_LAYOUT_DESC inputLayout{};
	inputLayout.pInputElementDescs = elems;
	inputLayout.NumElements = _countof(elems);

	// 既存 Object3D（kBlendModeNormal）と同じブレンド／ラスタライザ／深度。
	D3D12_BLEND_DESC blend{};
	blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

	D3D12_RASTERIZER_DESC rasterizer{};
	rasterizer.CullMode = D3D12_CULL_MODE_BACK;
	rasterizer.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_DEPTH_STENCIL_DESC depthStencil{};
	depthStencil.DepthEnable = TRUE;
	depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = object3DManager_->GetRootSignature(); // Object3D パスと共通
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	desc.InputLayout = inputLayout;
	desc.BlendState = blend;
	desc.RasterizerState = rasterizer;
	desc.DepthStencilState = depthStencil;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;

	HRESULT hr = dxCore_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState_));
	if (FAILED(hr)) {
		Log("InstancedBlockRenderer: PSO の生成に失敗しました\n");
		return false;
	}
	return true;
}

void InstancedBlockRenderer::Finalize() {
	pipelineState_.Reset();
	instanceResource_.Reset();
	cameraResource_.Reset();
	instanceData_ = nullptr;
	cameraData_ = nullptr;
	model_ = nullptr;
	valid_ = false;
}

void InstancedBlockRenderer::SetInstances(const std::vector<Vector3>& worldPositions, const Vector3& scale) {
	positions_ = worldPositions;
	scale_ = scale;
	instanceCount_ = static_cast<uint32_t>(positions_.size());
	if (instanceCount_ > kMaxInstances) {
		instanceCount_ = kMaxInstances;
	}
	instanceVbv_.SizeInBytes = instanceCount_ * sizeof(InstanceMatrix);
}

void InstancedBlockRenderer::Update() {
	if (!valid_ || !camera_ || !instanceData_) {
		return;
	}

	if (cameraData_) {
		static_cast<CameraForGPU*>(cameraData_)->worldPosition = camera_->GetTranslate();
	}

	const Matrix4x4 viewProjection = camera_->GetViewProjectionMatrix();
	const Matrix4x4 localMatrix = model_ ? model_->GetModelData().rootNode.localMatrix : Matrix4x4{};

	for (uint32_t i = 0; i < instanceCount_; ++i) {
		Transform t;
		t.scale = scale_;
		t.rotate = baseRotation_;
		t.translate = positions_[i];

		const Matrix4x4 world = Multiply(localMatrix, MakeAffineMatrix(t));
		const Matrix4x4 wvp = Multiply(world, viewProjection);
		const Matrix4x4 wit = Transpose(Inverse(world));

		CopyMatrix(instanceData_[i].wvp, wvp);
		CopyMatrix(instanceData_[i].world, world);
		CopyMatrix(instanceData_[i].worldInverseTranspose, wit);
	}
}

void InstancedBlockRenderer::Draw(DirectXCore* dxCore) {
	if (!valid_ || instanceCount_ == 0 || !model_ || !model_->IsGPUReady()) {
		return;
	}
	if (model_->GetSubmeshes().empty()) {
		return;
	}

	auto* cmd = dxCore->GetCommandList();
	// ルートシグネチャ・環境マップ／シャドウ／フォグ・ライトは呼び出し側（GameScene の Object3D パス）で
	// Object3DManager::DrawSetting / LightManager::BindLights 済み。ここは PSO と個別リソースのみ差し替える。
	cmd->SetPipelineState(pipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VERTEX_BUFFER_VIEW vbvs[2] = { model_->GetVertexBufferView(), instanceVbv_ };
	cmd->IASetVertexBuffers(0, 2, vbvs);
	D3D12_INDEX_BUFFER_VIEW ibv = model_->GetIndexBufferView();
	cmd->IASetIndexBuffer(&ibv);

	// カメラ CB（b2 = ルートパラメータ4）。
	cmd->SetGraphicsRootConstantBufferView(4, cameraResource_->GetGPUVirtualAddress());

	auto* tm = TextureManager::GetInstance();
	for (const auto& sm : model_->GetSubmeshes()) {
		if (sm.indexCount == 0) {
			continue;
		}
		// マテリアル CB（b0 = ルートパラメータ0）。
		cmd->SetGraphicsRootConstantBufferView(0, sm.materialResource->GetGPUVirtualAddress());

		// ベースカラー（t0 = ルートパラメータ2）。空なら白テクスチャで埋めて未バインドを避ける。
		const std::string& subTex = !sm.textureFilePath.empty() ? sm.textureFilePath : model_->GetTextureFilePath();
		const std::string& baseTex = subTex.empty() ? kWhiteTexPath : subTex;
		cmd->SetGraphicsRootDescriptorTable(2, tm->GetSrvHandleGPU(baseTex));

		// 法線マップ（t2 = ルートパラメータ10）。無ければベースカラーで埋める（PS は useNormalMap で判定）。
		const std::string& normalTex = !sm.normalMapFilePath.empty() ? sm.normalMapFilePath : baseTex;
		cmd->SetGraphicsRootDescriptorTable(10, tm->GetSrvHandleGPU(normalTex));

		cmd->DrawIndexedInstanced(sm.indexCount, instanceCount_, sm.indexStart, 0, 0);
	}
}
