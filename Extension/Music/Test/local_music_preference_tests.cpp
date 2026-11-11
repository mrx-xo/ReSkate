#include "Extension/Music/local_music_preference.h"
#include "Extension/Music/local_music_binding.h"
#include <Windows.h>
#include <cstring>
#include <iostream>

namespace {
int failures{};
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}
int main() {
    using namespace dingosdk;
    namespace pref = music_preference;
    const auto folder = std::filesystem::temp_directory_path() /
        ("reskate-music-preference-tests-" + std::to_string(GetCurrentProcessId()));
    const auto path = folder / "profile.sqlite3";
    const pref::Members a{{1}, {2}}, b{{3}, {4}};
    const pref::Choice wanted{"mod:mine", 13};
    {
        // Native-shaped private manager, one bucket, two playlist nodes and owned GUID arrays.
        std::array<std::byte, 0xe0> manager{};
        std::array<std::byte, 0x60> first{}, second{};
        std::array<std::uintptr_t, 2> buckets{};
        auto first_members = b, second_members = a;
        const auto address = [](const auto& value) { return reinterpret_cast<std::uintptr_t>(value.data()); };
        const auto put = [](auto& bytes, std::size_t offset, auto value) { std::memcpy(bytes.data() + offset, &value, sizeof(value)); };
        buckets[0] = address(first);
        put(manager, 0xa0, address(buckets)); put(manager, 0xa8, std::uint32_t{1});
        put(first, 0, std::uint32_t{12}); put(first, 0x58, address(second));
        put(first, 0x10, address(first_members)); put(first, 0x18, address(first_members) + first_members.size() * sizeof(pref::Guid));
        put(second, 0, std::uint32_t{35});
        put(second, 0x10, address(second_members)); put(second, 0x18, address(second_members) + second_members.size() * sizeof(pref::Guid));
        const std::array ranges{std::pair{address(manager), sizeof(manager)}, std::pair{address(first), sizeof(first)},
            std::pair{address(second), sizeof(second)}, std::pair{address(buckets), sizeof(buckets)},
            std::pair{address(first_members), first_members.size() * sizeof(pref::Guid)},
            std::pair{address(second_members), second_members.size() * sizeof(pref::Guid)}};
        auto read = [&](std::uintptr_t pointer, auto& value) {
            for (const auto& [base, size] : ranges)
                if (pointer >= base && pointer - base <= size && sizeof(value) <= size - (pointer - base)) {
                    std::memcpy(&value, reinterpret_cast<const void*>(pointer), sizeof(value)); return true;
                }
            return false;
        };
        check(pref::find_playlist(read, address(manager), a) == 35u, "native table resolves complete playlist membership");
        put(second, 0, std::uint32_t{198});
        check(pref::find_playlist(read, address(manager), a) == 198u, "fresh map playlist number is resolved instead of reusing an old ID");
        put(second, 0x18, address(second_members));
        check(!pref::find_playlist(read, address(manager), a), "empty or unavailable playlist does not match");
        put(second, 0x58, address(first));
        check(!pref::find_playlist(read, address(manager), a), "corrupt cyclic native chains terminate safely");
        put(manager, 0xa8, std::uint32_t{0xffffffff});
        check(!pref::find_playlist(read, address(manager), a), "invalid native capacity is refused");
    }
    pref::State state;
    state.publish({{"mod:mine", a}, {"stock", b}});
    state.select(13, a, false, true);
    check(!state.preference, "automatic map bindings never become a preference");
    state.select(13, a, true, false);
    check(!state.preference, "failed native binding readback is not saved");
    state.select(13, {{2}, {1}, {1}}, true, true);
    check(state.preference == wanted && state.dirty, "verified choice resolves stable identity regardless of order");
    check(state.wants(13) && !state.wants(5), "only the deliberately chosen player channel is restored");
    try {
        {
            profile::Store store(path);
            check(!pref::load(store), "old profiles have no forced playlist");
            pref::save(store, state.preference);
            const auto writes = profile::Store::changes();
            pref::save(store, state.preference);
            check(profile::Store::changes() == writes, "unchanged preferences do not rewrite the database");
        }
        {
            profile::Store reopened(path);
            check(pref::load(reopened) == wanted, "playlist and channel survive closing and reopening the database");
            state.preference = pref::load(reopened);
            state.before_transition();
            check(state.wants(13) && state.members().empty(), "transition retains intent but discards old runtime membership");
            state.publish({{"stock", b}});
            check(state.preference == wanted && state.members().empty(), "missing pack is never replaced by the stock playlist");
            state.publish({{"mod:mine", b}});
            check(state.members() == b, "reloaded playlist uses fresh catalog membership");
            check(!state.needs_catalog(false) && state.needs_catalog(true),
                "unresolved native binding refreshes even a nonempty partial catalog");
            state.select(13, a, true, true);
            check(!state.wants(13), "unknown deliberate choice suspends restoration of the previous choice");
            pref::save(reopened, state.preference);
            check(!pref::load(reopened), "unknown deliberate choice also releases the previous choice on restart");
            state.publish({{"new:playlist", a}, {"mod:mine", b}});
            check(state.preference == pref::Choice{"new:playlist", 13}, "delayed catalog resolves the new deliberate choice");
            state.select(13, {}, true, true, false, true);
            check(!state.preference && !state.wants(13), "deliberate external selection releases native persistence");
            pref::save(reopened, state.preference);
            check(!pref::load(reopened), "released preference persists");
            state.publish({{"one", a}, {"two", a}});
            state.select(13, a, true, true);
            check(!state.preference, "identical playlist memberships cannot invent a stable identity");
            state.select(13, {}, true, true, true);
            check(state.preference == pref::Choice{std::string(pref::favorites_id), 13}, "favorites have an explicit stable identity");
            for (const auto& invalid : {std::string("garbage"), std::string(R"({"version":1,"id":"mod:x","channel":999})"),
                     std::string(R"({"version":1,"id":"","channel":13})"), std::string(R"({"version":2,"id":"mod:x","channel":13})")}) {
                reopened.set_user_value(pref::profile_key, invalid);
                check(!pref::load(reopened), "malformed preference is ignored without applying an arbitrary channel");
                check(reopened.user_value(pref::profile_key) == Json(invalid), "loading invalid data does not overwrite it");
            }
        }
    } catch (const std::exception& error) { check(false, error.what()); }
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    if (failures) return 1;
    std::cout << "music preference: ok\n";
}
