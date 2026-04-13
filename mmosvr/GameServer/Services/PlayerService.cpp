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

int32 PlayerService::AddPlayer(const std::string& name)
{
	int32 id = nextPlayerId_.fetch_add(1);

	PlayerData data;
	data.playerId = id;
	data.name = name;
	data.position.set_x(0.0f);
	data.position.set_y(0.0f);
	data.position.set_z(0.0f);

	players_.Write([&](auto& m)
	{
		m[id] = std::move(data);
	});

	LOG_INFO("Player added: id=" + std::to_string(id) + " name=" + name);
	return id;
}

void PlayerService::RemovePlayer(int32 playerId)
{
	players_.Write([&](auto& m)
	{
		m.erase(playerId);
	});
}

void PlayerService::MovePlayer(int32 playerId, const Proto::Vector3& newPos, float /*yaw*/)
{
	players_.Write([&](auto& m)
	{
		auto it = m.find(playerId);
		if (it != m.end())
		{
			it->second.position = newPos;
		}
	});
}

std::vector<PlayerData> PlayerService::GetAllPlayers() const
{
	return players_.Read([](const auto& m)
	{
		std::vector<PlayerData> result;
		result.reserve(m.size());
		for (const auto& [id, data] : m)
			result.push_back(data);
		return result;
	});
}
