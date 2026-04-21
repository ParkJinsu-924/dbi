#include "pch.h"
#include "HomingProjectile.h"
#include "Zone.h"
#include "Unit.h"
#include "Utils/MathUtil.h"


namespace
{
	constexpr float HOMING_HIT_RADIUS = 1.0f;
}


void HomingProjectile::Step(const float dt)
{
	auto target = GetZone().FindAs<Unit>(targetGuid_);
	if (!target || !target->IsAlive())
	{
		DestroyWith(Proto::S_ProjectileDestroy_Reason_TARGET_LOST);
		return;
	}

	const auto& tp = target->GetPosition();
	const float dx = tp.x() - position_.x();
	const float dz = tp.y() - position_.y();
	const float dist = MathUtil::Length2D(dx, dz);

	if (dist < 0.001f) return; // 타겟 위치 도달. 적중은 CheckHit 에서 처리.

	float step = speed_ * dt;
	if (step > dist)
		step = dist;

	position_.set_x(position_.x() + dx / dist * step);
	position_.set_y(position_.y() + dz / dist * step);
}

void HomingProjectile::CheckHit()
{
	// Step 에서 TARGET_LOST 로 소멸되면 Projectile::Update 가 CheckHit 호출을 스킵.
	// 도달 시점에도 일관성을 위해 target 재조회 + 생존 확인.
	auto target = GetZone().FindAs<Unit>(targetGuid_);
	if (!target || !target->IsAlive()) return;

	constexpr float r2 = HOMING_HIT_RADIUS * HOMING_HIT_RADIUS;
	if (DistanceToSq(*target) < r2)
		ApplyHit(*target, position_);
}
