#include "pch.h"
#include "PlayerManager.h"
#include "Zone.h"


std::shared_ptr<Player> PlayerManager::CreatePlayerInZone(
	const std::string& name,
	Zone& zone,
	const std::shared_ptr<GameSession>& session)
{
	const int32 id = nextPlayerId_.fetch_add(1);
	auto player = std::make_shared<Player>(id, name, zone);   // zone_ ref 는 ctor 에서 바인딩

	zone.Add(player);                                         // zone 의 object map 에 등록
	if (session)
	{
		session->SetPlayerId(id);
		player->BindSession(session);
	}

	players_.Write([&](auto& m) { m[id] = player; });

	LOG_INFO(std::format("Player created: id={} name={} zone={}", id, name, zone.GetId()));
	return player;
}

void PlayerManager::RemovePlayer(int32 playerId)
{
	players_.Write([&](auto& m)
		{
			m.erase(playerId);
		});
}

std::shared_ptr<Player> PlayerManager::FindBySession(const std::shared_ptr<GameSession>& session) const
{
	if (session == nullptr)
		return nullptr;
	int32 playerId = session->GetPlayerId();
	if (playerId == 0)
		return nullptr;
	return FindPlayer(playerId);
}

std::shared_ptr<Player> PlayerManager::FindPlayer(int32 playerId) const
{
	return players_.Read([&](const auto& m) -> std::shared_ptr<Player>
		{
			auto it = m.find(playerId);
			if (it != m.end())
				return it->second;
			return nullptr;
		});
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetAllPlayers() const
{
	return players_.Read([](const auto& m)
		{
			std::vector<std::shared_ptr<Player>> result;
			result.reserve(m.size());
			for (const auto& [id, player] : m)
				result.push_back(player);
			return result;
		});
}
