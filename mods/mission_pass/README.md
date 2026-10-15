# Mission pass

Passes the current story mission by key (default F6, press twice within 3 s). Settings in the F11 console.

How it passes, in this order:

1. **Debug skip.** Almost every story mission still contains Rockstar's test branch "S key pressed: go to the pass
   code". The mod answers that check with "pressed", only for the mission thread. The mission then runs its own pass
   code (MISSION PASSED, respect, next contact, unlocks) and its normal cleanup. Some missions also move CJ, as in
   Rockstar's testing (Big Smoke puts CJ in Grove Street).
2. **Pass-code jump.** If the debug skip is off (`debug_skip = false`) or the mission does not reach its check within
   3 s, the mission thread is sent into its pass code, the way the game itself unwinds a mission when CJ is wasted or
   busted. The mission is identified by a checksum and the bytes at the target are checked first; on a modified
   main.scm nothing is written.

**Story runner**: choose a mission in the F11 console (`run_to`, 101 missions from Big Smoke to End of the Line, or "Story complete").
From a new game, a loaded save or free roam the runner plays the story in order through the game's own trigger
threads and stops next to the chosen mission's marker (`save_to` also saves that point into a menu slot):

- it walks CJ into each contact marker (inside the interior for Woozie's office, Caligula's and the driving school)
  and sets the clock for missions with a time window;
- it answers the contacts' phone calls the way the game does when the answer button is pressed;
- it passes each mission at its start: the pass code, a researched entry for missions without one (Catalina's jobs,
  King in Exile, Jizzy, the lowrider meeting, the finale's first parts), Rockstar's race skip for the races (CJ is
  put on each checkpoint in turn, so the race is won for real) and it lets Wu Zi Mu's cutscene stages play;
- it does what the game needs between missions: a Voodoo lowrider for the lowrider meeting and Wu Zi Mu, the money
  for the property the story needs (Zero's RC shop, Wang Cars, the airstrip; on the for-sale sign the runner presses the answer phone key, `buy_key`, Tab by default), Millie's
  keycard for the casino heist, enough respect for Los Sepulcros and Los Desperados, and Grove Street territory
  above 34 % for End of the Line (21 Los Santos gang zones set to Grove, what winning their gang wars does).

**Story saves**: on the way the runner saves every story point it passes (`save_each`, one save per mission, kept
apart from your menu, quick and auto saves). Choosing a mission you are already past, or choosing one from a fresh
game, then loads its story save at once (`load_story_saves`); from any other game the runner plays on, so your own
money, stats and side progress stay. Story saves of another install of the same game build (for example a copy the
runner has played through) can be copied over with `python tools/saves.py --game <install> story-copy --from <other>`.

Optional strands are included where the story allows them (driving school, Zero, Wang Cars, the heist), so every
story mission is a target. Cutscenes are skipped automatically during the run.

The opening "In the Beginning" has no pass code: once CJ has control, the key finishes it through its own end (the
Big Smoke marker in Grove Street appears).

Limits: the mission body that was skipped never ran, so CJ stays where he is and payouts that depend on how the
mission went pay what the mission had counted so far. Cutscene-only missions, races with several finish lines (High
Stakes Low-rider, BMX) and odd jobs cannot be passed by key.

- Build: `cmake --build --preset x86-release --target mission_pass`
- Deploy to a test copy: `python tools/deploy_dev.py --game <test copy> --mod mission_pass --apply`
- Mission table: `python tools/gen_mission_table.py --missions <mission list> --scm <game>/data/script/main.scm`
  (verifies every row against the game's script)
