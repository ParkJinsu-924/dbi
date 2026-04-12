#include "pch.h"
#include "Network/RecvBuffer.h"


RecvBuffer::RecvBuffer(int32 bufferSize)
	: buffer_(bufferSize)
{
}

char* RecvBuffer::WritePos()
{
	return buffer_.data() + writePos_;
}

int32 RecvBuffer::FreeSize() const
{
	return static_cast<int32>(buffer_.size()) - writePos_;
}

void RecvBuffer::OnWrite(int32 bytesWritten)
{
	writePos_ += bytesWritten;
}

char* RecvBuffer::ReadPos()
{
	return buffer_.data() + readPos_;
}

int32 RecvBuffer::DataSize() const
{
	return writePos_ - readPos_;
}

void RecvBuffer::OnRead(int32 bytesRead)
{
	readPos_ += bytesRead;
}

void RecvBuffer::Compact()
{
	int32 dataSize = DataSize();
	if (dataSize == 0)
	{
		readPos_ = 0;
		writePos_ = 0;
	}
	else if (FreeSize() < static_cast<int32>(buffer_.size()) / 4)
	{
		std::memmove(buffer_.data(), buffer_.data() + readPos_, dataSize);
		readPos_ = 0;
		writePos_ = dataSize;
	}
}
