// Copyright (c) 2014-2016 The Dash Core developers

#ifndef CACHEMAP_H_
#define CACHEMAP_H_

#include <map>
#include <list>
#include <cstddef>

#include "serialize.h"

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
    CacheMap(std::size_t nMaxSizeIn = 0)
          : nMaxSize(nMaxSizeIn),
            nCurrentSize(0),
            listKeys(),
            mapItems()
    {}

    void Clear()
    {
        mapItems.clear();
        listKeys.clear();
        nCurrentSize = 0;
    }

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

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nMaxSize);
        READWRITE(nCurrentSize);
        READWRITE(listKeys);
        READWRITE(mapItems);
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
