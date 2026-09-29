#pragma once

// Short names for the standard containers that allocate through a std::pmr::memory_resource.
//
// A pmr container takes the resource in its constructor and uses it for all of its memory:
//
//   PmrVector<Vec2> points(app.GetFrameAllocator()); // memory comes from this frame's arena
//   points.push_back({1.0f, 2.0f});
//
// Without an argument they use std::pmr::get_default_resource() (normally new/delete).
// Note: the resource must outlive the container, and assigning containers with different
// resources copies the elements instead of stealing the memory.

#include <deque>
#include <map>
#include <memory_resource>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Emerald {

template <typename T> using PmrVector = std::pmr::vector<T>;

template <typename T> using PmrDeque = std::pmr::deque<T>;

using PmrString = std::pmr::string;

template <typename Key, typename Value, typename Hash = std::hash<Key>,
          typename Equal = std::equal_to<Key>>
using PmrUnorderedMap = std::pmr::unordered_map<Key, Value, Hash, Equal>;

template <typename Key, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
using PmrUnorderedSet = std::pmr::unordered_set<Key, Hash, Equal>;

template <typename Key, typename Value, typename Compare = std::less<Key>>
using PmrMap = std::pmr::map<Key, Value, Compare>;

} // namespace Emerald
