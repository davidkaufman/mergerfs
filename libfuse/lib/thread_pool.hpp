#pragma once

#include "unbounded_queue.hpp"

#include <tuple>
#include <atomic>
#include <vector>
#include <thread>
#include <memory>
#include <future>
#include <utility>
#include <functional>
#include <type_traits>


class ThreadPool
{
public:
  explicit
  ThreadPool(std::size_t thread_count_ = 0)
    : _queues(thread_count_),
      _count(thread_count_)
  {
    if(thread_count_ == 0)
      thread_count_ = std::thread::hardware_concurrency();

    auto worker = [this](auto i)
    {
      while(true)
        {
          Proc f;

          for(std::size_t n = 0; n < (_count * K); ++n)
            {
              if(_queues[(i + n) % _count].try_pop(f))
                break;
            }

          if(!f && !_queues[i].pop(f))
            break;

          f();
        }
    };

    _threads.reserve(thread_count_);
    for(std::size_t i = 0; i < thread_count_; ++i)
      _threads.emplace_back(worker, i);
  }

  ~ThreadPool()
  {
    for(auto& queue : _queues)
      queue.unblock();
    for(auto& thread : _threads)
      thread.join();
  }

  template<typename Func, typename... Args>
  void
  enqueue_work(Func&&    f_,
               Args&&... args_)
  {
    auto i    = _index++;
    auto work = [p = std::forward<Func>(f_), t = std::make_tuple(std::forward<Args>(args_)...)]() { std::apply(p, t); };

    for(std::size_t n = 0; n < (_count * K); ++n)
      {
        if(_queues[(i + n) % _count].try_push(work))
          return;
      }

    _queues[i % _count].push(std::move(work));
  }

  template<typename Func, typename... Args>
  [[nodiscard]]
  auto
  enqueue_task(Func&&    f_,
               Args&&... args_) -> std::future<std::invoke_result_t<Func,Args...>>
  {
    using TaskReturnType = std::invoke_result_t<Func,Args...>;
    using TaskType       = std::packaged_task<TaskReturnType()>;

    auto i      = _index++;
    auto task   = std::make_shared<TaskType>(std::bind(std::forward<Func>(f_),std::forward<Args>(args_)...));
    auto work   = [=]() { (*task)(); };
    auto result = task->get_future();

    for(auto n = 0; n < (_count * K); ++n)
      {
        if(_queues[(i + n) % _count].try_push(work))
          return result;
      }

    _queues[i % _count].push(std::move(work));

    return result;
  }

private:
  using Proc   = std::function<void(void)>;
  using Queue  = UnboundedQueue<Proc>;
  using Queues = std::vector<Queue>;
  Queues _queues;

private:
  std::vector<std::thread> _threads;

private:
  const std::size_t _count;
  std::atomic_uint _index = 0;

  inline static const unsigned int K = 2;
};
