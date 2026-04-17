#include "pch.h"
#include "Projectile.h"
#include "Zone.h"
#include "Unit.h"
#include "Player.h"


void Projectile::Update(const float dt)
{
	if (consumed_)
		return;

	lifetime_ += dt;
	if (lifetimeLimit_ > 0.0f && lifetime_ >= lifetimeLimit_)
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
	target.TakeDamage(damage_);

	if (zone_)
	{
		Proto::S_ProjectileHit pkt;
		pkt.set_projectile_guid(GetGuid());
		pkt.set_target_guid(target.GetGuid());
		pkt.set_damage(damage_);
		*pkt.mutable_hit_pos() = hitPos;
		zone_->Broadcast(pkt);
	}

	if (target.GetType() == GameObjectType::Player)
	{
		auto& player = static_cast<Player&>(target);
		Proto::S_PlayerHp hpPkt;
		hpPkt.set_hp(player.GetHp());
		hpPkt.set_max_hp(player.GetMaxHp());
		player.Send(hpPkt);
	}

	consumed_ = true;

	if (zone_)
	{
		Proto::S_ProjectileDestroy destroyPkt;
		destroyPkt.set_projectile_guid(GetGuid());
		destroyPkt.set_reason(Proto::S_ProjectileDestroy_Reason_HIT);
		zone_->Broadcast(destroyPkt);
	}
}

void Projectile::DestroyWith(Proto::S_ProjectileDestroy_Reason reason)
{
	if (consumed_)
		return;
	consumed_ = true;

	if (zone_)
	{
		Proto::S_ProjectileDestroy pkt;
		pkt.set_projectile_guid(GetGuid());
		pkt.set_reason(reason);
		zone_->Broadcast(pkt);
	}
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
