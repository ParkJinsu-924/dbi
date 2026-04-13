#pragma once

#include "Network/Session.h"
#include "Network/SendBuffer.h"
#include "Utils/Synchronized.h"

class SessionManager
{
public:
	void Add(SessionPtr session);
	void Remove(SessionPtr session);
	void Broadcast(SendBufferChunkPtr chunk);
	int32 Count() const;
	void Clear();

private:
	Synchronized<std::set<SessionPtr>, std::shared_mutex> sessions_;
};
