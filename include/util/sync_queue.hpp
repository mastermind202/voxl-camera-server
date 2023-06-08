/*******************************************************************************
 * Copyright 2023 ModalAI Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * 4. The Software is used solely in conjunction with devices provided by
 *    ModalAI Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

#ifndef SYNC_QUEUE_H
#define SYNC_QUEUE_H

#include <condition_variable>
#include <mutex>
#include <deque>
#include <cstdint>

using std::unique_lock;

/**
 * The list of possible errors which can be returned from fallible SyncQueue
 * functions
 */
/**
 * This is a simple wrapper class which represents the union of a result, and an
 * error. It is a clean way to indicate that a function can either return an
 * error, or some value on success. It is internally implemented as a tagged
 * union of the two types, so that it's size is proportional to the larger of
 * the two contained types.
*/
template<typename T, typename E>
class Result {
public:
    /// construct a new result which is Ok
    static Result ok(T val) noexcept {
        ContainedUnion c;
        c.val = val;
        return Result(Tag::Ok, c);
    };

    /// construct a new result which contains an error
    static Result err(E err) noexcept {
        ContainedUnion c;
        c.err = err;
        return Result(Tag::Err);
    };

    /// returns whether this result is ok
    bool is_ok() const noexcept {
        return tag == Tag::Ok;
    };
    /// returns whether this result is an error
    bool is_err() const noexcept {
        return tag == Tag::Err;
    };

    /// moves the contained value out of this Result. If this Result contains an
    /// error, the behavior of this function is undefined.
    T&& value() noexcept {
        return std::move(contents.val);
    };

    T const& value_ref() const noexcept {
        return contents.val;
    }

    /// Moves the contained error out of this Result. If this Result contains a
    /// value, the behavior of this function is undefined.
    E&& error() noexcept {
        return std::move(contents.err);
    };

    E const& error_ref() const noexcept {
        return contents.err;
    }

private:
    // Result(Tag t, ContainedUnion c) : tag(t), contents(c) {};

    enum class Tag {
        Ok,
        Err,
    };
    union ContainedUnion {
        T val;
        E err;
    };
    
    const Tag tag;
    ContainedUnion contents;
};


enum class SyncQueueErr {
    Canceled,
    Empty,
};



/**
 * This class represents an unbounded MPMC synchronized queue. It is
 * synchronized using a combination of mutexes and conditional variables, such
 * that push() and pop() will block until the resources required to carry out
 * the operation become available.
 *
 * This queue operates on owned data -- for fast operations, the contained type
 * should always be trivially copyable. I would enforce this with a C++20
 * concept, but I think we're stuck on C++11...
 */
template<typename T>
class SynchronizedQueue {
    typedef Result<T, SyncQueueErr> QueueRes;
public:
    /**
     * Construct a new synchronized queue
     */
    SynchronizedQueue() {};


    /**
    * Push a new element into the queue. This will block until a push is
    * possible.
    */
    void push(T data) {
        // push data -- scope lock guard to release mutex before notifying
        {
            std::lock_guard<mutex>(m_mutex);
            m_queue.push(data);
        }
        // notify waiting threads
        m_cond.notify_one();
    };


    /**
    * Pop an element from the front of the queue. This will block until a pop
    * is possible. If the queue is empty, this will block until an element is
    * available to be popped.
    *
    * If the queue is dropped or canceled before this method returns, it will
    * return Err(Canceled); otherwise, it will returh Ok(val).
    */
    QueueRes pop() {
        unique_lock<mutex> lk(m_mutex);
        m_cond.wait(lk, [this] { return (!m_queue.empty() || m_canceled); });

        if (m_canceled) {
            return QueueRes::Err(SyncQueueErr::Canceled);
        }

        T res = std::move(m_queue.front());
        m_queue.pop();
        return QueueRes::Ok(res);
    };


    /**
    * Return a copy of the front element in the queue. This will block until we
    * are able to safely peek at the front element. If the queue is empty, this
    * will block until an element is available to peek.
    *
    * If the queue is dropped or canceled before this method returns, it will
    * return Err(Canceled); otherwise, it will returh Ok(val).
    */
    QueueRes peek() const {
        unique_lock<mutex> lk(m_mutex);
        m_cond.wait(lk, [this] { return (!m_queue.empty() || m_canceled); });

        if (m_canceled) {
            return QueueRes::Err(SyncQueueErr::Canceled);
        }

        return QueueRes::Ok(m_queue.front());
    };

    
    /**
     *  Attempt to pop a value from the queue. This will not block until the
     *  queue is full. If the queue has a value in it, it will be popped and we
     *  will return Ok(val); otherwise, we will return Err(Empty).
     */
    QueueRes try_pop(T& dest) const {
        lock_guard<mutex> lk(m_mutex);
        
        if (m_queue.empty()) {
            return QueueRes::Err(SyncQueueErr::Empty);
        }

        T res = std::move(m_queue.front());
        m_queue.pop();
        return QueueRes::Ok(res);
    };


    /**
     * Return the current size of the queue.
     */
    size_t size() {
        lock_guard<mutex> lk(m_mutex);
        return m_queue.size();
    };


    /**
     * Return whether or not the queue is currently empty.
     */
    bool empty() {
        lock_guard<mutex> lk(m_mutex);
        return m_queue.empty();
    };


    /**
     * Cancel the queue. This will cause all threads waiting on `pop()` to
     * unblock and return Err(Canceled) 
     */
    void cancel() {
        // scope lock guard to release mutex before notifying
        {
            lock_guard<mutex> lk(m_mutex);
            m_canceled = true;
        }
        m_cond.notify_all();
    }

private:
    std::deque<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_canceled;
};

#endif // SYNC_QUEUE_H
