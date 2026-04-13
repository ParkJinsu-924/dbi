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
	pendingChunks_.WithLock([&](auto& v)
		{
			v.push_back(std::move(chunk));
		});
}

std::vector<SendBufferChunkPtr> SendBuffer::PopAll()
{
	return pendingChunks_.WithLock([](auto& v)
		{
			std::vector<SendBufferChunkPtr> result;
			result.swap(v);
			return result;
		});
}

bool SendBuffer::Empty() const
{
	return pendingChunks_.WithLock([](const auto& v)
		{
			return v.empty();
		});
}
