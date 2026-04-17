#include "pch.h"
#include "Network/IoContextPool.h"


IoContextPool::IoContextPool(int32 poolSize)
{
	if (poolSize <= 0)
		poolSize = static_cast<int32>(std::thread::hardware_concurrency());
	if (poolSize <= 0)
		poolSize = 2;

	ioContexts_.resize(poolSize);
	for (auto& entry : ioContexts_)
	{
		entry.context = std::make_unique<net::io_context>();
		entry.workGuard = std::make_unique<net::executor_work_guard<net::io_context::executor_type>>(
			entry.context->get_executor());
	}
}

IoContextPool::~IoContextPool()
{
	Stop();
}

void IoContextPool::Run()
{
	for (auto& entry : ioContexts_)
	{
		threads_.emplace_back([&ioc = *entry.context]()
			{
				ioc.run();
			});
	}
}

void IoContextPool::Stop()
{
	for (auto& entry : ioContexts_)
	{
		entry.workGuard.reset();
		entry.context->stop();
	}
	// std::jthread auto-joins on destruction, but we clear explicitly for reuse
	threads_.clear();
}

net::io_context& IoContextPool::GetNextIoContext()
{
	int32 index = nextIndex_.fetch_add(1) % static_cast<int32>(ioContexts_.size());
	return *ioContexts_[index].context;
}

