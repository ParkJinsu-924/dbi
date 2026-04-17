#include "pch.h"
#include "SkillshotProjectile.h"
#include "Zone.h"
#include "Unit.h"


namespace
{
	constexpr float TARGET_HIT_RADIUS = 0.5f;  // Unit 가상 반경
}


void SkillshotProjectile::Step(const float dt)
{
	const float step = speed_ * dt;
	position_.set_x(position_.x() + dirX_ * step);
	position_.set_z(position_.z() + dirZ_ * step);
	traveled_ += step;

	if (traveled_ >= rangeLimit_)
	{
		DestroyWith(Proto::S_ProjectileDestroy_Reason_EXPIRED);
	}
}

void SkillshotProjectile::CheckHit()
{
	if (!zone_)
		return;

	const float r = radius_ + TARGET_HIT_RADIUS;
	const float r2 = r * r;

	auto units = zone_->GetObjectsByType<Unit>();
	for (const auto& unit : units)
	{
		if (consumed_)
			return;
		if (!IsHostile(*unit))
			continue;
		if (!unit->IsAlive())
			continue;

		const auto& tp = unit->GetPosition();
		const float dx = tp.x() - position_.x();
		const float dz = tp.z() - position_.z();
		if (dx * dx + dz * dz < r2)
		{
			ApplyHit(*unit, position_);
			return;  // 첫 적중 후 소멸
		}
	}
}
