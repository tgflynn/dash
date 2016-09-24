// Copyright (c) 2014-2016 The Dash Core developers

#ifndef CACHEMULTIMAP_H_
#define CACHEMULTIMAP_H_

#include <cstddef>
#include <map>
#include <list>
#include <set>

#include "serialize.h"

#include "cachemap.h"

/**
 * Map like container that keeps the N most recently added items
 */
template<typename K, typename V>
class CacheMultiMap
{
public:
    typedef CacheItem<K,V> item_t;

    typedef std::list<item_t> list_t;

    typedef typename list_t::iterator list_it;

    typedef typename list_t::const_iterator list_cit;

    typedef std::list<list_it> it_list_t;

    typedef typename it_list_t::iterator it_list_it;

    typedef typename it_list_t::const_iterator it_list_cit;

    typedef std::map<V,list_it> it_map_t;

    typedef typename it_map_t::iterator it_map_it;

    typedef typename it_map_t::const_iterator it_map_cit;

    typedef std::map<K, it_map_t> map_t;

    typedef typename map_t::iterator map_it;

    typedef typename map_t::const_iterator map_cit;

    typedef std::pair<K, V> pair_t;

    typedef std::map<list_it,map_it> cross_m_t;

    typedef typename cross_m_t::iterator cross_m_it;

    typedef typename cross_m_t::const_iterator cross_m_cit;

private:
    std::size_t nMaxSize;

    std::size_t nCurrentSize;

    list_t listItems;

    map_t mapIndex;

    cross_m_t mapCrossIndex;

public:
    CacheMultiMap(std::size_t nMaxSizeIn = 0)
        : nMaxSize(nMaxSizeIn),
          nCurrentSize(0),
          listItems(),
          mapIndex(),
          mapCrossIndex()
    {}

    CacheMultiMap(const CacheMap<K,V>& other)
        : nMaxSize(other.nMaxSize),
          nCurrentSize(other.nCurrentSize),
          listItems(other.listItems),
          mapIndex(),
          mapCrossIndex()
    {
        RebuildIndex();
    }

    void Clear()
    {
        mapCrossIndex();
        mapIndex.clear();
        listItems.clear();
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
        if(nCurrentSize == nMaxSize) {
            PruneLast();
        }
        listItems.push_front(item_t(key, value));
        list_it lit = listItems.begin();
        map_it mit = mapIndex.find(key);
        if(mit == mapIndex.end()) {
            mit = mapIndex.insert(std::pair<K,it_map_t>(key, it_map_t())).first;
        }
        it_map_t& mapIt = mit->second;
        mapIt[value] = lit;
        ++nCurrentSize;
    }

    bool HasKey(const K& key) const
    {
        map_cit it = mapIndex.find(key);
        return (it != mapIndex.end());
    }

    bool Get(const K& key, V& value) const
    {
        map_cit it = mapIndex.find(key);
        if(it == mapIndex.end()) {
            return false;
        }
        const it_map_t& mapIt = it->second;
        const item_t& item = *(mapIt.begin()->second);
        value = item.value;
        return true;
    }

    bool GetAll(const K& key, std::vector<V>& vecValues)
    {
        map_cit mit = mapIndex.find(key);
        if(mit == mapIndex.end()) {
            return false;
        }
        const it_map_t& mapIt = mit->second;

        for(it_map_cit it = mapIt.begin(); it != mapIt.end(); ++it) {
            const item_t& item = *(it->second);
            vecValues.push_back(item.value);
        }
        return true;
    }

    void Erase(const K& key)
    {
        map_it mit = mapIndex.find(key);
        if(mit == mapIndex.end()) {
            return;
        }
        it_map_t& mapIt = mit->second;

        for(it_map_it it = mapIt.begin(); it != mapIt.end(); ++it) {
            listItems.erase(it->second);
            --nCurrentSize;
        }

        mapIndex.erase(mit);
    }

    const list_t& GetItemList() const {
        return listItems;
    }

    CacheMap<K,V>& operator=(const CacheMap<K,V>& other)
    {
        nMaxSize = other.nMaxSize;
        nCurrentSize = other.nCurrentSize;
        listItems = other.listItems;
        RebuildIndex();
        return *this;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nMaxSize);
        READWRITE(nCurrentSize);
        READWRITE(listItems);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void PruneLast()
    {
        if(nCurrentSize < 1) {
            return;
        }
        list_it lit = listItems.end();
        --lit;
        item_t& item = *lit;

        map_it mit = mapIndex.find(item.key);
        it_map_t& mapIt = mit->second;

        mapIt.erase(item.value);

        if(mapIt.size() < 1) {
            mapIndex.erase(item.key);
        }

        listItems.pop_back();
        --nCurrentSize;
    }

    void RebuildIndex()
    {
        mapIndex.clear();
        for(list_it lit = listItems.begin(); lit != listItems.end(); ++lit) {
            item_t& item = *lit;
            map_it mit = mapIndex.find(item.key);
            if(mit == mapIndex.end()) {
                mit = mapIndex.insert(std::pair<K,it_map_t>(item.key, it_map_t())).first;
            }
            it_map_t& mapIt = mit->second;
            mapIt[item.value] = lit;
        }
    }
};

#endif /* CACHEMAP_H_ */
