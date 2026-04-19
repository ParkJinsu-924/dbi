#include "pch.h"
#include "Projectile.h"
#include "Zone.h"
#include "Unit.h"
#include "Player.h"
#include "Monster.h"
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

	Step(dt);
	if (consumed_)
		return;
	CheckHit();
}

void Projectile::ApplyHit(Unit& target, const Proto::Vector2& hitPos)
{
	const int32 hpBefore = target.GetHp();

	// caster Unit 조회 (OnHit Self-scope Effect 와 Aggro 용). 사라졌으면 nullptr.
	auto ownerObj = GetZone()->Find(ownerGuid_);
	Unit* casterUnit = nullptr;
	if (ownerObj &&
		(ownerObj->GetType() == GameObjectType::Player ||
		 ownerObj->GetType() == GameObjectType::Monster))
	{
		casterUnit = static_cast<Unit*>(ownerObj.get());
	}

	// OnHit 효과 전체 적용 (Damage / Slow / Stun / Heal 등).
	// Unit::TakeDamage 가 Invulnerable 체크를 포함하므로 여기서 별도 분기 불필요.
	SkillRuntime::ApplyEffects(skillId_, EffectTrigger::OnHit, casterUnit, &target);

	const int32 actualDmg = hpBefore - target.GetHp();   // 0 if invulnerable or no Damage effect

	GetZone()->Broadcast(PacketMaker::MakeSkillHit(
		ownerGuid_, target.GetGuid(), skillId_, actualDmg,
		GetPosition(), hitPos));

	if (actualDmg != 0)
		GetZone()->Broadcast(PacketMaker::MakeUnitHp(target));

	// --- Aggro accumulation ---
	// 플레이어 → 몬스터 피격 시 실제 적용 데미지를 aggro 로 누적.
	if (ownerType_ == GameObjectType::Player &&
		target.GetType() == GameObjectType::Monster &&
		actualDmg > 0)
	{
		auto& monster = static_cast<Monster&>(target);
		monster.AddAggro(ownerGuid_, static_cast<float>(actualDmg));
	}

	consumed_ = true;

	GetZone()->Broadcast(PacketMaker::MakeProjectileDestroy(GetGuid(), Proto::S_ProjectileDestroy_Reason_HIT));
}

void Projectile::DestroyWith(Proto::S_ProjectileDestroy_Reason reason)
{
	if (consumed_)
		return;
	consumed_ = true;

	GetZone()->Broadcast(PacketMaker::MakeProjectileDestroy(GetGuid(), reason));
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
