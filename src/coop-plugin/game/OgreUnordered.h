// OgreUnordered.h - the game's hash containers.
//
// GameHashMap<K, V>::type / GameHashSet<T>::type name boost's unordered containers with
// Ogre's general-category allocator: the exact container type the game embeds (0x40 bytes each, asserted
// in game/layout_asserts.inl). Only this small wrapper is ours - boost and Ogre stay third-party headers.
#pragma once

#include <ogre/OgreMemoryAllocatorConfig.h>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

template <typename K, typename V>
struct GameHashMap
{
    typedef boost::unordered::unordered_map<K, V, boost::hash<K>, std::equal_to<K>,
        Ogre::STLAllocator<std::pair<K const, V>, Ogre::CategorisedAllocPolicy<Ogre::MEMCATEGORY_GENERAL> > > type;
};

template <typename T>
struct GameHashSet
{
    typedef boost::unordered::unordered_set<T, boost::hash<T>, std::equal_to<T>,
        Ogre::STLAllocator<T, Ogre::CategorisedAllocPolicy<Ogre::MEMCATEGORY_GENERAL> > > type;
};
