// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#include "src/vm/scheduler.h"

#include <stdio.h>

#include "src/shared/flags.h"

#include "src/vm/interpreter.h"
#include "src/vm/port.h"
#include "src/vm/process.h"
#include "src/vm/process_queue.h"
#include "src/vm/thread.h"

namespace fletch {

ThreadState* const kEmptyThreadState = reinterpret_cast<ThreadState*>(1);

Scheduler::Scheduler()
    : max_threads_(Platform::GetNumberOfHardwareThreads()),
      thread_pool_(max_threads_),
      preempt_monitor_(Platform::CreateMonitor()),
      processes_(0),
      sleeping_threads_(0),
      thread_count_(0),
      idle_threads_(kEmptyThreadState),
      threads_(new std::atomic<ThreadState*>[max_threads_]),
      startup_queue_(new ProcessQueue()),
      pause_monitor_(Platform::CreateMonitor()),
      pause_(false),
      current_processes_(new std::atomic<Process*>[max_threads_]) {
  for (int i = 0; i < max_threads_; i++) {
    threads_[i] = NULL;
    current_processes_[i] = NULL;
  }
}

Scheduler::~Scheduler() {
  delete preempt_monitor_;
  delete pause_monitor_;
  delete[] current_processes_;
  delete[] threads_;
  delete startup_queue_;
}

void Scheduler::ScheduleProgram(Program* program) {
  program->set_scheduler(this);
}

bool Scheduler::StopProgram(Program* program) {
  ASSERT(program->scheduler() == this);
  pause_monitor_->Lock();

  if (stopped_processes_map_.find(program) != stopped_processes_map_.end()) {
    pause_monitor_->Unlock();
    return false;
  }

  pause_ = true;

  NotifyAllThreads();

  ProcessList& list = stopped_processes_map_[program];

  while (true) {
    int count = 0;
    // Preempt running processes, only if it was possibly to 'take' the current
    // process. This makes sure we don't preempt while deleting. Loop to
    // ensure we continue to preempt until all threads are sleeping.
    for (int i = 0; i < max_threads_; i++) {
      if (threads_[i] != NULL) count++;
      PreemptThreadProcess(i);
    }
    if (count == sleeping_threads_) break;
    pause_monitor_->Wait();
  }

  Process* to_enqueue = NULL;

  while (true) {
    Process* process = NULL;
    // All processes dequeued are marked as Running.
    if (!TryDequeueFromAnyThread(&process)) continue;  // Retry.
    if (process == NULL) break;
    if (process->program() == program) {
      process->set_next(list.head);
      list.head = process;
    } else {
      process->set_next(to_enqueue);
      to_enqueue = process;
    }
  }

  while (to_enqueue != NULL) {
    to_enqueue->ChangeState(Process::kRunning, Process::kReady);
    EnqueueOnAnyThread(to_enqueue);
    Process* next = to_enqueue->next();
    to_enqueue->set_next(NULL);
    to_enqueue = next;
  }

  pause_ = false;
  pause_monitor_->Unlock();
  NotifyAllThreads();

  return true;
}

void Scheduler::ResumeProgram(Program* program) {
  ASSERT(program->scheduler() == this);
  pause_monitor_->Lock();

  ASSERT(stopped_processes_map_.find(program) != stopped_processes_map_.end());
  ProcessList& list = stopped_processes_map_[program];

  Process* process = list.head;
  while (process != NULL) {
    Process* next = process->next();
    process->set_next(NULL);
    process->ChangeState(Process::kRunning, Process::kReady);
    EnqueueOnAnyThread(process);
    process = next;
  }

  stopped_processes_map_.erase(program);

  pause_monitor_->Unlock();
  NotifyAllThreads();
}

void Scheduler::VisitProcesses(Program* program, ProcessVisitor* visitor) {
  ASSERT(program->scheduler() == this);
  pause_monitor_->Lock();

  ASSERT(stopped_processes_map_.find(program) != stopped_processes_map_.end());
  ProcessList& list = stopped_processes_map_[program];

  Process* process = list.head;
  while (process != NULL) {
    visitor->VisitProcess(process);
    process = process->next();
  }

  pause_monitor_->Unlock();
}

void Scheduler::EnqueueProcess(Process* process, ThreadState* thread_state) {
  ++processes_;
  if (!process->ChangeState(Process::kSleeping, Process::kReady)) UNREACHABLE();
  EnqueueProcessAndNotifyThreads(thread_state, process);
}

void Scheduler::ResumeProcess(Process* process) {
  if (!process->ChangeState(Process::kSleeping, Process::kReady)) return;
  EnqueueOnAnyThread(process, 0);
}

bool Scheduler::RunProcessOnCurrentThread(Process* process, Port* port) {
  ASSERT(port->IsLocked());
  if (!process->ChangeState(Process::kSleeping, Process::kRunning)) {
    port->Unlock();
    return false;
  }
  port->Unlock();
  // TODO(ajohnsen): This thread_state is only used for lookup cache. Consider
  // having a pool of 'queue-less' thread states that can be reused.
  ThreadState thread_state;
  process = InterpretProcess(process, &thread_state);
  if (process != NULL) EnqueueOnAnyThread(process);
  ASSERT(thread_state.queue()->is_empty());
  return true;
}

bool Scheduler::Run() {
  // Start initial thread.
  while (!thread_pool_.TryStartThread(RunThread, this, 1)) { }
  int thread_index = 0;
  while (true) {
    preempt_monitor_->Lock();
    // If we are done, bail out.
    if (processes_ == 0) {
      preempt_monitor_->Unlock();
      break;
    }
    int milliseconds = GetPreemptInterval();
    preempt_monitor_->Wait(milliseconds);
    preempt_monitor_->Unlock();

    // Clamp the thread_index to the number of current threads.
    if (thread_index >= thread_count_) thread_index = 0;
    PreemptThreadProcess(thread_index);
    thread_index++;
  }
  thread_pool_.JoinAll();
  return true;
}

void Scheduler::PreemptThreadProcess(int thread_id) {
  Process* process = current_processes_[thread_id];
  if (process != NULL) {
    if (current_processes_[thread_id].compare_exchange_strong(process, NULL)) {
      process->Preempt();
      current_processes_[thread_id] = process;
    }
  }
}

int Scheduler::GetPreemptInterval() {
  // Wait between 1 and 100 ms.
  int current_threads = Utils::Maximum<int>(1, thread_count_);
  return Utils::Maximum(1, 100 / current_threads);
}

void Scheduler::EnqueueProcessAndNotifyThreads(ThreadState* thread_state,
                                               Process* process) {
  ASSERT(process != NULL);
  int thread_id = 0;
  if (thread_state != NULL) {
    thread_id = thread_state->thread_id();
  } else if (thread_count_ == 0) {
    bool was_empty;
    while (!startup_queue_->TryEnqueue(process, &was_empty)) { }
    return;
  }

  // If we were able to enqueue on an idle thread, no need to spawn a new one.
  if (EnqueueOnAnyThread(process, thread_id + 1)) return;
  // Start a worker thread, if less than [processes_] threads are running.
  while (!thread_pool_.TryStartThread(RunThread, this, processes_)) { }
}

void Scheduler::PushIdleThread(ThreadState* thread_state) {
  // Add thread_state to idle_threads_, if it is not already in it.
  ThreadState* idle_threads = idle_threads_;
  if (idle_threads == thread_state ||
      thread_state->next_idle_thread() != NULL) {
    return;
  }
  while (true) {
    thread_state->set_next_idle_thread(idle_threads);
    if (idle_threads_.compare_exchange_weak(idle_threads, thread_state)) {
      break;
    }
  }
}

ThreadState* Scheduler::PopIdleThread() {
  ThreadState* idle_threads = idle_threads_;
  while (idle_threads != kEmptyThreadState) {
    ThreadState* next = idle_threads->next_idle_thread();
    if (idle_threads_.compare_exchange_weak(idle_threads, next)) {
      idle_threads->set_next_idle_thread(NULL);
      return idle_threads;
    }
  }
  return NULL;
}

void Scheduler::RunInThread() {
  ThreadState* thread_state = new ThreadState();
  ThreadEnter(thread_state);
  while (true) {
    thread_state->idle_monitor()->Lock();
    while (thread_state->queue()->is_empty() &&
           startup_queue_->is_empty() &&
           !pause_ &&
           processes_ > 0) {
      PushIdleThread(thread_state);
      // The thread is becoming idle.
      thread_state->idle_monitor()->Wait();
      // At this point the thread_state may still be in idle_threads_. That's
      // okay, as it will just be ignored later on.
    }
    thread_state->idle_monitor()->Unlock();
    if (processes_ == 0) {
      preempt_monitor_->Lock();
      preempt_monitor_->Notify();
      preempt_monitor_->Unlock();
      break;
    } else if (pause_) {
      thread_state->cache()->Clear();
      // Take lock to be sure StopProgram is waiting.
      pause_monitor_->Lock();
      sleeping_threads_++;
      pause_monitor_->Notify();
      pause_monitor_->Unlock();
      thread_state->idle_monitor()->Lock();
      while (pause_) {
        thread_state->idle_monitor()->Wait();
      }
      sleeping_threads_--;
      thread_state->idle_monitor()->Unlock();
    } else {
      while (!pause_) {
        Process* process = NULL;
        DequeueFromThread(thread_state, &process);
        // No more processes for this state, break.
        if (process == NULL) break;
        while (process != NULL) {
          process = InterpretProcess(process, thread_state);
        }
      }
    }
  }
  // TODO(ajohnsen): Delete ThreadStates (should happen when all threads are
  // guaranteed not to run).
  ThreadExit(thread_state);
}

void Scheduler::SetCurrentProcessForThread(int thread_id, Process* process) {
  if (thread_id == -1) return;
  ASSERT(current_processes_[thread_id] == NULL);
  current_processes_[thread_id] = process;
}

void Scheduler::ClearCurrentProcessForThread(int thread_id, Process* process) {
  if (thread_id == -1) return;
  while (true) {
    // Take value at each attempt, as value will be overriden on failure.
    Process* value = process;
    if (current_processes_[thread_id].compare_exchange_weak(value, NULL)) {
      break;
    }
  }
}

Process* Scheduler::InterpretProcess(Process* process,
                                     ThreadState* thread_state) {
  Program* program = process->program();

  int thread_id = thread_state->thread_id();
  SetCurrentProcessForThread(thread_id, process);

  // Mark the process as owned by the current thread while interpreting.
  process->set_thread_state(thread_state);
  Interpreter interpreter(process);
  interpreter.Run();
  process->set_thread_state(NULL);

  ClearCurrentProcessForThread(thread_id, process);

  if (interpreter.isTerminated()) {
    delete process;
    if (--processes_ == 0) {
      NotifyAllThreads();
    } else {
      if (Flags::IsOn("gc-on-delete")) {
        sleeping_threads_++;
        thread_state->cache()->Clear();
        program->CollectGarbage();
        sleeping_threads_--;
      }
    }
    return NULL;
  }

  if (interpreter.isYielded()) {
    process->ChangeState(Process::kRunning, Process::kYielding);
    if (process->IsQueueEmpty()) {
      process->ChangeState(Process::kYielding, Process::kSleeping);
    } else {
      process->ChangeState(Process::kYielding, Process::kReady);
      EnqueueOnThread(thread_state, process);
    }
    return NULL;
  }

  if (interpreter.isTargetYielded()) {
    // The returned port currently has the lock. Unlock as soon as we know the
    // process is not kRunning (ChangeState either succeeded or failed).
    Port* port = interpreter.target();
    ASSERT(port != NULL);
    ASSERT(port->IsLocked());
    Process* target = port->process();
    ASSERT(target != NULL);
    if (target->ChangeState(Process::kSleeping, Process::kRunning)) {
      port->Unlock();
      process->ChangeState(Process::kRunning, Process::kReady);
      EnqueueOnAnyThread(process, thread_state->thread_id() + 1);
      return target;
    } else {
      ProcessQueue* target_queue = target->process_queue();
      if (target_queue != NULL && target_queue->TryDequeueEntry(target)) {
        port->Unlock();
        ASSERT(target->state() == Process::kRunning);
        process->ChangeState(Process::kRunning, Process::kReady);
        EnqueueOnAnyThread(process, thread_state->thread_id() + 1);
        return target;
      }
    }
    port->Unlock();
    process->ChangeState(Process::kRunning, Process::kReady);
    EnqueueOnThread(thread_state, process);
    return NULL;
  }

  if (interpreter.isInterrupted()) {
    // No need to notify threads, as 'this' is now available.
    process->ChangeState(Process::kRunning, Process::kReady);
    EnqueueOnThread(thread_state, process);
    return NULL;
  }

  if (interpreter.isUncaughtException()) {
    // Just hang by not enqueueing the process. The session
    // will terminate the program on uncaught exceptions.
    return NULL;
  }

  UNREACHABLE();
  return NULL;
}

void Scheduler::ThreadEnter(ThreadState* thread_state) {
  // TODO(ajohnsen): This only works because we never return threads, unless
  // the scheduler in done.
  int thread_id = thread_count_++;
  ASSERT(thread_id < max_threads_);
  thread_state->set_thread_id(thread_id);
  threads_[thread_id] = thread_state;
  // Notify pause_monitor_ when changing threads_.
  pause_monitor_->Lock();
  pause_monitor_->Notify();
  pause_monitor_->Unlock();
}

void Scheduler::ThreadExit(ThreadState* thread_state) {
  threads_[thread_state->thread_id()] = NULL;
  // Notify pause_monitor_ when changing threads_.
  pause_monitor_->Lock();
  pause_monitor_->Notify();
  pause_monitor_->Unlock();
}

void Scheduler::NotifyAllThreads() {
  for (int i = 0; i < thread_count_; i++) {
    ThreadState* thread_state = threads_[i];
    if (thread_state != NULL) {
      thread_state->idle_monitor()->Lock();
      thread_state->idle_monitor()->Notify();
      thread_state->idle_monitor()->Unlock();
    }
  }
}

void Scheduler::DequeueFromThread(ThreadState* thread_state,
                                  Process** process) {
  ASSERT(*process == NULL);
  // While the current thread's queue is busy, try from the other.
  while (!thread_state->queue()->TryDequeue(process)) {
    // If we were able to dequeue a process, we are done.
    if (TryDequeueFromAnyThread(process, thread_state->thread_id()) &&
        *process != NULL) return;
  }
  // If no process was found (current thread's queue is empty), take a last loop
  // over all threads.
  if (*process == NULL) {
    TryDequeueFromAnyThread(process, thread_state->thread_id());
  }
}

static bool TryDequeue(ProcessQueue* queue,
                       Process** process,
                       bool* should_retry) {
  if (queue->TryDequeue(process)) {
    if (*process != NULL) {
      return true;
    }
  } else {
    *should_retry = true;
  }
  return false;
}

bool Scheduler::TryDequeueFromAnyThread(Process** process, int start_id) {
  ASSERT(*process == NULL);
  int count = thread_count_;
  bool should_retry = false;
  for (int i = start_id; i < count; i++) {
    ThreadState* thread_state = threads_[i];
    if (thread_state == NULL) continue;
    if (TryDequeue(thread_state->queue(), process, &should_retry)) return true;
  }
  for (int i = 0; i < start_id; i++) {
    ThreadState* thread_state = threads_[i];
    if (thread_state == NULL) continue;
    if (TryDequeue(thread_state->queue(), process, &should_retry)) return true;
  }
  // TODO(ajohnsen): Merge startup_queue_ into the first thread we start, or
  // use it for queing other proceses as well?
  if (TryDequeue(startup_queue_, process, &should_retry)) return true;
  return !should_retry;
}

void Scheduler::EnqueueOnThread(ThreadState* thread_state, Process* process) {
  if (thread_state->thread_id() == -1) {
    EnqueueOnAnyThread(process);
    return;
  }
  while (!thread_state->queue()->TryEnqueue(process)) {
    int count = thread_count_;
    for (int i = 0; i < count; i++) {
      ThreadState* thread_state = threads_[i];
      if (thread_state != NULL && thread_state->queue()->TryEnqueue(process)) {
        return;
      }
    }
  }
}

bool Scheduler::TryEnqueueOnIdleThread(Process* process) {
  while (true) {
    ThreadState* thread_state = PopIdleThread();
    if (thread_state == NULL) return false;
    bool was_empty = false;
    if (!thread_state->queue()->TryEnqueue(process, &was_empty)) {
      // Turns out someone else tried to spin it up. Take another one.
      continue;
    }
    thread_state->idle_monitor()->Lock();
    thread_state->idle_monitor()->Notify();
    thread_state->idle_monitor()->Unlock();
    return true;
  }
  UNREACHABLE();
  return false;
}

bool Scheduler::EnqueueOnAnyThread(Process* process, int start_id) {
  ASSERT(process->state() == Process::kReady);
  // First try to resume an idle thread.
  if (TryEnqueueOnIdleThread(process)) return true;
  // Loop threads until enqueued.
  int i = start_id;
  while (true) {
    if (i >= thread_count_) i = 0;
    ThreadState* thread_state = threads_[i];
    bool was_empty = false;
    if (thread_state != NULL &&
        thread_state->queue()->TryEnqueue(process, &was_empty)) {
      if (was_empty && current_processes_[i] == NULL) {
        thread_state->idle_monitor()->Lock();
        thread_state->idle_monitor()->Notify();
        thread_state->idle_monitor()->Unlock();
      }
      return false;
    }
    i++;
  }
  UNREACHABLE();
  return false;
}

void Scheduler::RunThread(void* data) {
  Scheduler* scheduler = reinterpret_cast<Scheduler*>(data);
  scheduler->RunInThread();
}

}  // namespace fletch
