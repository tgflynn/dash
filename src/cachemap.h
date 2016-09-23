// Copyright (c) 2014-2016 The Dash Core developers

#ifndef CACHEMAP_H_
#define CACHEMAP_H_

#include <map>
#include <list>
#include <cstddef>

/**
 * Map like container that keeps the N most recently added items
 */
template<typename K, typename V>
class CacheMap
{
public:
    typedef std::list<K> list_t;

    typedef typename list_t::iterator list_it;

    typedef typename list_t::const_iterator list_cit;

    typedef std::map<K, V> map_t;

    typedef typename map_t::iterator map_it;

    typedef typename map_t::const_iterator map_cit;

private:
    std::size_t nMaxSize;

    std::size_t nCurrentSize;

    list_t listKeys;

    map_t mapItems;

public:
    CacheMap(std::size_t nMaxSizeIn) :
            nMaxSize(nMaxSizeIn),
            nCurrentSize(0),
            listKeys(),
            mapItems()
    {}

    void SetMaxSize(std::size_t nMaxSizeIn)
    {
        nMaxSize = nMaxSizeIn;
    }

    std::size_t GetMaxSize() const {
        return nMaxSize;
    }

    std::size_t GetSize() const {
        return nCurrentSize;
    }

    void Insert(const K& key, const V& value)
    {
        map_it it = mapItems.find(key);
        if(it != mapItems.end()) {
            it->second = value;
            return;
        }
        if(nCurrentSize == nMaxSize) {
            PruneLast();
        }
        mapItems[key] = value;
        listKeys.push_front(key);
        ++nCurrentSize;
    }

    bool HasKey(const K& key) const
    {
        map_cit it = mapItems.find(key);
        return (it != mapItems.end());
    }

    bool Get(const K& key, V& value) const
    {
        map_cit it = mapItems.find(key);
        if(it == mapItems.end()) {
            return false;
        }
        value = it->second;
        return true;
    }

private:
    void PruneLast()
    {
        if(nCurrentSize < 1) {
            return;
        }
        K keyLast = listKeys.back();
        mapItems.erase(keyLast);
        listKeys.pop_back();
        --nCurrentSize;
    }
};

#endif /* CACHEMAP_H_ */
