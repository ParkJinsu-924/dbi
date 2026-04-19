#pragma once

#include "GameObject.h"
#include "game.pb.h"

class Zone;
class Unit;


class Projectile : public GameObject
{
public:
	Projectile(long long ownerGuid, GameObjectType ownerType,
	           int32 skillId, int32 damage, float speed, float lifetimeLimit, Zone& zone)
		: GameObject(GameObjectType::Projectile)
		, ownerGuid_(ownerGuid)
		, ownerType_(ownerType)
		, skillId_(skillId)
		, damage_(damage)
		, speed_(speed)
		, lifetimeLimit_(lifetimeLimit)
		, zone_(zone)
	{
	}

	void Update(float dt) override final;

	bool IsConsumed() const { return consumed_; }
	long long GetOwnerGuid() const { return ownerGuid_; }
	GameObjectType GetOwnerType() const { return ownerType_; }
	int32 GetSkillId() const { return skillId_; }
	int32 GetDamage() const { return damage_; }
	float GetSpeed() const { return speed_; }
	float GetLifetimeLimit() const { return lifetimeLimit_; }

protected:
	virtual void Step(float dt)  = 0;
	virtual void CheckHit()      = 0;

	// HIT: 데미지 적용 + S_SkillHit + S_UnitHp + S_ProjectileDestroy(HIT) 브로드캐스트, consumed_=true.
	void ApplyHit(Unit& target, const Proto::Vector2& hitPos);

	// EXPIRED / TARGET_LOST: S_ProjectileDestroy 브로드캐스트, consumed_=true.
	void DestroyWith(Proto::S_ProjectileDestroy_Reason reason);

	// owner.type vs other.type 단순 검사 — Monster↔Player 만 적대.
	bool IsHostile(const GameObject& other) const;

	long long      ownerGuid_;
	GameObjectType ownerType_;
	int32          skillId_;              // S_SkillHit.skill_id 에 실릴 값. 0 이면 미지정.
	int32          damage_;
	float          speed_;
	float          lifetime_      = 0.0f;
	float          lifetimeLimit_;        // 0 이하 = 비활성 (구 maxLifetime_ — max 매크로 충돌 회피)
	bool           consumed_      = false;
	Zone&          zone_;                 // 생성자에서 Zone::this 바인딩, 수명 동안 고정
};
