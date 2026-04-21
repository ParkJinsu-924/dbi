#pragma once

#include "Unit.h"
#include "Agent/MovementAgent.h"
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

	// --- Movement (LoL-style click-to-move) ---
	// 실제 상태는 MovementAgent 가 관리. 아래 메서드는 wrapper.
	// C_MoveCommand 수신 시 SetDestination, C_StopMove / C_UseSkill 시 ClearDestination.
	void SetDestination(const Proto::Vector2& dest) { Get<MovementAgent>().SetDestination(dest); }
	void ClearDestination()                          { Get<MovementAgent>().Clear(); }
	bool IsMoving() const                            { return Get<MovementAgent>().IsMoving(); }
	const Proto::Vector2& GetDestination() const     { return Get<MovementAgent>().GetDestination(); }

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
