#include "pch.h"
#include "Unit.h"

#include "Agent/BuffAgent.h"
#include "Agent/SkillCooldownAgent.h"
#include "Agent/AggroAgent.h"
#include "Agent/ForcedMoveAgent.h"
#include "Utils/MathUtil.h"
#include "Zone.h"
#include "PacketMaker.h"


Unit::Unit(GameObjectType type, Zone& zone, std::string name)
	: Unit(type, zone, GetObjectGuidGenerator().Generate(), std::move(name))
{
}

Unit::Unit(GameObjectType type, Zone& zone, long long guid, std::string name)
	: GameObject(type, zone, guid, std::move(name))
{
	AddAgent<BuffAgent>();
	AddAgent<SkillCooldownAgent>();
	AddAgent<ForcedMoveAgent>();   // 강제 이동(돌진/넉백 등) 수신 채널. 첫 사용처 도입 전엔 inactive.
}

void Unit::Update(const float deltaTime)
{
	for (auto* a : tickOrder_)
		a->Tick(deltaTime);
}

void Unit::TakeDamage(int32 amount, const Unit* attacker)
{
	if (Get<BuffAgent>().CanIgnoreDamage()) return;

	const int32 hpBefore = hp_;
	hp_ = (std::max)(0, hp_ - amount);
	const int32 actualDmg = hpBefore - hp_;
	if (actualDmg <= 0) return;

	GetZone().Broadcast(PacketMaker::MakeUnitHp(*this));

	// Player 가 Monster 공격 시 실제 적용 피해량만큼 aggro 자동 누적.
	if (attacker &&
		attacker->GetType() == GameObjectType::Player &&
		GetType() == GameObjectType::Monster)
	{
		Get<AggroAgent>().Add(attacker->GetGuid(), static_cast<float>(actualDmg));
	}
}

void Unit::Heal(int32 amount)
{
	const int32 hpBefore = hp_;
	hp_ = (std::min)(maxHp_, hp_ + amount);
	if (hp_ != hpBefore)
		GetZone().Broadcast(PacketMaker::MakeUnitHp(*this));
}

bool Unit::MoveToward(const Proto::Vector2& target, float deltaTime)
{
	if (!Get<BuffAgent>().CanMove()) return false;

	const float dx = target.x() - position_.x();
	const float dz = target.y() - position_.y();
	const float dist = MathUtil::Length2D(dx, dz);
	if (dist < 0.001f) return true;

	const float step = Get<BuffAgent>().EffectiveMoveSpeed(moveSpeed_) * deltaTime;
	if (step >= dist)
	{
		position_.set_x(target.x());
		position_.set_y(target.y());
		return true;
	}
	position_.set_x(position_.x() + (dx / dist) * step);
	position_.set_y(position_.y() + (dz / dist) * step);
	return false;
}
