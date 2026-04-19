#include "pch.h"
#include "Projectile.h"
#include "Zone.h"
#include "Unit.h"
#include "Player.h"
#include "Monster.h"
#include "PacketMaker.h"


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

void Projectile::ApplyHit(Unit& target, const Proto::Vector3& hitPos)
{
	const auto hpBefore = target.GetHp();
	target.TakeDamage(damage_);

	zone_.Broadcast(PacketMaker::MakeProjectileHit(*this, target, hitPos));

	if (target.GetHp() != hpBefore)   // only sync when HP actually changed
	{
		// S_UnitHp 는 guid 기반이라 Player/Monster 공통으로 재사용 가능.
		// 몬스터도 방송해야 클라가 HP 바를 갱신한다.
		zone_.Broadcast(PacketMaker::MakeUnitHp(target));
	}

	// --- Aggro accumulation ---
	// 플레이어가 몬스터에게 데미지를 주면 피해량 만큼 해당 몬스터의 AggroTable 에 누적.
	// 실제 변화량(damage applied) 이 아닌 의도한 damage_ 를 쓴다 — overkill 에도 모든 기여가 반영.
	if (ownerType_ == GameObjectType::Player &&
		target.GetType() == GameObjectType::Monster)
	{
		auto& monster = static_cast<Monster&>(target);
		monster.AddAggro(ownerGuid_, static_cast<float>(damage_));
	}

	consumed_ = true;

	zone_.Broadcast(PacketMaker::MakeProjectileDestroy(GetGuid(), Proto::S_ProjectileDestroy_Reason_HIT));
}

void Projectile::DestroyWith(Proto::S_ProjectileDestroy_Reason reason)
{
	if (consumed_)
		return;
	consumed_ = true;

	zone_.Broadcast(PacketMaker::MakeProjectileDestroy(GetGuid(), reason));
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
