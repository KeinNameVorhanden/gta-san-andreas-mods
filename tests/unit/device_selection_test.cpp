#include "stability/stability.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using sacore::PickVideoModeEntry;

namespace {

// Entries as the game's Device Selection dialog shows them (text copied from a real dialog): every size in 16 and
// 32 bit with the aspect class after it.
const std::vector<std::string> kDialogList = {
    "640 x 480 x 16 (FULLSCREEN)",   "800 x 600 x 16 (FULLSCREEN)",   "1280 x 720 x 16 (WIDESCREEN)",
    "1280 x 1024 x 16 (FULLSCREEN)", "1920 x 1080 x 16 (WIDESCREEN)", "640 x 480 x 32 (FULLSCREEN)",
    "800 x 600 x 32 (FULLSCREEN)",   "1280 x 720 x 32 (WIDESCREEN)",  "1280 x 1024 x 32 (FULLSCREEN)",
    "1920 x 1080 x 32 (WIDESCREEN)", "1920 x 1200 x 32 (WIDESCREEN)",
};

}  // namespace

TEST_CASE("device selection: a size without depth takes the deepest entry offered") {
    CHECK(PickVideoModeEntry(kDialogList, "1280x720") == 7);
    CHECK(PickVideoModeEntry(kDialogList, "1920x1080") == 9);  // not 1920 x 1200
    CHECK(PickVideoModeEntry(kDialogList, "1920 x 1200") == 10);
}

TEST_CASE("device selection: an explicit depth picks exactly that entry") {
    CHECK(PickVideoModeEntry(kDialogList, "1280x720x16") == 2);
    CHECK(PickVideoModeEntry(kDialogList, "1920X1200x16") == -1);  // only offered in 32 bit
}

TEST_CASE("device selection: sizes not offered and malformed settings leave the default") {
    CHECK(PickVideoModeEntry(kDialogList, "1366x768") == -1);
    CHECK(PickVideoModeEntry(kDialogList, "") == -1);
    CHECK(PickVideoModeEntry(kDialogList, "1280") == -1);
    CHECK(PickVideoModeEntry(kDialogList, "widescreen 1280x720") == -1);
    CHECK(PickVideoModeEntry({}, "1280x720") == -1);
}
