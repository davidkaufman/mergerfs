#pragma once

#include <mutex>
#include <queue>
#include <utility>
#include <condition_variable>


template<typename T>
class BoundedQueue
{
public:
  explicit
  BoundedQueue(std::size_t max_size_,
               bool        block_ = true)
    : _block(block),
      _max_size(max_size_)
  {
    if(_max_size == 0)
      _max_size = 1;
  }

  bool
  push(const T& item_)
  {
    {
      std::unique_lock guard(_queue_lock);

      _condition_push.wait(guard, [&]() { return _queue.size() < _max_size || !_block; });

      if(_queue.size() == _max_size)
        return false;

      _queue.push(item);
    }

    _condition_pop.notify_one();

    return true;
  }

  bool
  push(T&& item_)
  {
    {
      std::unique_lock guard(_queue_lock);

      _condition_push.wait(guard, [&]() { return _queue.size() < _max_size || !_block; });

      if(_queue.size() == _max_size)
        return false;

      _queue.push(std::move(item_));
    }
    _condition_pop.notify_one();
    return true;
  }

  template<typename... Args>
  bool
  emplace(Args&&... args_)
  {
    {
      std::unique_lock guard(_queue_lock);

      _condition_push.wait(guard, [&]() { return _queue.size() < _max_size || !_block; });

      if(_queue.size() == _max_size)
        return false;

      _queue.emplace(std::forward<Args>(args_)...);
    }

    _condition_pop.notify_one();

    return true;
  }

  bool
  pop(T& item_)
  {
    {
      std::unique_lock guard(_queue_lock);

      _condition_pop.wait(guard, [&]() { return !_queue.empty() || !_block; });
      if(_queue.empty())
        return false;

      item_ = std::move(_queue.front());

      _queue.pop();
    }

    _condition_push.notify_one();

    return true;
  }

  std::size_t
  size() const
  {
    std::scoped_lock guard(_queue_lock);

    return _queue.size();
  }

  std::size_t
  capacity() const
  {
    return _max_size;
  }

  bool
  empty() const
  {
    std::scoped_lock guard(_queue_lock);

    return _queue.empty();
  }

  bool
  full() const
  {
    std::scoped_lock lock(_queue_lock);

    return (_queue.size() == capacity());
  }

  void
  block()
  {
    std::scoped_lock guard(_queue_lock);
    _block = true;
  }

  void
  unblock()
  {
    {
      std::scoped_lock guard(_queue_lock);
      _block = false;
    }

    _condition_push.notify_all();
    _condition_pop.notify_all();
  }

  bool
  blocking() const
  {
    std::scoped_lock guard(_queue_lock);

    return _block;
  }

private:
  mutable std::mutex _queue_lock;

private:
  bool _block;
  std::queue<T> _queue;
  const std::size_t _max_size;
  std::condition_variable _condition_push;
  std::condition_variable _condition_pop;
};
