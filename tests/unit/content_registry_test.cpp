#include "content/content_registry.h"

#include <doctest/doctest.h>

using sacore::ContentRegistry;
using sacore::ContentSource;

TEST_CASE("game paths normalize the way the game spells them, and never escape the game folder") {
    const std::filesystem::path game = "E:\\Games\\GTA SA";
    CHECK(ContentRegistry::NormalizeGamePath("DATA\\GTA.DAT", game) == "data\\gta.dat");
    CHECK(ContentRegistry::NormalizeGamePath(".\\data/../stream.ini", game) == "stream.ini");
    CHECK(ContentRegistry::NormalizeGamePath("E:\\Games\\GTA SA\\models\\gta3.IMG", game) == "models\\gta3.img");
    CHECK(ContentRegistry::NormalizeGamePath("e:/games/gta sa/text/american.gxt", game) == "text\\american.gxt");
    CHECK_FALSE(ContentRegistry::NormalizeGamePath("E:\\Games\\GTA SA2\\stream.ini", game));  // sibling folder
    CHECK_FALSE(ContentRegistry::NormalizeGamePath("C:\\Users\\x\\Documents\\gta_sa.set", game));
    CHECK_FALSE(ContentRegistry::NormalizeGamePath("..\\outside.txt", game));
}

TEST_CASE("the later mod in load order wins a shared file and the conflict names both owners") {
    ContentRegistry registry;
    const std::filesystem::path a = "E:\\G\\sacore\\mods\\a\\content";
    const std::filesystem::path b = "E:\\G\\sacore\\mods\\b\\content";
    registry.Build({
        ContentSource{"a", a, {a / "DATA" / "handling.cfg", a / "stream.ini"}},
        ContentSource{"b", b, {b / "data" / "HANDLING.CFG"}},
    });
    REQUIRE(registry.Find("data\\handling.cfg"));
    CHECK(registry.Find("data\\handling.cfg")->owner == "b");
    CHECK(registry.Find("stream.ini")->owner == "a");
    REQUIRE(registry.Conflicts().size() == 1);
    CHECK(registry.Conflicts()[0].replacedOwner == "a");
    CHECK(registry.Conflicts()[0].winner == "b");
    CHECK_FALSE(registry.Find("data\\gta.dat"));
}
