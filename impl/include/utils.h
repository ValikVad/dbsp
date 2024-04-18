#pragma once

#include <algorithm>
#include <cstdio>
#include <memory>
#include <unordered_set>

#if defined(PREFETCH_ENABLE_TRACE)
#    include <glog/logging.h>
#    define FORMAT_REQUEST(r)                 "R{" << std::dec << r->start_addr_ << "," << r->size_bytes_ << "}"
#    define FORMAT_REQUEST_WITH_TIME_STAMP(r) FORMAT_REQUEST(r) << " TS{" << r->Count() << "," << r->Stamp(0) << ".." << ts << "}"
#else
struct NullLog {
    template <typename... Args>
    NullLog& operator<<(Args&&...) {
        return *this;
    }
};
#    define LOG(...) \
        NullLog {}
#    define LOG_IF(...) \
        NullLog {}
#    define DLOG(...) \
        NullLog {}
#    define VLOG(...) \
        NullLog {}
#    define VLOG_IF(...) \
        NullLog {}
#    define VLOG_IS_ON(...) (false)

#    define FORMAT_REQUEST(...)                 ""
#    define FORMAT_REQUEST_WITH_TIME_STAMP(...) ""
#endif

template <typename T>
struct LimitedQueue {
    using value_type = T;
    std::unique_ptr<T[]> data;

    // The virtual beginning of the ring buffer
    T* _first = nullptr;
    // The virtual end of the ring buffer (after the last element)
    T* _last = nullptr;
    // End of storage
    T* _end = nullptr;
    // Num of virtual iterms
    size_t _size = 0;

    LimitedQueue(size_t s) : data(std::make_unique<T[]>(s)), _first(data.get()), _last(_first), _end(_first + s), _size() {}

    LimitedQueue(size_t s, T const& t) : data(std::make_unique<T[]>(s)), _first(data.get()), _last(_first), _end(_first + s), _size() {
        std::fill_n(data.get(), s, t);  // no way to create std::unique_ptr<[]> with arguments in ctor
    }

    LimitedQueue(LimitedQueue const& q)
        : data(std::make_unique<T[]>(q.Capacity())),
          _first(data.get() + (q._first - q.data.get())),
          _last(data.get() + (q._last - q.data.get())),
          _end(data.get() + (q._end - q.data.get())),
          _size(q._size) {
        std::copy_n(data.get(), Capacity(), q.data.get());
    }

    LimitedQueue(LimitedQueue&& q) : data(std::move(q.data)), _first(q._first), _last(q._last), _end(q._end), _size(q._size) {
        q._first = q._last = q._end = nullptr;
        q._size = 0;
    }

    LimitedQueue& operator=(LimitedQueue const& q) {
        LimitedQueue t(q);
        std::swap(t.data, this->data);
        std::swap(t._first, this->_first);
        std::swap(t._last, this->_last);
        std::swap(t._end, this->_end);
        std::swap(t._size, this->_size);

        return *this;
    }

    LimitedQueue& operator=(LimitedQueue&& q) {
        if (this != &q) {
            data = std::move(q.data);
            _first = q._first;
            _last = q._last;
            _end = q._end;
            _size = q._size;

            q._first = q._last = q._end = nullptr;
            q._size = 0;
        }
        return *this;
    }

    T* Emplace(T&& t) {
        *_last = std::move(t);
        auto r = _last;
        Increment(_last);
        if (Full()) {
            _first = _last;
        } else {
            ++_size;
        }

        return r;
    }

    T* Push(T t) {
        return Emplace(std::move(t));
    }

    void Pop() {
        if (_size) {
            Decrement(_last);
            --_size;
        }
    }

    T* Front() {
        return _first;
    }

    T* Back() {
        return ((_last == data.get() ? _end : _last) - 1);
    }

    void Clear() {
        _first = _last = data.get();
        _size = 0;
    }

    size_t Capacity() const {
        return _end - data.get();
    }

    size_t Size() const {
        return _size;
    }

    bool Full() const {
        return Capacity() == Size();
    }

private:
    void Increment(T*& ptr) const {
        if (++ptr == _end)
            ptr = const_cast<T*>(data.get());
    }

    void Decrement(T*& ptr) const {
        if (ptr == data.get())
            ptr = _end;
        --ptr;
    }

    T* Add(T* p, ptrdiff_t n) const {
        return p + (n < (_end - p) ? n : n - (_end - data.get()));
    }

    template <class Pointer>
    Pointer Sub(Pointer p, ptrdiff_t n) const {
        return p - (n > (p - data.get()) ? n - (_end - data.get()) : n);
    }

public:
    class iterator {
    public:
        using self_type = iterator;
        using value_type = T;
        using reference = T&;
        using pointer = T*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;

        iterator(LimitedQueue<T>* container, pointer ptr) : _container(container), _it(ptr) {}
        iterator& operator++() {
            _container->Increment(_it);
            if (_it == _container->_last)
                _it = 0;

            return *this;
        }
        iterator operator++(int) {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }

        iterator& operator--() {
            if (_it == 0)
                _it = _container->_last;
            _container->Decrement(_it);
            return *this;
        }

        iterator operator--(int) {
            iterator tmp = *this;
            --*this;
            return tmp;
        }

        reference operator*() const {
            return *_it;
        }
        pointer operator->() const {
            return &(operator*());
        }

        bool operator==(const iterator& rhs) {
            return _it == rhs._it;
        }
        bool operator!=(const iterator& rhs) {
            return _it != rhs._it;
        }

        pointer linearize(const iterator& it) const {
            return it._it == 0 ? _container->data.get() + _container->Size() :
                                 (it._it < _container->_first ? it._it + (_container->_end - _container->_first) :
                                                                _container->data.get() + (it._it - _container->_first));
        }

        difference_type operator-(const iterator& it) const {
            return linearize(*this) - linearize(it);
        }

        iterator operator+(difference_type n) const {
            return iterator(*this) += n;
        }
        iterator& operator+=(difference_type n) {
            if (n > 0) {
                _it = _container->Add(_it, n);
                if (_it == _container->_last)
                    _it = 0;
            } else if (n < 0) {
                *this -= -n;
            }
            return *this;
        }

        iterator operator-(difference_type n) const {
            return iterator(*this) -= n;
        }
        iterator& operator-=(difference_type n) {
            if (n > 0) {
                _it = _container->Sub(_it == 0 ? _container->_last : _it, n);
            } else if (n < 0) {
                *this += -n;
            }
            return *this;
        }

        bool operator<(const iterator& it) const {
            return linearize(*this) < linearize(it);
        }

        bool operator>(const iterator& it) const {
            return it < *this;
        }
        bool operator<=(const iterator& it) const {
            return !(it < *this);
        }
        bool operator>=(const iterator& it) const {
            return !(*this < it);
        }

    private:
        LimitedQueue<T>* _container = nullptr;
        pointer _it = nullptr;
    };

    iterator begin() {
        return iterator(this, _size == 0 ? 0 : _first);
    }

    iterator end() {
        return iterator(this, 0);
    }
};

//Requires bool 'Valid(T)'
template <typename T>
struct LimitedHash {
    using table_type = LimitedQueue<T>;
    using hash_type = std::unordered_set<T*>;

    table_type table;
    hash_type hash;

    LimitedHash(size_t _size) : table(_size) {
        hash.reserve(_size);
    }

    LimitedHash(size_t _size, T const& t) : table(_size, t) {
        hash.reserve(_size);
    }

    //Type 'U' should be convertible to 'T'
    template <typename U>
    T* Find(U u) {
        T x{ u };
        auto i = hash.find(&x);
        return i != std::end(hash) ? *i : nullptr;
    }

    //Returns pointer to element and denoting whether the insertion took place
    std::pair<T*, bool> Push(T const& t) {
        auto i = hash.find(const_cast<T*>(&t));
        if (i != std::end(hash))
            return std::make_pair(*i, false);
        else {
            if (Valid(*table._last))
                hash.erase(table._last);

            auto r = table.Push(t);
            hash.insert(r);
            return std::make_pair(r, true);
        }
    }

    //Type 'U' should be convertible to 'T'
    //Returns pointer to element and  denoting whether the insertion took place
    template <typename U>
    std::pair<T*, bool> Push(U u) {
        T x{ u };
        return Push(x);
    }

    //Extract element to 'x' keeps tracking it as key
    void Extract(T* r, T& x) {
        table.Pop();

        hash.erase(r);
        x = std::move(*r);
        if (r != table._last) {
            hash.erase(table._last);
            *r = std::move(*table._last);
            hash.insert(r);
        }

        hash.insert(&x);
    }

    template <typename U>
    void Extract(U u, T& x) {
        Extract(Find(u), x);
    }

    /* Resulted 'this' table has all entries from RHS table plus,
       if capacity allows, old entries from 'this' table giving priority to entries with lowest index */
    void Merge(LimitedHash&& h) {
        std::swap(*this, h);
        if (table.Full()) {  // no place to insert old results
            h.Clear();
            return;
        }

        auto b = std::begin(h.table);
        auto e = std::end(h.table);
        while (!table.Full() && b != e) {
            Push(std::move(*b));
            ++b;
        }

        h.Clear();
    }

    void Clear() {
        hash.clear();
        table.Clear();
    }

    size_t Size() const {
        return hash.size();
    }
};
