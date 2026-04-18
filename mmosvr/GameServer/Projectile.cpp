#include "pch.h"
#include "Projectile.h"
#include "Zone.h"
#include "Unit.h"
#include "Player.h"


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

	Proto::S_ProjectileHit pkt;
	pkt.set_projectile_guid(GetGuid());
	pkt.set_target_guid(target.GetGuid());
	pkt.set_damage(damage_);
	*pkt.mutable_hit_pos() = hitPos;
	zone_.Broadcast(pkt);

	if (target.GetHp() != hpBefore &&   // only sync when HP actually changed
		target.GetType() == GameObjectType::Player)
	{
		Proto::S_PlayerHp hpPkt;
		hpPkt.set_hp(target.GetHp());
		hpPkt.set_max_hp(target.GetMaxHp());
		hpPkt.set_guid(target.GetGuid());
		zone_.Broadcast(hpPkt);
	}

	consumed_ = true;

	Proto::S_ProjectileDestroy destroyPkt;
	destroyPkt.set_projectile_guid(GetGuid());
	destroyPkt.set_reason(Proto::S_ProjectileDestroy_Reason_HIT);
	zone_.Broadcast(destroyPkt);
}

void Projectile::DestroyWith(Proto::S_ProjectileDestroy_Reason reason)
{
	if (consumed_)
		return;
	consumed_ = true;

	Proto::S_ProjectileDestroy pkt;
	pkt.set_projectile_guid(GetGuid());
	pkt.set_reason(reason);
	zone_.Broadcast(pkt);
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
