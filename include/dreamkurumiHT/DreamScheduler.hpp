#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <vector>
#include <deque>
#include <chrono>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <algorithm>
#include "DreamCrashGuard.hpp"
#include "DreamLog.hpp"

namespace dreamkurumi {

using TaskId = uint64_t;
using TaskFn = std::function<void()>;
using RecurringTaskFn = std::function<bool()>;
using PredicateFn = std::function<bool()>;

// ============================================================================
// DreamScheduler - Game Main-Thread Coroutine & Background Task Runner (v3.8.0-beta)
// Coordinates background worker threads, network tasks, and delayed execution
// safely on the main thread within frame-time budgets.
// ============================================================================
class DreamScheduler {
public:
    static DreamScheduler& Instance() {
        static DreamScheduler s_inst;
        return s_inst;
    }

    // Run a task on the next game frame / main thread tick
    TaskId Enqueue(TaskFn task) {
        if (!task) return 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        TaskId id = ++m_nextId;
        m_immediateQueue.push_back({ id, std::move(task) });
        return id;
    }

    // Run a task once after delayMs milliseconds
    TaskId RunAfter(uint32_t delayMs, TaskFn task) {
        if (!task) return 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        TaskId id = ++m_nextId;
        auto runAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
        m_delayedTasks.push_back({ id, std::move(task), runAt });
        return id;
    }

    // Run a task recurringly every intervalMs milliseconds.
    // If task() returns false, it will automatically cancel.
    TaskId RunEvery(uint32_t intervalMs, RecurringTaskFn task) {
        if (!task) return 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        TaskId id = ++m_nextId;
        auto runAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
        m_recurringTasks.push_back({ id, std::move(task), intervalMs, runAt });
        return id;
    }

    // Coroutine-style yield: tests predicate on each tick.
    // When predicate returns true, onReady is executed.
    // If timeoutMs > 0 and timeout expires, onTimeout (if provided) is executed.
    TaskId YieldUntil(PredicateFn predicate, TaskFn onReady, uint32_t timeoutMs = 0, TaskFn onTimeout = nullptr) {
        if (!predicate || !onReady) return 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        TaskId id = ++m_nextId;
        auto startTime = std::chrono::steady_clock::now();
        m_yieldTasks.push_back({ id, std::move(predicate), std::move(onReady), std::move(onTimeout), startTime, timeoutMs });
        return id;
    }

    // Cancel a scheduled task by ID
    bool Cancel(TaskId id) {
        if (id == 0) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancelledIds.insert(id);
        return true;
    }

    // Headless Dedicated Server tick entry point (Vulkan is absent on headless server)
    static void DedicatedServerTick(double maxTimeBudgetMs = 1.0) {
        Instance().Tick(maxTimeBudgetMs);
    }

    // Main thread execution pump.
    // Processes immediate, delayed, recurring, and coroutine tasks.
    // maxTimeBudgetMs: Max execution duration before deferring remaining tasks to next frame.
    // Recommended default: 1.0ms to prevent frame drops on 144Hz+ displays.
    size_t Tick(double maxTimeBudgetMs = 1.0) {
        auto startTime = std::chrono::steady_clock::now();
        size_t executedCount = 0;

        auto isTimeExhausted = [&]() -> bool {
            if (maxTimeBudgetMs <= 0.0) return false;
            auto now = std::chrono::steady_clock::now();
            double elapsedMs = std::chrono::duration<double, std::milli>(now - startTime).count();
            return elapsedMs >= maxTimeBudgetMs;
        };

        // 1. Process YieldUntil (Coroutine) tasks
        {
            std::vector<YieldTask> activeYields;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                activeYields.swap(m_yieldTasks);
            }

            auto now = std::chrono::steady_clock::now();
            std::vector<YieldTask> remainingYields;

            for (auto& yt : activeYields) {
                if (IsCancelled(yt.id)) continue;

                bool ready = false;
                DreamCrashGuard::ExecuteSafely("SchedulerYieldPred", [&]() {
                    ready = yt.predicate();
                });

                if (ready) {
                    DreamCrashGuard::ExecuteSafely("SchedulerYieldReady", [&]() {
                        yt.onReady();
                    });
                    executedCount++;
                } else if (yt.timeoutMs > 0 &&
                           std::chrono::duration_cast<std::chrono::milliseconds>(now - yt.startTime).count() >= yt.timeoutMs) {
                    if (yt.onTimeout) {
                        DreamCrashGuard::ExecuteSafely("SchedulerYieldTimeout", [&]() {
                            yt.onTimeout();
                        });
                    }
                    executedCount++;
                } else {
                    remainingYields.push_back(std::move(yt));
                }

                if (isTimeExhausted()) break;
            }

            if (!remainingYields.empty()) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_yieldTasks.insert(m_yieldTasks.begin(),
                                    std::make_move_iterator(remainingYields.begin()),
                                    std::make_move_iterator(remainingYields.end()));
            }
        }

        // 2. Process Delayed Tasks (RunAfter)
        if (!isTimeExhausted()) {
            std::vector<DelayedTask> readyTasks;
            auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_delayedTasks.begin();
                while (it != m_delayedTasks.end()) {
                    if (IsCancelled(it->id)) {
                        it = m_delayedTasks.erase(it);
                    } else if (it->runAt <= now) {
                        readyTasks.push_back(std::move(*it));
                        it = m_delayedTasks.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            for (auto& dt : readyTasks) {
                DreamCrashGuard::ExecuteSafely("SchedulerDelayedTask", [&]() {
                    dt.task();
                });
                executedCount++;
                if (isTimeExhausted()) break;
            }
        }

        // 3. Process Recurring Tasks (RunEvery)
        if (!isTimeExhausted()) {
            std::vector<RecurringTask> tasksToRun;
            auto now = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& rt : m_recurringTasks) {
                    if (!IsCancelled(rt.id) && rt.nextRunAt <= now) {
                        tasksToRun.push_back(rt);
                        rt.nextRunAt = now + std::chrono::milliseconds(rt.intervalMs);
                    }
                }
            }

            for (auto& rt : tasksToRun) {
                bool keepRunning = true;
                DreamCrashGuard::ExecuteSafely("SchedulerRecurringTask", [&]() {
                    keepRunning = rt.task();
                });
                executedCount++;
                if (!keepRunning) {
                    Cancel(rt.id);
                }
                if (isTimeExhausted()) break;
            }
        }

        // 4. Process Immediate Tasks (Swap-Under-Lock Pattern - Zero Mutex Contention)
        if (!isTimeExhausted()) {
            std::deque<ImmediateTask> tasksToRun;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                tasksToRun.swap(m_immediateQueue);
            }

            while (!tasksToRun.empty()) {
                auto currentTask = std::move(tasksToRun.front());
                tasksToRun.pop_front();

                if (!IsCancelled(currentTask.id) && currentTask.task) {
                    DreamCrashGuard::ExecuteSafely("SchedulerImmediateTask", [&]() {
                        currentTask.task();
                    });
                    executedCount++;
                }

                if (isTimeExhausted()) {
                    // Put remaining unexecuted tasks back into front of queue
                    if (!tasksToRun.empty()) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        while (!tasksToRun.empty()) {
                            m_immediateQueue.push_front(std::move(tasksToRun.back()));
                            tasksToRun.pop_back();
                        }
                    }
                    break;
                }
            }
        }

        // Clean up cancelled tasks
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_cancelledIds.empty()) {
                m_delayedTasks.erase(
                    std::remove_if(m_delayedTasks.begin(), m_delayedTasks.end(),
                                   [this](const DelayedTask& t) { return IsCancelled(t.id); }),
                    m_delayedTasks.end());

                m_recurringTasks.erase(
                    std::remove_if(m_recurringTasks.begin(), m_recurringTasks.end(),
                                   [this](const RecurringTask& t) { return IsCancelled(t.id); }),
                    m_recurringTasks.end());

                m_yieldTasks.erase(
                    std::remove_if(m_yieldTasks.begin(), m_yieldTasks.end(),
                                   [this](const YieldTask& t) { return IsCancelled(t.id); }),
                    m_yieldTasks.end());

                m_cancelledIds.clear();
            }
        }

        return executedCount;
    }

    size_t GetPendingTaskCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_immediateQueue.size() + m_delayedTasks.size() + m_recurringTasks.size() + m_yieldTasks.size();
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_immediateQueue.clear();
        m_delayedTasks.clear();
        m_recurringTasks.clear();
        m_yieldTasks.clear();
        m_cancelledIds.clear();
    }

private:
    DreamScheduler() = default;

    bool IsCancelled(TaskId id) const {
        return m_cancelledIds.find(id) != m_cancelledIds.end();
    }

    struct ImmediateTask {
        TaskId id;
        TaskFn task;
    };

    struct DelayedTask {
        TaskId id;
        TaskFn task;
        std::chrono::steady_clock::time_point runAt;
    };

    struct RecurringTask {
        TaskId id;
        RecurringTaskFn task;
        uint32_t intervalMs;
        std::chrono::steady_clock::time_point nextRunAt;
    };

    struct YieldTask {
        TaskId id;
        PredicateFn predicate;
        TaskFn onReady;
        TaskFn onTimeout;
        std::chrono::steady_clock::time_point startTime;
        uint32_t timeoutMs;
    };

    mutable std::mutex m_mutex;
    std::atomic<TaskId> m_nextId{0};
    std::deque<ImmediateTask> m_immediateQueue;
    std::vector<DelayedTask> m_delayedTasks;
    std::vector<RecurringTask> m_recurringTasks;
    std::vector<YieldTask> m_yieldTasks;
    std::unordered_set<TaskId> m_cancelledIds;
};

} // namespace dreamkurumi
