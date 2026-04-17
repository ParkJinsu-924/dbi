#pragma once

#include "Projectile.h"


class HomingProjectile : public Projectile
{
public:
	HomingProjectile(long long ownerGuid, GameObjectType ownerType,
	                 long long targetGuid,
	                 int32 damage, float speed, float lifetimeLimit, Zone* zone)
		: Projectile(ownerGuid, ownerType, damage, speed, lifetimeLimit, zone)
		, targetGuid_(targetGuid)
	{
	}

	long long GetTargetGuid() const { return targetGuid_; }

protected:
	void Step(float dt) override;
	void CheckHit() override {} // Step 안에서 적중 처리

private:
	long long targetGuid_;
};
