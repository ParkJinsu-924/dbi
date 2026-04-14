#pragma once

#include "common.pb.h"
#include <string>
#include <memory>

class GameSession;

class Player
{
public:
	Player(int32 playerId, const std::string& name);
	~Player() = default;

	Player(const Player&) = delete;
	Player& operator=(const Player&) = delete;
	Player(Player&&) = delete;
	Player& operator=(Player&&) = delete;

	// --- Identity (immutable) ---
	int32 GetPlayerId() const { return playerId_; }
	const std::string& GetName() const { return name_; }

	// --- Session Binding ---
	void BindSession(std::shared_ptr<GameSession> session);
	void UnbindSession();
	std::shared_ptr<GameSession> GetSession() const;
	bool IsOnline() const;

	// --- Transform ---
	const Proto::Vector3& GetPosition() const { return position_; }
	void SetPosition(const Proto::Vector3& pos) { position_ = pos; }
	float GetYaw() const { return yaw_; }
	void SetYaw(float yaw) { yaw_ = yaw; }

	// --- Zone ---
	int32 GetZoneId() const { return zoneId_; }
	void SetZoneId(int32 zoneId) { zoneId_ = zoneId; }

	// --- Game State ---
	int32 GetHp() const { return hp_; }
	void SetHp(int32 hp) { hp_ = hp; }
	int32 GetMaxHp() const { return maxHp_; }
	void SetMaxHp(int32 maxHp) { maxHp_ = maxHp; }
	int32 GetLevel() const { return level_; }
	void SetLevel(int32 level) { level_ = level; }
	bool IsAlive() const { return hp_ > 0; }

	// --- Network Helper ---
	template<typename T>
	void Send(const T& pkt);

private:
	const int32 playerId_;
	const std::string name_;
	std::weak_ptr<GameSession> session_;

	Proto::Vector3 position_;
	float yaw_ = 0.0f;
	int32 zoneId_ = 0;

	int32 hp_ = 100;
	int32 maxHp_ = 100;
	int32 level_ = 1;
};

// Send<T> requires full GameSession definition for PacketSession::Send<T>
#include "GameSession.h"

template<typename T>
void Player::Send(const T& pkt)
{
	if (auto session = session_.lock())
	{
		session->Send(pkt);
	}
}
