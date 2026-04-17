#pragma once

#include "Utils/Types.h"


class RecvBuffer
{
public:
	explicit RecvBuffer(int32 bufferSize = 65536);

	char* WritePos();
	int32 FreeSize() const;
	void OnWrite(int32 bytesWritten);

	char* ReadPos();
	int32 DataSize() const;
	void OnRead(int32 bytesRead);

	void Compact();

private:
	std::vector<char> buffer_;
	int32 readPos_ = 0;
	int32 writePos_ = 0;
};
