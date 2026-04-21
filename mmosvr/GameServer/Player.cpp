#include "pch.h"
#include "Player.h"
#include "Agent/BuffAgent.h"
#include "Utils/MathUtil.h"


Player::Player(int32 playerId, const std::string& name, Zone& zone)
	: Unit(GameObjectType::Player, zone, GetObjectGuidGenerator().Generate(), name)
	, playerId_(playerId)
{
	position_.set_x(0.0f);
	position_.set_y(0.0f);
}

void Player::BindSession(std::shared_ptr<GameSession> session)
{
	session_ = session;
	LOG_INFO("Player " + std::to_string(playerId_) + " bound to session");
}

void Player::UnbindSession()
{
	session_.reset();
	LOG_INFO("Player " + std::to_string(playerId_) + " unbound from session");
}

std::shared_ptr<GameSession> Player::GetSession() const
{
	return session_.lock();
}

bool Player::IsOnline() const
{
	auto s = session_.lock();
	return s && s->IsConnected();
}

bool Player::TryConsumeCooldown(const int32 skillId, const float cooldownSec)
{
	const float now = GetTimeManager().GetTotalTime();
	auto it = skillCooldowns_.find(skillId);
	if (it != skillCooldowns_.end() && now < it->second)
		return false;

	skillCooldowns_[skillId] = now + cooldownSec;
	return true;
}

void Player::Update(const float deltaTime)
{
	Unit::Update(deltaTime);

	if (!isMoving_ || !Get<BuffAgent>().CanMove())
		return;

	const float dx = destination_.x() - position_.x();
	const float dz = destination_.y() - position_.y();
	const float dist = MathUtil::Length2D(dx, dz);

	if (dist < 0.001f)
	{
		isMoving_ = false;
		return;
	}

	const float step = Get<BuffAgent>().EffectiveMoveSpeed(moveSpeed_) * deltaTime;
	if (step >= dist)
	{
		position_.set_x(destination_.x());
		position_.set_y(destination_.y());
		isMoving_ = false;
		return;
	}

	const float nx = dx / dist;
	const float nz = dz / dist;
	position_.set_x(position_.x() + nx * step);
	position_.set_y(position_.y() + nz * step);
}