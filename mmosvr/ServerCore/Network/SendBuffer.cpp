#include "pch.h"
#include "Network/SendBuffer.h"


// --- SendBufferChunk ---

SendBufferChunk::SendBufferChunk(int32 capacity)
	: buffer_(capacity)
{
}

char* SendBufferChunk::Buffer()
{
	return buffer_.data();
}

int32 SendBufferChunk::Capacity() const
{
	return static_cast<int32>(buffer_.size());
}

void SendBufferChunk::SetSize(int32 size)
{
	size_ = size;
}

int32 SendBufferChunk::Size() const
{
	return size_;
}

// --- SendBuffer ---

void SendBuffer::Push(SendBufferChunkPtr chunk)
{
	std::scoped_lock lock(mutex_);
	pendingChunks_.push_back(std::move(chunk));
}

std::vector<SendBufferChunkPtr> SendBuffer::PopAll()
{
	std::scoped_lock lock(mutex_);
	std::vector<SendBufferChunkPtr> result;
	result.swap(pendingChunks_);
	return result;
}

bool SendBuffer::Empty() const
{
	std::scoped_lock lock(mutex_);
	return pendingChunks_.empty();
}
