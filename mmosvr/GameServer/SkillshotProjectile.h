#pragma once

#include "Projectile.h"


class SkillshotProjectile : public Projectile
{
public:
	SkillshotProjectile(long long ownerGuid, GameObjectType ownerType,
	                    float dirX, float dirZ,
	                    int32 damage, float speed, float radius, float range, Zone& zone)
		// lifetimeLimit = range / speed + buffer (안전장치)
		: Projectile(ownerGuid, ownerType, damage, speed,
		             (speed > 0.0f && range > 0.0f) ? range / speed + 0.5f : 5.0f,
		             zone)
		, dirX_(dirX)
		, dirZ_(dirZ)
		, radius_(radius)
		, rangeLimit_(range)
	{
	}

	float GetDirX()        const { return dirX_; }
	float GetDirZ()        const { return dirZ_; }
	float GetRadius()      const { return radius_; }
	float GetRangeLimit()  const { return rangeLimit_; }

protected:
	void Step(float dt) override;
	void CheckHit() override;

private:
	float dirX_;
	float dirZ_;
	float radius_;
	float rangeLimit_;     // 구 maxRange_ — max 매크로 충돌 회피
	float traveled_ = 0.0f;
};
