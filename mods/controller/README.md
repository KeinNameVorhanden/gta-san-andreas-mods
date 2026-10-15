# XInput controller

Xbox-style (XInput) controllers on the 1.0 US build, which only knows DirectInput joysticks. The Steam build
reads XInput controllers itself, so the mod does nothing there. Layout as on the console: A = Cross, X = Square,
Y = Triangle, B = Circle, bumpers = L1/R1, triggers = L2/R2, Back = Select, Start, stick clicks and D-pad.
Keyboard and mouse keep working at the same time. Settings: controller number and stick dead zone.

- Build: `cmake --build --preset x86-release --target controller`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod controller --apply`
- Package: `python tools/package_mod.py controller`
- Settings: `sacore\config\mods\controller.toml` (declared in `mod.toml [config]`)
