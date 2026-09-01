#pragma once
#include "BaseTransition.h"
#include <memory>

class SpriteInstance;

/// <summary>
/// シンプルなフェードイン/アウトトランジション
/// </summary>
class FadeTransition : public BaseTransition {
public:
	/// <summary>
	/// コンストラクタ / デストラクタ。
	///
	/// どちらも実装は .cpp 側に置く。fadeSprite_ が前方宣言のみの SpriteInstance を
	/// 指しているため、ヘッダで = default にすると FadeTransition を
	/// 「構築する」または「破棄する」翻訳単位すべてで完全型が要求される
	/// （can't delete an incomplete type になる）。
	/// </summary>
	FadeTransition();
	~FadeTransition() override;

	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth, float screenHeight) override;

	void Finalize() override;
	void Update() override;
	void Draw() override;
	void Start(std::function<void()> onSceneChange) override;

	TransitionType GetType() const override { return TransitionType::Fade; }
	std::string GetName() const override { return "Fade"; }

	void SetFadeDuration(float duration) { fadeDuration_ = duration; }

private:
	std::unique_ptr<SpriteInstance> fadeSprite_;
	float fadeDuration_ = 0.5f;
	float alpha_ = 0.0f;
};
