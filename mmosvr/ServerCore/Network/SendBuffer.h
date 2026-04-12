#pragma once

#include "Utils/Types.h"


class SendBufferChunk
{
public:
	explicit SendBufferChunk(int32 capacity = 4096);

	char* Buffer();
	int32 Capacity() const;
	void SetSize(int32 size);
	int32 Size() const;

private:
	std::vector<char> buffer_;
	int32 size_ = 0;
};

using SendBufferChunkPtr = std::shared_ptr<SendBufferChunk>;

class SendBuffer
{
public:
	void Push(SendBufferChunkPtr chunk);
	std::vector<SendBufferChunkPtr> PopAll();
	bool Empty() const;

private:
	mutable std::mutex mutex_;
	std::vector<SendBufferChunkPtr> pendingChunks_;
};
