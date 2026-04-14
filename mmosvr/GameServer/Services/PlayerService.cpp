#include "pch.h"
#include "Services/PlayerService.h"


void PlayerService::Init()
{
	LOG_INFO("PlayerService initialized");
}

void PlayerService::Update(float /*deltaTime*/)
{
	// Tick-based game logic (future use)
}

void PlayerService::Shutdown()
{
	players_.Write([](auto& m)
		{
			m.clear();
		});
	LOG_INFO("PlayerService shutdown");
}

std::shared_ptr<Player> PlayerService::AddPlayer(const std::string& name)
{
	int32 id = nextPlayerId_.fetch_add(1);
	auto player = std::make_shared<Player>(id, name);

	players_.Write([&](auto& m)
		{
			m[id] = player;
		});

	LOG_INFO("Player added: id=" + std::to_string(id) + " name=" + name);
	return player;
}

void PlayerService::RemovePlayer(int32 playerId)
{
	players_.Write([&](auto& m)
		{
			m.erase(playerId);
		});
}

std::shared_ptr<Player> PlayerService::FindPlayer(int32 playerId) const
{
	return players_.Read([&](const auto& m) -> std::shared_ptr<Player>
		{
			auto it = m.find(playerId);
			if (it != m.end())
				return it->second;
			return nullptr;
		});
}

std::vector<std::shared_ptr<Player>> PlayerService::GetAllPlayers() const
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
