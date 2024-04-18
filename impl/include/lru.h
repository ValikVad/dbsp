#pragma once

#include <icache.h>

#include <list>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

template <typename key_t, typename value_t>
class lru_cache {
public:
    typedef typename std::pair<key_t, value_t> key_value_pair_t;
    typedef typename std::list<key_value_pair_t>::iterator list_iterator_t;

    lru_cache(size_t max_size) : _max_size(max_size) {}

    std::pair<value_t& /*added*/, std::optional<key_value_pair_t> /*evicted*/> put(const key_t& key, const value_t& value) {
        auto it = _cache_items_map.find(key);
        _cache_items_list.push_front(key_value_pair_t(key, value));
        if (it != _cache_items_map.end()) {
            _cache_items_list.erase(it->second);
            _cache_items_map.erase(it);
        }
        auto& val = _cache_items_map[key];
        val = _cache_items_list.begin();

        if (_cache_items_map.size() > _max_size) {
            auto& oldest = _cache_items_list.back();
            auto evicted_key = oldest.first;
            auto evicted_page = std::move(oldest.second);

            _cache_items_list.pop_back();
            _cache_items_map.erase(evicted_key);

            return std::make_pair(std::ref(val->second), std::make_optional(std::move(key_value_pair_t(evicted_key, evicted_page))));
        }
        return std::make_pair(std::ref(val->second), std::nullopt);
    }

    std::optional<std::reference_wrapper<value_t>> get(const key_t& key) {
        auto it = _cache_items_map.find(key);
        if (it == _cache_items_map.end())
            return std::nullopt;

        _cache_items_list.splice(_cache_items_list.begin(), _cache_items_list, it->second);
        return std::make_optional(std::ref(it->second->second));
    }

    bool exists(const key_t& key) const {
        return _cache_items_map.find(key) != _cache_items_map.end();
    }

    size_t size() const {
        return _cache_items_map.size();
    }

private:
    std::list<key_value_pair_t> _cache_items_list;
    std::unordered_map<key_t, list_iterator_t> _cache_items_map;
    size_t _max_size;
};

struct Page {
    size_t id;
    size_t block_id;  // normalized to max page size
    size_t num_blocks;

    Page() : id(0), block_id(0), num_blocks(0) {}

    Page(size_t i, size_t b, size_t l) : id(i), block_id(b), num_blocks(l) {}
};

static Page GetPage(const CacheParams& par, size_t begin_id, size_t end_id) {
    const auto max_blocks = par.page_size / par.block_size;
    if (begin_id < end_id) {
        auto page_idx = begin_id * par.block_size / par.page_size;
        auto range = std::pair<size_t, size_t>(begin_id, end_id);

        const auto current_page = std::make_pair<size_t, size_t>(page_idx * max_blocks, (page_idx + 1) * max_blocks);
        const auto start_idx_inside_page = range.first - current_page.first;
        const auto end_idx_inside_page =
            (current_page.first < range.second) && (range.second < current_page.second) ? (range.second - current_page.first) : max_blocks;
        const auto block_num_inside_page = end_idx_inside_page - start_idx_inside_page;

        return Page(page_idx, start_idx_inside_page, block_num_inside_page);
    }

    return Page(0, 0, 0);
}

static Page GetPage(const CacheParams& par, const Request& r) {
    return GetPage(par, r.start_addr_ / par.block_size, (r.start_addr_ + r.size_bytes_) / par.block_size);
}

static void VerifyParams(const CacheParams& par) {
    if (par.cache_size % par.page_size)
        throw std::runtime_error(std::string("cache_size ") + std::to_string(par.cache_size) + std::string(" is not muptiple to page_size ") +
                                 std::to_string(par.page_size));
    if (par.cache_size % par.block_size)
        throw std::runtime_error(std::string("cache_size ") + std::to_string(par.cache_size) + std::string(" is not muptiple to block_size ") +
                                 std::to_string(par.block_size));
    if (par.page_size % par.block_size)
        throw std::runtime_error(std::string("page_size ") + std::to_string(par.page_size) + std::string(" is not muptiple to block_size ") +
                                 std::to_string(par.block_size));
}

static void VerifyRequest(const Request& r, const CacheParams& par) {
    if (r.size_bytes_ % par.block_size)
        throw std::runtime_error(std::string("size_bytes_ ") + std::to_string(r.size_bytes_) + std::string(" is not muptiple to block_size ") +
                                 std::to_string(par.block_size));

    if (r.start_addr_ % par.block_size)
        throw std::runtime_error(std::string("start_addr_ ") + std::to_string(r.start_addr_) + std::string(" is not muptiple to block_size ") +
                                 std::to_string(par.block_size));
}

class PageIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Page;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;

    PageIterator() = default;

    PageIterator(const CacheParams& par) : _par(par), _max_blocks(par.page_size / par.block_size), _begin_id(0), _end_id(0) {}

    PageIterator(const CacheParams& par, const Request& r)
        : _par(par),
          _max_blocks(par.page_size / par.block_size),
          _page(GetPage(par, r)),
          _begin_id(_page.id * _max_blocks + _page.block_id + _page.num_blocks),
          _end_id(r.start_addr_ / par.block_size + r.size_bytes_ / par.block_size) {}

    PageIterator(Page const& page, size_t end) : _page(page), _begin_id(page.id * _max_blocks + page.block_id + page.num_blocks), _end_id(end) {}

    Page& operator*() {
        return _page;
    }

    Page* operator->() {
        return &_page;
    }

    PageIterator& operator++() {
        _page = GetPage(_par, _begin_id, _end_id);
        _begin_id = _page.id * _max_blocks + _page.block_id + _page.num_blocks;
        return *this;
    }

    PageIterator operator++(int) {
        PageIterator tmp(*this);
        ++*this;
        return tmp;
    }

    bool operator==(PageIterator const& right) const {
        return _page.id == right._page.id && _page.block_id == right._page.block_id && _page.num_blocks == right._page.num_blocks;
    }

    bool operator!=(PageIterator const& right) const {
        return !(*this == right);
    }

private:
    const CacheParams _par = {};
    size_t _max_blocks = 0;
    Page _page = {};
    size_t _begin_id = 0;  // original block id (_not_ normalized to max page size)
    size_t _end_id = 0;
};

class LruCache {
public:
    void Init(const CacheParams&);
    ~LruCache() = default;

    Response Write(const Request&);
    Response Read(const Request&);

    Response Prefetch(const Request&);

private:
    void Verify(const Request&) const;

    struct IsFromPredictor {
        bool val;
    };

    struct NumReads {
        uint32_t val;
    };

    using page_w_blocks_t = std::unordered_map<uint32_t /*blk index*/, std::tuple<IsFromPredictor, NumReads>>;
    std::unique_ptr<lru_cache<size_t, page_w_blocks_t>> _cache;
    CacheParams _par;
    uint32_t _blocks_in_page;
};