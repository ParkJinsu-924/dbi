#include "pch.h"
#include "Player.h"


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

void Player::Update(const float deltaTime)
{
	Unit::Update(deltaTime);

	if (!isMoving_)
		return;

	if (MoveToward(destination_, moveSpeed_, deltaTime))
		isMoving_ = false;
}