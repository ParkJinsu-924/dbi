#pragma once

#include "Player.h"
#include "GameSession.h"
#include "Utils/Synchronized.h"
#include "Utils/TSingleton.h"
#include <unordered_map>
#include <atomic>
#include <string>

class Zone;


#define GetPlayerManager() PlayerManager::Instance()

class PlayerManager : public TSingleton<PlayerManager>
{
public:
	// Player 를 완성된 상태(zone + session 모두 바인딩)로 생성한다.
	// zone 은 레퍼런스라 null 불가 — 호출측이 GetZone 성공을 선행 보장.
	// 외부에 노출되는 모든 시점에 GetZone()/GetSession() 이 유효하다.
	std::shared_ptr<Player> CreatePlayerInZone(
		const std::string& name,
		Zone& zone,
		const std::shared_ptr<GameSession>& session);

	void RemovePlayer(int32 playerId);
	std::shared_ptr<Player> FindPlayer(int32 playerId) const;
	std::shared_ptr<Player> FindBySession(const std::shared_ptr<GameSession>& session) const;
	std::vector<std::shared_ptr<Player>> GetAllPlayers() const;

private:
	Synchronized<std::unordered_map<int32, std::shared_ptr<Player>>, std::shared_mutex> players_;
	std::atomic<int32> nextPlayerId_{1};
};
