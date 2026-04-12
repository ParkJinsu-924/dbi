#pragma once

#include "Utils/Types.h"


class IoContextPool
{
public:
	explicit IoContextPool(int32 poolSize);
	~IoContextPool();

	void Run();
	void Stop();

	net::io_context& GetNextIoContext();
	int32 Size() const { return static_cast<int32>(ioContexts_.size()); }

private:
	struct IoContextEntry
	{
		std::unique_ptr<net::io_context> context;
		std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> workGuard;
	};

	std::vector<IoContextEntry> ioContexts_;
	std::vector<std::jthread> threads_;
	std::atomic<int32> nextIndex_{0};
};
