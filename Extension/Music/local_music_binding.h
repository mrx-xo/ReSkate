#pragma once
#include "local_music_preference.h"
#include "Engine/Game/Build/20260929/local_music.h"
#include <limits>

namespace dingosdk::music_preference {
// Read is the runtime's guarded memory reader. The same traversal is exercised with private
// native-shaped tables in tests. Nothing borrowed from the manager survives this call.
template<class Read> bool read_members(Read read, std::uintptr_t node, Members& out) {
    namespace layout = game::build::v20260929::local_music;
    std::uintptr_t begin{}, end{};
    out.clear();
    if (node < 0x10000 || node > UINTPTR_MAX - layout::playlist_next ||
        !read(node + layout::playlist_members_begin, begin) ||
        !read(node + layout::playlist_members_end, end) || begin < 0x10000 || end <= begin ||
        (end - begin) % sizeof(Guid) || (end - begin) / sizeof(Guid) > 8192) return false;
    out.resize((end - begin) / sizeof(Guid));
    for (std::size_t i = 0; i < out.size(); ++i)
        if (!read(begin + i * sizeof(Guid), out[i])) { out.clear(); return false; }
    canonicalize(out);
    return true;
}
template<class Read> std::optional<std::uint32_t> find_playlist(Read read, std::uintptr_t manager, const Members& wanted) {
    namespace layout = game::build::v20260929::local_music;
    if (wanted.empty() || manager < 0x10000 || manager > UINTPTR_MAX - layout::playlist_capacity) return {};
    std::uintptr_t buckets{}, sentinel{};
    std::uint32_t capacity{};
    if (!read(manager + layout::playlist_buckets, buckets) || buckets < 0x10000 ||
        !read(manager + layout::playlist_capacity, capacity) || !capacity || capacity > 4096 ||
        buckets > UINTPTR_MAX - (static_cast<std::uintptr_t>(capacity) + 1) * 8 ||
        !read(buckets + capacity * 8ULL, sentinel)) return {};
    Members actual;
    std::size_t visited{};
    for (std::uint32_t bucket = 0; bucket < capacity; ++bucket) {
        std::uintptr_t node{};
        if (!read(buckets + bucket * 8ULL, node)) return {};
        while (node && node != sentinel) {
            if (++visited > 8192 || node < 0x10000 || node > UINTPTR_MAX - layout::playlist_next) return {};
            std::uint32_t id{};
            std::uintptr_t next{};
            if (!read(node, id) || !read(node + layout::playlist_next, next)) return {};
            if (id && read_members(read, node, actual) && actual == wanted) return id;
            node = next;
        }
    }
    return {};
}
}
