#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <thread>
#include <list>
#include <queue>
#include <memory>
#include <condition_variable>
#include <future>

class ThreadPool
{
    std::list<std::thread> _threads;
    std::queue<std::function<void()>, std::list<std::function<void()> > > _tasks;
    std::shared_ptr<bool> _running;

    std::mutex _mutex;
    std::condition_variable _conditionVariable;

    static void threadLoop(
        std::shared_ptr<bool> running,
        std::queue<std::function<void()>, std::list<std::function<void()> > >& tasks,
        std::mutex& mutex,
        std::condition_variable& conditionVariable)
    {
        while (*running)
        {
            std::unique_lock<std::mutex> lock(mutex);

            if (!*running)
            {
                return;
            }

            if (tasks.size() > 0)
            {
                auto task = tasks.front();
                tasks.pop();
                lock.unlock();
                task();

                continue;
            }

            conditionVariable.wait(lock);

            if (!*running)
            {
                return;
            }

            if (tasks.size() > 0)
            {
                auto task = tasks.front();
                tasks.pop();
                lock.unlock();
                task();

                continue;
            }
        }
    }

    template<
        typename Function,
        typename ReturnType,
        typename ...Args,
        typename = typename std::enable_if<!std::is_same<ReturnType, void>::value>::type
    >
    static void callAndSetPromise(std::promise<ReturnType>* promise, Function task, Args... args)
    {
        promise->set_value(task(args...));
        delete promise;
    }

    template<
        typename Function,
        typename ReturnType,
        typename ...Args,
        typename = typename std::enable_if<std::is_same<ReturnType, void>::value>::type
    >
    static void callAndSetPromise(std::promise<void>* promise, Function task, Args... args)
    {
        task(args...);
        promise->set_value();
        delete promise;
    }

public:

    ThreadPool(size_t threadCount)
        : _running(new bool(true))
    {
        for (size_t i = 0; i<threadCount; i++)
        {
            _threads.emplace_back(
                std::thread(
                    threadLoop,
                    _running,
                    std::ref(_tasks),
                    std::ref(_mutex),
                    std::ref(_conditionVariable)
                )
            );
        }
    }

    ~ThreadPool()
    {
        *_running = false;

        std::unique_lock<std::mutex> lock(_mutex);

        _conditionVariable.notify_all();
        lock.unlock();

        for (auto& thread : _threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        _tasks = std::queue<std::function<void()>, std::list<std::function<void()> > >();
    }

    template<typename Function, typename ...Args>
    auto addTask(Function rawTask, Args... args) -> std::future<decltype(rawTask(args...))>
    {
        using ReturnType = decltype(rawTask(args...));

        std::promise<ReturnType> *promise = new std::promise<ReturnType>();
        std::future<ReturnType> future = promise->get_future();
        auto task = std::bind(callAndSetPromise<Function, ReturnType, Args...>, promise, rawTask, args...);

        std::unique_lock<std::mutex> lock(_mutex);

        _tasks.emplace(task);
        _conditionVariable.notify_one();

        return future;
    }

    void clearTasks()
    {
        std::unique_lock<std::mutex> lock(_mutex);

        _tasks = std::queue<std::function<void()>, std::list<std::function<void()> > >();
    }

    void detachThreads()
    {
        for (auto& thread : _threads)
        {
            if (thread.joinable())
            {
                thread.detach();
            }
        }
    }

    int getTaskCount()
    {
        std::unique_lock<std::mutex> lock(_mutex);

        return _tasks.size();
    }
};

#endif
