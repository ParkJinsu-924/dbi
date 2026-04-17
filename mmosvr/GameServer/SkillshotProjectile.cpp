#include "pch.h"
#include "SkillshotProjectile.h"
#include "Zone.h"
#include "Unit.h"
#include "Player.h"
#include "Monster.h"


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
	const float r = radius_ + TARGET_HIT_RADIUS;
	const float r2 = r * r;

	// owner type 에 따라 적대 진영 컨테이너만 직접 조회.
	auto checkUnits = [&](auto&& candidates)
		{
			for (const auto& unit : candidates)
			{
				if (consumed_) return;
				if (!unit->IsAlive()) continue;

				const auto& tp = unit->GetPosition();
				const float dx = tp.x() - position_.x();
				const float dz = tp.z() - position_.z();
				if (dx * dx + dz * dz < r2)
				{
					ApplyHit(*unit, position_);
					return;  // 첫 적중 후 소멸
				}
			}
		};

	if (ownerType_ == GameObjectType::Monster)
		checkUnits(zone_.GetObjectsByType<Player>());
	else if (ownerType_ == GameObjectType::Player)
		checkUnits(zone_.GetObjectsByType<Monster>());
}
