#pragma once

#include "Network/Session.h"
#include "Network/SendBuffer.h"


class SessionManager
{
public:
	void Add(SessionPtr session);
	void Remove(SessionPtr session);
	void Broadcast(SendBufferChunkPtr chunk);
	int32 Count() const;
	void Clear();

private:
	mutable std::mutex mutex_;
	std::set<SessionPtr> sessions_;
};
