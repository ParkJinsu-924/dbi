#include "pch.h"
#include "Server/SessionManager.h"
#include "Network/ServerSession.h"
#include "Utils/Metrics.h"


void SessionManager::AddClientSession(SessionPtr session)
{
	bool inserted = false;
	sessions_.Write([&](auto& set)
		{
			inserted = set.insert(std::move(session)).second;
		});
	if (inserted)
		ServerMetrics::currentSessions.fetch_add(1, std::memory_order_relaxed);
}

void SessionManager::RemoveClientSession(SessionPtr session)
{
	bool erased = false;
	sessions_.Write([&](auto& set)
		{
			erased = (set.erase(session) > 0);
		});
	if (erased)
		ServerMetrics::currentSessions.fetch_sub(1, std::memory_order_relaxed);
}

void SessionManager::BroadcastToClientSessions(SendBufferChunkPtr chunk)
{
	sessions_.Read([&](const auto& set)
		{
			for (auto& session : set)
			{
				if (session->IsConnected())
				{
					session->Send(chunk);
				}
			}
		});
}

int32 SessionManager::GetClientSessionsCount() const
{
	return sessions_.Read([](const auto& set)
		{
			return static_cast<int32>(set.size());
		});
}

void SessionManager::ClearClientSessions()
{
	sessions_.Write([](auto& set)
		{
			for (auto& session : set)
			{
				session->Disconnect();
			}
			set.clear();
		});
}

void SessionManager::SetServerSession(ServerType type, std::shared_ptr<ServerSession> session)
{
	serverSession_.WithLock([&](auto& set)
		{
			set[type] = std::move(session);
		});
}

void SessionManager::ClearServerSession(ServerType type)
{
	serverSession_.WithLock([&](auto& set)
		{
			set.erase(type);
		});
}

std::shared_ptr<ServerSession> SessionManager::GetServerSession(ServerType type) const
{
	return serverSession_.WithLock([&](auto& set) -> std::shared_ptr<ServerSession>
		{
			auto it = set.find(type);
			if (it == set.end())
				return nullptr;
			return it->second.lock();
		});
}
