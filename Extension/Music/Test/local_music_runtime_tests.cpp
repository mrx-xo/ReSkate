// Exercise the production bind, update and native queue observation paths with private
// music-manager tables. Retail game code, assets and the installed profile are never used.
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>

namespace fixture {
std::uintptr_t base{};
bool screen{};
USHORT WINAPI frames(ULONG, ULONG count, PVOID* output, PULONG) {
    if (!screen || !count) return 0;
    output[0] = reinterpret_cast<void*>(base + 0x3df0b6e);
    return 1;
}
}
#undef CaptureStackBackTrace
#define CaptureStackBackTrace fixture::frames
#include "Extension/Music/local_music_playback.cpp"
#undef CaptureStackBackTrace
#include "Extension/Music/spotify_station.cpp"

namespace fixture {
std::vector<dingosdk::music_preference::Playlist> catalog;
int refreshes{}, failures{};
bool reject{}, race{};
std::thread competing_selector;
std::atomic<bool> started{};
std::array<std::uint64_t, 2> player_binding{}, speaker_binding{};
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::uintptr_t __fastcall bind(std::uintptr_t channel_pointer, std::uintptr_t playlist_pointer) {
    const auto channel = *reinterpret_cast<const std::uint32_t*>(channel_pointer);
    const auto playlist = *reinterpret_cast<const std::uint32_t*>(playlist_pointer);
    auto& binding = channel == 13 ? player_binding : speaker_binding;
    if (!reject) binding[0] = (std::uint64_t{playlist} << 32) | channel;
    if (race && channel == 13 && playlist == 12) {
        race = false;
        started.store(false);
        competing_selector = std::thread([] {
            started.store(true);
            (void)dingosdk::profile_runtime::ensure_preferred_binding(13);
        });
        while (!started.load()) std::this_thread::yield();
        Sleep(30); // Give the competing selector the old preference before verified capture.
    }
    return 0; // Native status is not a boolean; success is established by readback.
}
template<class Bytes, class T> void put(Bytes& bytes, std::size_t offset, T value) {
    std::memcpy(reinterpret_cast<std::byte*>(bytes.data()) + offset, &value, sizeof(value));
}
template<class Bytes> std::uintptr_t address(const Bytes& bytes) { return reinterpret_cast<std::uintptr_t>(bytes.data()); }
void tick() {
    dingosdk::profile_runtime::preference_next_poll.store(0);
    dingosdk::profile_runtime::update_music_preference();
}
void select(std::uint32_t channel, std::uint32_t playlist, bool manual) {
    screen = manual;
    dingosdk::spotify_station::set_music_screen(manual);
    dingosdk::profile_runtime::music_channel_playlist_bind_hook(
        reinterpret_cast<std::uintptr_t>(&channel), reinterpret_cast<std::uintptr_t>(&playlist));
    screen = false;
    dingosdk::spotify_station::set_music_screen(false);
}
}
namespace dingosdk::profile_runtime {
LocalRuntime& local_runtime() { static LocalRuntime runtime; return runtime; }
bool read_music_catalog(MusicCatalog&) {
    ++fixture::refreshes;
    publish_music_preferences(fixture::catalog);
    return true;
}
}
namespace dingosdk::spotify_rows {
std::string id_of(const unsigned char*) { std::abort(); }
bool add_member(std::uintptr_t, std::uintptr_t, std::uint32_t, const unsigned char*) noexcept {
    fixture::check(false, "external station mutation was unexpectedly invoked"); return false;
}
}
// Unrelated external-player services must never run in this native playlist fixture.
namespace dingosdk::music_inject {
void station_song_begins() noexcept { std::abort(); }
bool spatial_active() noexcept { std::abort(); }
}
namespace dingosdk::external_player {
void start() noexcept { std::abort(); }
Track now() { std::abort(); }
std::uint64_t revision() noexcept { std::abort(); }
void send(Command) noexcept { std::abort(); }
void use_app(const std::string&) { std::abort(); }
}
namespace dingosdk::spotify_cover {
void want(const std::vector<unsigned char>&, std::uint64_t) noexcept { std::abort(); }
}
namespace dingosdk::spotify_web {
void play(const std::string&, const std::string&) { std::abort(); }
State state() { std::abort(); }
}
namespace dingosdk::spotify_device {
bool connected() noexcept { std::abort(); }
bool in_use() noexcept { std::abort(); }
void activate() noexcept { std::abort(); }
void start_radio() noexcept { std::abort(); }
}
namespace dingosdk::external_audio {
std::string app() { std::abort(); }
bool on() noexcept { std::abort(); }
void set_on(bool) noexcept { std::abort(); }
}
namespace dingosdk::youtube_music {
bool on() noexcept { std::abort(); }
void set_on(bool) noexcept { std::abort(); }
void play(std::size_t, const std::string&) { std::abort(); }
}
namespace dingosdk::tricks::feed {
void song_playing(std::string_view, std::string_view) noexcept { std::abort(); }
}

int main() {
    using namespace dingosdk;
    using namespace fixture;
    namespace runtime = profile_runtime;
    namespace pref = music_preference;
    namespace layout = game::build::v20260929::local_music;
    auto* globals = static_cast<std::byte*>(VirtualAlloc(nullptr, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!globals) return 1;
    base = reinterpret_cast<std::uintptr_t>(globals) - layout::asset_manager;
    alignas(16) std::array<std::byte, 0x100> manager{};
    std::array<std::byte, 0x200> ui{};
    std::array<std::byte, 0x60> mod{}, stock{};
    std::array<std::uintptr_t, 2> playlists{address(mod), 0}, channels{address(player_binding), 0};
    pref::Members mod_members{{1}, {2}}, stock_members{{3}, {4}};
    const auto manager_address = address(manager), ui_address = address(ui);
    std::memcpy(globals, &manager_address, sizeof(manager_address));
    std::memcpy(globals + layout::ui_manager - layout::asset_manager, &ui_address, sizeof(ui_address));
    auto* critical = reinterpret_cast<CRITICAL_SECTION*>(manager.data() + layout::manager_lock);
    InitializeCriticalSection(critical);
    put(manager, 0xa0, address(playlists)); put(manager, 0xa8, std::uint32_t{1});
    put(manager, 0xc8, address(channels)); put(manager, 0xd0, std::uint32_t{1});
    player_binding = {13 | (std::uint64_t{12} << 32), address(speaker_binding)};
    speaker_binding = {5 | (std::uint64_t{12} << 32), 0};
    put(mod, 0, std::uint32_t{35}); put(mod, 0x58, address(stock));
    put(mod, 0x10, address(mod_members)); put(mod, 0x18, address(mod_members) + mod_members.size() * sizeof(pref::Guid));
    put(stock, 0, std::uint32_t{12});
    put(stock, 0x10, address(stock_members)); put(stock, 0x18, address(stock_members) + stock_members.size() * sizeof(pref::Guid));
    runtime::image_base.store(base);
    runtime::bind_channel_playlist = &bind;
    runtime::activate_music_playback();
    const auto folder = std::filesystem::temp_directory_path() / ("reskate-music-runtime-tests-" + std::to_string(GetCurrentProcessId()));
    runtime::local_runtime().store = std::make_unique<profile::Store>(folder / "profile.sqlite3");
    catalog = {{"mod:mine", mod_members}, {"stock", stock_members}};
    runtime::publish_music_preferences(catalog);
    select(13, 35, true);
    tick();
    check(pref::load(*runtime::local_runtime().store) == pref::Choice{"mod:mine", 13}, "production hook verifies and persists manual choice even with zero return status");
    select(13, 12, false);
    check(player_binding[0] >> 32 == 35, "automatic player binding stays on preferred playlist");
    select(5, 12, false);
    check(speaker_binding[0] >> 32 == 12 && spotify_station::plain() == 13, "speaker binding preserves its own playlist and cannot steal player tracking");
    reject = true;
    select(13, 12, true);
    reject = false;
    check(runtime::preference_state.preference == pref::Choice{"mod:mine", 13}, "failed manual readback retains the verified preference");
    race = true;
    select(13, 12, true);
    competing_selector.join();
    check(runtime::preference_state.preference == pref::Choice{"stock", 13} && player_binding[0] >> 32 == 12,
        "manual selection and capture are atomic against a concurrent restore");
    select(13, 35, true);
    runtime::music_preference_before_level_transition(3);
    put(mod, 0, std::uint32_t{198});
    runtime::publish_music_preferences(catalog);
    select(13, 12, false);
    check(player_binding[0] >> 32 == 198, "map change resolves a fresh native playlist ID");

    // A player queue which attempted playback waits while only part of a playlist is loaded.
    std::array<std::byte, 0x100> player_queue{}, speaker_queue{};
    put(player_queue, 0, std::uintptr_t{0x12340000}); put(speaker_queue, 0, std::uintptr_t{0x56780000});
    spotify_station::plain_song_chosen(address(player_queue), 13, false);
    runtime::publish_music_preferences({{"mod:mine", {{1}}}});
    runtime::preference_waiting.store(14);
    const auto before = refreshes;
    select(5, 12, false);
    spotify_station::plain_song_chosen(address(speaker_queue), 5, false);
    tick();
    check(refreshes > before && player_queue[0xf1] == std::byte{1} && speaker_queue[0xf1] == std::byte{0},
        "partial catalog refresh retries only the waiting player after a speaker binding");
    check(!spotify_station::game_next_on(5), "checked next rejects a different channel");
    player_queue[0xf1] = std::byte{0};
    runtime::preference_waiting.store(14);
    runtime::preference_song_selected(5, 0);
    runtime::preference_song_selected(13, -1);
    check(runtime::preference_waiting.load() == 14, "failed or unrelated selection cannot cancel the retry");
    runtime::preference_song_selected(13, 0);
    tick();
    check(player_queue[0xf1] == std::byte{0} && !runtime::preference_waiting.load(),
        "native recovery cancels the pending retry instead of skipping the new song");
    spotify_station::before_level_transition();
    check(!spotify_station::game_next_on(13) && player_queue[0xf1] == std::byte{0}, "transition invalidates even a still-readable old queue");

    put(mod, 0x18, address(mod_members));
    select(13, 198, true);
    check(runtime::pending_native_choice.has_value(), "unready manual playlist keeps a deferred native observation");
    put(mod, 0x18, address(mod_members) + mod_members.size() * sizeof(pref::Guid));
    tick(); tick();
    check(pref::load(*runtime::local_runtime().store) == pref::Choice{"mod:mine", 13}, "deferred native membership eventually resolves and persists");
    put(ui, layout::ui_favorites_playlist, std::uint32_t{198});
    select(13, 198, true);
    check(runtime::preference_state.preference == pref::Choice{std::string(pref::favorites_id), 13}, "Favorites is recognized by its native ID as well as zero alias");
    runtime::local_runtime().store.reset();
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    DeleteCriticalSection(critical);
    VirtualFree(globals, 0, MEM_RELEASE);
    if (failures) return 1;
    std::cout << "music runtime: ok\n";
}
