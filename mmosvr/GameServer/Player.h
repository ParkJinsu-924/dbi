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

	// --- Movement (LoL-style click-to-move) ---
	// C_MoveCommand 수신 시 destination 설정, Zone 틱마다 직선으로 접근.
	// 스킬 사용 시 ClearDestination 으로 즉시 멈춘다.
	void SetDestination(const Proto::Vector2& dest) { destination_ = dest; isMoving_ = true; }
	void ClearDestination() { isMoving_ = false; }
	bool IsMoving() const { return isMoving_; }
	float GetMoveSpeed() const { return moveSpeed_; }
	void SetMoveSpeed(float v) { moveSpeed_ = v; }
	const Proto::Vector2& GetDestination() const { return destination_; }

	void Update(float deltaTime) override;

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

	// Movement
	Proto::Vector2 destination_;
	bool isMoving_ = false;
	float moveSpeed_ = 5.0f;   // 월드 유닛/초. debug_tool config.MOVE_SPEED 와 동일값.
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
