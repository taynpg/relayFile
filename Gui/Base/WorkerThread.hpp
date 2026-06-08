#pragma once

#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>
#include <functional>
#include <utility>

template <typename T> class WorkerThread : public QThread
{
public:
    explicit WorkerThread(QObject* parent = nullptr) : QThread(parent)
    {
    }

    using Task = std::function<void()>;

    void invoke(Task task)
    {
        QMutexLocker locker(&mutex_);
        tasks_.enqueue(std::move(task));
        condition_.wakeOne();
    }

    void stop()
    {
        {
            QMutexLocker locker(&mutex_);
            stop_ = true;
        }
        condition_.wakeAll();
        wait();
    }

protected:
    void run() override
    {
        while (true) {
            Task task;
            {
                QMutexLocker locker(&mutex_);
                while (tasks_.isEmpty() && !stop_) {
                    condition_.wait(&mutex_);
                }

                if (stop_ && tasks_.isEmpty()) {
                    return;
                }

                task = std::move(tasks_.dequeue());
            }
            task();
        }
    }

private:
    QMutex mutex_;
    QWaitCondition condition_;
    QQueue<Task> tasks_;
    bool stop_{false};
};