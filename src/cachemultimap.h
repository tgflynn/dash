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

    typedef std::map<K, it_list_t> map_t;

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
            std::pair<map_it,bool> ret = mapIndex.insert(std::pair<K,it_list_t>(key, it_list_t()));
            mit = ret.first;
        }
        it_list_t& listIt = mit->second;
        listIt.push_front(lit);
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
        const it_list_t& listIt = it->second;
        const item_t& item = *(listIt.front());
        value = item.value;
        return true;
    }

    bool GetAll(const K& key, std::vector<V>& vecValues)
    {
        map_cit mit = mapIndex.find(key);
        if(mit == mapIndex.end()) {
            return false;
        }
        it_list_t& listIt = mit->second;

        for(it_list_it it = listIt.begin(); it != listIt.end(); ++it) {
            item_t& item = *it;
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
        it_list_t& listIt = mit->second;

        for(it_list_it it = listIt.begin(); it != listIt.end(); ++it) {
            listItems.erase(*it);
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
        it_list_t& listIt = mit->second;

        for(it_list_it it = listIt.begin(); it != listIt.end(); ++it) {
            if(*it == lit) {
                listIt.erase(it);
                break;
            }
        }

        int nCount = 0;
        for(it_list_it it = listIt.begin(); it != listIt.end(); ++it) {
            if(nCount > 0) {
                break;
            }
            ++nCount;
        }

        if(nCount == 0) {
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
                mit = mapIndex.insert(std::pair<K,it_list_t>(item.key, it_list_t())).first;
            }
            mit->second.push_front(lit);
        }
    }
};

#endif /* CACHEMAP_H_ */
