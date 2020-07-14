#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace vks
{
	class Thread
	{
	private:
		bool destroying = false;
		std::thread worker;
		std::queue<std::function<void()>> jobs;
		std::mutex mutex;
		std::condition_variable condition;

		// Loop through all remaining jobs
		void Loop()
		{
			while (true)
			{
				std::function<void()> job;
				{
					std::unique_lock<std::mutex> lock(mutex);
					condition.wait(lock, [this]
						{
							return !jobs.empty() || destroying;
						}
					);
					if (destroying)
						break;
					job = jobs.front();
				}
				job();

				{
					std::lock_guard<std::mutex> lock(mutex);
					jobs.pop();
					condition.notify_one();
				}
			}
		}

	public:
		Thread()
		{
			worker = std::thread(&Thread::Loop, this);
		}

		~Thread()
		{
			if (worker.joinable())
			{
				Wait();
				mutex.lock();
				destroying = true;
				condition.notify_one();
				mutex.unlock();
				worker.join();
			}
		}

		// Add a new job to the thread's queue
		void AddJob(std::function<void()> function)
		{
			std::lock_guard<std::mutex> lock(mutex);
			jobs.push(std::move(function));
			condition.notify_one();
		}

		// Wait until all work items have been finished
		void Wait()
		{
			std::unique_lock<std::mutex> lock(mutex);
			condition.wait(lock, [this]()
				{
					return jobs.empty();
				}
			);
		}
	};

	class ThreadPool
	{
	public: 
		std::vector<std::unique_ptr<Thread>> threads;

		// Sets the number of threads to be allocted in this pool
		void SetThreadCount(uint32_t count)
		{
			threads.clear();
			for (auto i = 0; i < count; i++)
				threads.push_back(std::make_unique<Thread>());
		}

		// Wait until all threads have finished their work items
		void Wait()
		{
			for (auto& thread : threads)
				thread->Wait();
		}
	};
}