#include "pch.h"
#include "Player.h"


Player::Player(int32 playerId, const std::string& name)
	: playerId_(playerId)
	, name_(name)
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
