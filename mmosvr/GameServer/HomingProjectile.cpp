#include "pch.h"
#include "HomingProjectile.h"
#include "Zone.h"
#include "Unit.h"
#include <cmath>


namespace
{
	constexpr float HOMING_HIT_RADIUS = 1.0f;
}


void HomingProjectile::Step(const float dt)
{
	if (!zone_)
	{
		DestroyWith(Proto::S_ProjectileDestroy_Reason_TARGET_LOST);
		return;
	}

	auto target = zone_->FindAs<Unit>(targetGuid_);
	if (!target || !target->IsAlive())
	{
		DestroyWith(Proto::S_ProjectileDestroy_Reason_TARGET_LOST);
		return;
	}

	const auto& tp = target->GetPosition();
	const float dx = tp.x() - position_.x();
	const float dz = tp.z() - position_.z();
	const float dist = std::sqrt(dx * dx + dz * dz);

	if (dist < HOMING_HIT_RADIUS)
	{
		ApplyHit(*target, position_);
		return;
	}

	if (dist < 1e-4f)
		return;

	float step = speed_ * dt;
	if (step > dist)
		step = dist;

	position_.set_x(position_.x() + dx / dist * step);
	position_.set_z(position_.z() + dz / dist * step);
}
