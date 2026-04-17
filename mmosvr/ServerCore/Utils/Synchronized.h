#pragma once

#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <functional>

template<typename StoreType, typename MutexType = std::shared_mutex>
class Synchronized;

template<typename StoreType>
class Synchronized<StoreType, std::shared_mutex>
{
public:
    Synchronized() : m_StoreValue{} {}
    explicit Synchronized(StoreType&& value) : m_StoreValue(std::move(value)) {}
    explicit Synchronized(const StoreType& value) : m_StoreValue(value) {}
    ~Synchronized() = default;
    
    Synchronized(const Synchronized&) = delete;
    Synchronized& operator=(const Synchronized&) = delete;
    Synchronized(Synchronized&&) = delete;
    Synchronized& operator=(Synchronized&&) = delete;

private:
    mutable std::shared_mutex m_Lock;
    StoreType m_StoreValue;

public:
    class WritePtr
    {
        std::unique_lock<std::shared_mutex> lock;
        StoreType* value;
    public:
        WritePtr(std::shared_mutex& mutex, StoreType& val) : lock(mutex), value(&val) {}
        ~WritePtr() = default;
        
        WritePtr(WritePtr&&) = default;
        WritePtr(const WritePtr&) = delete;
        WritePtr& operator=(const WritePtr&) = delete;
        WritePtr& operator=(WritePtr&&) = delete;
        
        StoreType* operator->() noexcept { return value; }
        StoreType& operator*() noexcept { return *value; }
    };

    class ReadPtr
    {
        std::shared_lock<std::shared_mutex> lock;
        const StoreType* value;
    public:
        ReadPtr(std::shared_mutex& mutex, const StoreType& val) : lock(mutex), value(&val) {}
        ~ReadPtr() = default;
        
        ReadPtr(ReadPtr&&) = default;
        ReadPtr(const ReadPtr&) = delete;
        ReadPtr& operator=(const ReadPtr&) = delete;
        ReadPtr& operator=(ReadPtr&&) = delete;
        
        const StoreType* operator->() const noexcept { return value; }
        const StoreType& operator*() const noexcept { return *value; }
    };

    [[nodiscard]] WritePtr WriteLock() noexcept 
    { 
        return WritePtr(m_Lock, m_StoreValue); 
    }
    
    [[nodiscard]] ReadPtr ReadLock() const noexcept 
    { 
        return ReadPtr(m_Lock, m_StoreValue); 
    }

    template<typename Func>
    auto Write(Func&& f) 
    {
        std::unique_lock _lk(m_Lock);
        return std::invoke(std::forward<Func>(f), m_StoreValue);
    }

    template<typename Func>
    auto Read(Func&& f) const 
    {
        static_assert(std::is_invocable_v<Func, const StoreType&>, 
            "Lambda must accept const reference: [](const T&)");
        std::shared_lock _lk(m_Lock);
        return std::invoke(std::forward<Func>(f), m_StoreValue);
    }

    [[nodiscard]] StoreType Copy() const 
    {
        std::shared_lock _lk(m_Lock);
        return m_StoreValue;
    }
};

template<typename StoreType>
class Synchronized<StoreType, std::mutex>
{
public:
    Synchronized() : m_StoreValue{} {}
    explicit Synchronized(StoreType&& value) : m_StoreValue(std::move(value)) {}
    explicit Synchronized(const StoreType& value) : m_StoreValue(value) {}
    ~Synchronized() = default;
    
    Synchronized(const Synchronized&) = delete;
    Synchronized& operator=(const Synchronized&) = delete;
    Synchronized(Synchronized&&) = delete;
    Synchronized& operator=(Synchronized&&) = delete;

private:
    mutable std::mutex m_Lock;
    StoreType m_StoreValue;

public:
    class ValuePtr
    {
        std::unique_lock<std::mutex> lock;
        StoreType* value;
    public:
        ValuePtr(std::mutex& mutex, StoreType& val) : lock(mutex), value(&val) {}
        ~ValuePtr() = default;
        
        ValuePtr(ValuePtr&&) = default;
        ValuePtr(const ValuePtr&) = delete;
        ValuePtr& operator=(const ValuePtr&) = delete;
        ValuePtr& operator=(ValuePtr&&) = delete;
        
        StoreType* operator->() noexcept { return value; }
        StoreType& operator*() noexcept { return *value; }
    };

    class ConstValuePtr
    {
        std::unique_lock<std::mutex> lock;
        const StoreType* value;
    public:
        ConstValuePtr(std::mutex& mutex, const StoreType& val) : lock(mutex), value(&val) {}
        ~ConstValuePtr() = default;
        
        ConstValuePtr(ConstValuePtr&&) = default;
        ConstValuePtr(const ConstValuePtr&) = delete;
        ConstValuePtr& operator=(const ConstValuePtr&) = delete;
        ConstValuePtr& operator=(ConstValuePtr&&) = delete;
        
        const StoreType* operator->() const noexcept { return value; }
        const StoreType& operator*() const noexcept { return *value; }
    };

    [[nodiscard]] ValuePtr Lock() noexcept 
    { 
        return ValuePtr(m_Lock, m_StoreValue); 
    }
    
    [[nodiscard]] ConstValuePtr Lock() const noexcept 
    { 
        return ConstValuePtr(m_Lock, m_StoreValue); 
    }

    template<typename Func>
    auto WithLock(Func&& f) 
    {
        std::unique_lock _lk(m_Lock);
        return std::invoke(std::forward<Func>(f), m_StoreValue);
    }

    template<typename Func>
    auto WithLock(Func&& f) const
    {
        static_assert(std::is_invocable_v<Func, const StoreType&>, 
            "Lambda must accept const reference: [](const T&)");
        std::unique_lock _lk(m_Lock);
        return std::invoke(std::forward<Func>(f), std::as_const(m_StoreValue));
    }

    [[nodiscard]] StoreType Copy() const 
    {
        std::unique_lock _lk(m_Lock);
        return m_StoreValue;
    }
};