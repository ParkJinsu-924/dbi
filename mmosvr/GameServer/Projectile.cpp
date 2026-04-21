#include "pch.h"
#include "Projectile.h"
#include "Zone.h"
#include "Unit.h"
#include "PacketMaker.h"
#include "SkillRuntime.h"


void Projectile::Update(const float dt)
{
	if (consumed_)
		return;
	
	constexpr float MAX_LIFETIME = 40.0f;
	const float lifetimeLimit = lifetimeLimit_ > 0.0f ? lifetimeLimit_ : MAX_LIFETIME;

	lifetime_ += dt;
	if (lifetimeLimit > 0.0f && lifetime_ >= lifetimeLimit)
	{
		DestroyWith(Proto::S_ProjectileDestroy_Reason_EXPIRED);
		return;
	}

	Step(dt); // Move Projectile
	if (consumed_) // Projectile Move 로 인해서 소멸된 Projectile
		return;
	CheckHit();
}

void Projectile::ApplyHit(Unit& target, const Proto::Vector2& hitPos)
{
	// caster Unit 조회 (OnHit Self-scope Effect 및 aggro 속성용). 사라졌으면 nullptr.
	auto ownerObj = GetZone().Find(ownerGuid_);
	Unit* casterUnit = nullptr;
	if (ownerObj &&
		(ownerObj->GetType() == GameObjectType::Player ||
		 ownerObj->GetType() == GameObjectType::Monster))
	{
		casterUnit = static_cast<Unit*>(ownerObj.get());
	}

	// OnHit 효과 적용 + S_SkillHit 방송. S_UnitHp / aggro 누적은
	// Unit::TakeDamage 가 BuffAgent 경로로 자동 처리.
	SkillRuntime::ResolveHit(casterUnit, target, skillId_, GetPosition(), hitPos, GetZone());

	consumed_ = true;
	GetZone().Broadcast(PacketMaker::MakeProjectileDestroy(GetGuid(), Proto::S_ProjectileDestroy_Reason_HIT));
}

void Projectile::DestroyWith(Proto::S_ProjectileDestroy_Reason reason)
{
	if (consumed_)
		return;
	consumed_ = true;

	GetZone().Broadcast(PacketMaker::MakeProjectileDestroy(GetGuid(), reason));
}

bool Projectile::IsHostile(const GameObject& other) const
{
	if (other.GetGuid() == ownerGuid_)
		return false;

	const auto tt = other.GetType();
	if (ownerType_ == GameObjectType::Monster && tt == GameObjectType::Player)
		return true;
	if (ownerType_ == GameObjectType::Player && tt == GameObjectType::Monster)
		return true;
	return false;
}
