#pragma once

#include <concurrent_queue.h>
#include <functional>

class JobQueue
{
public:
	using Job = std::function<void()>;

	void Push(Job job)
	{
		queue_.push(std::move(job));
	}

	void Flush()
	{
		Job job;
		while (queue_.try_pop(job))
		{
			job();
		}
	}

	bool Empty() const
	{
		return queue_.empty();
	}

private:
	concurrency::concurrent_queue<Job> queue_;
};
