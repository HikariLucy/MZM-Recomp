#include "launcher_state.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "mzm-launcher-state-test";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto rom = root / "owned.gba";
    const auto bios = root / "owned.bin";
    { std::ofstream out(rom); out << "test"; }
    { std::ofstream out(bios); out << "test"; }

    mzm::LauncherState state;
    assert(!state.ready());
    assert(!state.save(root / "missing" / "launcher.ini"));
    state.rom = rom.string();
    state.bios = bios.string();
    assert(state.ready());
    assert(state.save(root / "launcher.ini"));
    auto loaded = mzm::LauncherState::load(root / "launcher.ini");
    assert(loaded.rom == state.rom && loaded.bios == state.bios);
    assert(loaded.ready());
    loaded.rom = (root / "absent.gba").string();
    assert(!loaded.ready());
    assert(mzm::validate_game_file(rom) == "Unsupported game file (expected USA revision 0).");
    assert(mzm::validate_bios(bios) == "Invalid GBA BIOS (expected 16 KB).");
    loaded.rom = rom.string();
    loaded.bios = (root / "absent.bin").string();
    assert(!loaded.ready());
    assert(mzm::resolve_game_config(root / "MZMRecomp") == root / "configs/mzm-us.toml" ||
           mzm::resolve_game_config(root / "MZMRecomp") == root.parent_path() / "configs/mzm-us.toml");
    fs::remove_all(root);
}
