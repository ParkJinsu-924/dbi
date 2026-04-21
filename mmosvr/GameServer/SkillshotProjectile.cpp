#include "pch.h"
#include "SkillshotProjectile.h"
#include "AttackTypes.h"
#include "Zone.h"
#include "Unit.h"
#include "Utils/MathUtil.h"


namespace
{
	constexpr float TARGET_HIT_RADIUS = 0.5f;  // Unit 가상 반경
}


void SkillshotProjectile::Step(const float dt)
{
	const float step = speed_ * dt;
	position_.set_x(position_.x() + dirX_ * step);
	position_.set_y(position_.y() + dirZ_ * step);
	traveled_ += step;

	if (traveled_ >= rangeLimit_)
	{
		DestroyWith(Proto::S_ProjectileDestroy_Reason_EXPIRED);
	}
}

void SkillshotProjectile::CheckHit()
{
	const float r = radius_ + TARGET_HIT_RADIUS;
	const float r2 = r * r;

	// HostileTypeOf 로 결정된 적대 진영 bucket 만 순회.
	// 첫 적중 시 ApplyHit 이 consumed_=true 로 세팅 → 이후 반복은 즉시 리턴.
	GetZone().ForEachOfType(HostileTypeOf(ownerType_),
		[&](long long /*guid*/, const std::shared_ptr<GameObject>& obj)
		{
			if (consumed_) return;
			const auto unit = std::static_pointer_cast<Unit>(obj);
			if (!unit->IsAlive()) return;
			if (DistanceToSq(*unit) < r2)
				ApplyHit(*unit, position_);
		});
}
