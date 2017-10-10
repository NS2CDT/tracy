#ifndef __TRACYLOCK_HPP__
#define __TRACYLOCK_HPP__

#include <atomic>

#include "../common/TracySystem.hpp"
#include "TracyProfiler.hpp"

namespace tracy
{

static std::atomic<uint64_t> s_lockCounter( 0 );

template<class T>
class Lockable
{
public:
    tracy_force_inline Lockable( const SourceLocation* srcloc )
        : m_id( s_lockCounter.fetch_add( 1, std::memory_order_relaxed ) )
    {
        Magic magic;
        auto& token = s_token;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        item->hdr.type = QueueType::LockAnnounce;
        item->lockAnnounce.id = m_id;
        item->lockAnnounce.srcloc = (uint64_t)srcloc;
        tail.store( magic + 1, std::memory_order_release );
    }

    Lockable( const Lockable& ) = delete;
    Lockable& operator=( const Lockable& ) = delete;

    tracy_force_inline void lock()
    {
        uint32_t cpu;
        const auto thread = GetThreadHandle();
        {
            Magic magic;
            auto& token = s_token;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            item->hdr.type = QueueType::LockWait;
            item->lockWait.id = m_id;
            item->lockWait.thread = thread;
            item->lockWait.time = Profiler::GetTime( cpu );
            tail.store( magic + 1, std::memory_order_release );
        }

        m_lockable.lock();

        {
            Magic magic;
            auto& token = s_token;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            item->hdr.type = QueueType::LockObtain;
            item->lockObtain.id = m_id;
            item->lockObtain.thread = thread;
            item->lockObtain.time = Profiler::GetTime( cpu );
            tail.store( magic + 1, std::memory_order_release );
        }
    }

    tracy_force_inline void unlock()
    {
        m_lockable.unlock();

        uint32_t cpu;
        Magic magic;
        auto& token = s_token;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        item->hdr.type = QueueType::LockRelease;
        item->lockRelease.id = m_id;
        item->lockRelease.thread = GetThreadHandle();
        item->lockRelease.time = Profiler::GetTime( cpu );
        tail.store( magic + 1, std::memory_order_release );
    }

    tracy_force_inline bool try_lock()
    {
        const auto ret = m_lockable.try_lock();
        if( ret )
        {
            uint32_t cpu;
            Magic magic;
            auto& token = s_token;
            auto& tail = token->get_tail_index();
            auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
            item->hdr.type = QueueType::LockObtain;
            item->lockObtain.id = (uint64_t)&m_lockable;
            item->lockObtain.thread = GetThreadHandle();
            item->lockObtain.time = Profiler::GetTime( cpu );
            tail.store( magic + 1, std::memory_order_release );
        }
        return ret;
    }

    tracy_force_inline void Mark( const SourceLocation* srcloc ) const
    {
        Magic magic;
        auto& token = s_token;
        auto& tail = token->get_tail_index();
        auto item = token->enqueue_begin<moodycamel::CanAlloc>( magic );
        item->hdr.type = QueueType::LockMark;
        item->lockMark.id = m_id;
        item->lockMark.thread = GetThreadHandle();
        item->lockMark.srcloc = (uint64_t)srcloc;
        tail.store( magic + 1, std::memory_order_release );
    }

private:
    T m_lockable;
    uint64_t m_id;
};

};

#endif
