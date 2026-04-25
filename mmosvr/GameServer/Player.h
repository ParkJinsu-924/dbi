#pragma once

#include "Unit.h"
#include <memory>
#include <string>

class GameSession;


class Player : public Unit
{
public:
	Player(int32 playerId, const std::string& name, Zone& zone);
	~Player() override = default;

	Player(const Player&) = delete;
	Player& operator=(const Player&) = delete;
	Player(Player&&) = delete;
	Player& operator=(Player&&) = delete;

	// --- Identity ---
	int32 GetPlayerId() const { return playerId_; }

	// --- Session Binding ---
	void BindSession(std::shared_ptr<GameSession> session);
	void UnbindSession();
	std::shared_ptr<GameSession> GetSession() const;
	bool IsOnline() const;

	// --- Transform (yaw only; position/zoneId are in Unit/GameObject) ---
	float GetYaw() const { return yaw_; }
	void SetYaw(float yaw) { yaw_ = yaw; }

	// 이동은 클라 권위 — C_PlayerMove 가 SetPosition 으로 직접 갱신한다.
	// 서버 권위 강제 이동(돌진/넉백 등)이 도입되면 ForcedMoveAgent 로 처리.

	// --- Game State (hp/maxHp/IsAlive are in Unit) ---
	int32 GetLevel() const { return level_; }
	void SetLevel(int32 level) { level_ = level; }

	// --- Network Helper ---
	template<typename T>
	void Send(const T& pkt);

private:
	const int32 playerId_;
	std::weak_ptr<GameSession> session_;

	float yaw_ = 0.0f;
	int32 level_ = 1;
};

// Send<T> requires full GameSession definition for PacketSession::Send<T>
#include "GameSession.h"

template<typename T>
void Player::Send(const T& pkt)
{
	if (const auto session = session_.lock())
	{
		session->Send(pkt);
	}
}
