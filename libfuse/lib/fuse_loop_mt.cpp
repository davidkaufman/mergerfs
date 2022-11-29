/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB.
*/

#include "thread_pool.hpp"

#include "fuse_i.h"
#include "fuse_kernel.h"
#include "fuse_lowlevel.h"
#include "fuse_misc.h"

#include "fuse_msgbuf.hpp"
#include "fuse_ll.hpp"

#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <cassert>
#include <vector>

/* Environment var controlling the thread stack size */
#define ENVNAME_THREAD_STACK "FUSE_THREAD_STACK"

struct fuse_worker_data_t
{
  struct fuse_session *se;
  sem_t finished;
  std::shared_ptr<ThreadPool> tp;
};

static
void*
fuse_do_work_sync(void *data)
{
  fuse_worker_data_t *wd = (fuse_worker_data_t*)data;
  fuse_session       *se = wd->se;

  while(!fuse_session_exited(se))
    {
      int res;
      fuse_msgbuf_t *msgbuf;

      msgbuf = msgbuf_alloc();

      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
      res = se->receive_buf(se,msgbuf);
      pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
      switch(res)
        {
        case 0:
          break;
        case -EINTR:
        case -EAGAIN:
        case -ENOENT:
          continue;
        default:
          if(res < 0)
            break;
        }

      se->process_buf(se,msgbuf);
      msgbuf_free(msgbuf);
    }

  fuse_session_exit(se);
  sem_post(&wd->finished);

  return NULL;
}

static
void*
fuse_do_work_async(void *data)
{
  fuse_worker_data_t          *wd = (fuse_worker_data_t*)data;
  fuse_session                *se = wd->se;
  std::shared_ptr<ThreadPool>  tp = wd->tp;

  while(!fuse_session_exited(se))
    {
      int res;
      fuse_msgbuf_t *msgbuf;

      msgbuf = msgbuf_alloc();

      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
      res = se->receive_buf(se,msgbuf);
      pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
      switch(res)
        {
        case 0:
          break;
        case -EINTR:
        case -EAGAIN:
        case -ENOENT:
          continue;
        default:
          if(res < 0)
            break;
        }

      tp->enqueue_work([se,msgbuf] {
        se->process_buf(se,msgbuf);
        msgbuf_free(msgbuf);
      });
    }

  fuse_session_exit(se);
  sem_post(&wd->finished);

  return NULL;
}

int
fuse_start_thread(pthread_t *thread_id,
                  void      *(*func)(void *),
                  void      *arg)
{
  int res;
  sigset_t oldset;
  sigset_t newset;

  /* Disallow signal reception in worker threads */
  sigfillset(&newset);
  pthread_sigmask(SIG_BLOCK,&newset,&oldset);
  res = pthread_create(thread_id,NULL,func,arg);
  pthread_sigmask(SIG_SETMASK,&oldset,NULL);

  if(res != 0)
    {
      fprintf(stderr,
              "fuse: error creating thread: %s\n",
              strerror(res));
      return -1;
    }

  return 0;
}

static
int
calculate_thread_count(const int raw_thread_count_)
{
  int thread_count;

  if(raw_thread_count_ == 0)
    thread_count = std::thread::hardware_concurrency();
  else if(raw_thread_count_ == -1)
    thread_count = std::thread::hardware_concurrency();
  else if(raw_thread_count_ < 0)
    thread_count = (std::thread::hardware_concurrency() / -raw_thread_count_);
  else if(raw_thread_count_ > 0)
    thread_count = raw_thread_count_;

  if(thread_count <= 0)
    thread_count = 1;

  return thread_count;
}

static
void
calculate_thread_counts(int *read_thread_count_,
                        int *process_thread_count_)
{
  *read_thread_count_    = ::calculate_thread_count(*read_thread_count_);
  *process_thread_count_ = ::calculate_thread_count(*process_thread_count_);
}

int
fuse_session_loop_mt(struct fuse_session *se_,
                     const int            raw_read_thread_count_,
                     const int            raw_process_thread_count_)
{
  int err;
  int read_thread_count;
  int process_thread_count;
  fuse_worker_data_t wd = {0};
  std::vector<pthread_t> threads;
  std::shared_ptr<ThreadPool> tp;

  read_thread_count    = raw_read_thread_count_;
  process_thread_count = raw_process_thread_count_;
  ::calculate_thread_counts(&read_thread_count,&process_thread_count);

  if(process_thread_count > 0)
    wd.tp = std::make_shared<ThreadPool>(process_thread_count);
  wd.se = se_;
  sem_init(&wd.finished,0,0);

  auto func = (wd.tp ? fuse_do_work_async : fuse_do_work_sync);

  err = 0;
  for(int i = 0; (i < read_thread_count) && !err; i++)
    {
      pthread_t thread_id;
      err = fuse_start_thread(&thread_id,func,&wd);
      assert(err == 0);
      threads.push_back(thread_id);
    }

  if(!err)
    {
      /* sem_wait() is interruptible */
      while(!fuse_session_exited(se_))
        sem_wait(&wd.finished);

      for(const auto &thread_id : threads)
        pthread_cancel(thread_id);

      for(const auto &thread_id : threads)
        pthread_join(thread_id,NULL);
    }

  sem_destroy(&wd.finished);

  return err;
}
