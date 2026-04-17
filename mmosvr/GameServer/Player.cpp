#include "pch.h"
#include "Player.h"


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
