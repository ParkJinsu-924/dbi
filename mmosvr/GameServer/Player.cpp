#include "pch.h"
#include "Player.h"
#include "Zone.h"
#include "ZoneManager.h"


Player::Player(int32 playerId, const std::string& name)
	: Unit(GameObjectType::Player, GetObjectGuidGenerator().Generate(), name)
	, playerId_(playerId)
{
	position_.set_x(0.0f);
	position_.set_y(0.0f);
	position_.set_z(0.0f);
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

bool Player::TryConsumeCooldown(const std::string& skillName, float cooldownSec)
{
	const float now = GetTimeManager().GetTotalTime();
	auto it = skillCooldowns_.find(skillName);
	if (it != skillCooldowns_.end() && now < it->second)
		return false;

	skillCooldowns_[skillName] = now + cooldownSec;
	return true;
}

Zone* Player::GetZone() const
{
	return GetZoneManager().GetZone(GetZoneId());
}

void Player::Update(const float deltaTime)
{
	if (!isMoving_)
		return;

	const float dx = destination_.x() - position_.x();
	const float dz = destination_.z() - position_.z();
	const float dist = std::sqrt(dx * dx + dz * dz);

	if (dist < 0.001f)
	{
		isMoving_ = false;
		return;
	}

	const float step = moveSpeed_ * deltaTime;
	if (step >= dist)
	{
		// 도착: destination 에 snap 하고 이동 종료
		position_.set_x(destination_.x());
		position_.set_z(destination_.z());
		isMoving_ = false;
		return;
	}

	const float nx = dx / dist;
	const float nz = dz / dist;
	position_.set_x(position_.x() + nx * step);
	position_.set_z(position_.z() + nz * step);
}