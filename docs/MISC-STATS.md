**Yes, Starfield has built-in support for viewing and editing many of these miscellaneous integer-based character stats** (often called "misc stats" or crime stats in the community). These include things like murders/people killed, assaults, items stolen/thefts, bounties, and similar counters. Other stats like quests completed, planets surveyed, or creatures scanned are partially covered through related systems or tracking tools, but not always as simple exposed integers.

Here's a breakdown of the best options from mods, console commands, Reddit, Nexus Mods, Bethesda Creations, and other sources.

### 1. Console Commands (Most Direct & Flexible Method)
Starfield supports `ModPCMiscStat` (shorthand: `ModPCMS`) and `GetPCMiscStat` (`GetPCMS`). These let you query or adjust integer misc stats directly in-game.

**How to use:**
- Press `~` (tilde) to open the console.
- Commands are usually targeted at the player (no need for `player.` prefix in most reports).
- **Query a stat**: `GetPCMiscStat "Stat Name"` or `GetPCMS "Stat Name"`
- **Modify a stat**: `ModPCMiscStat "Stat Name" <amount>` (positive = add, negative = subtract) or shorthand `ModPCMS "Stat Name" <amount>`

**Examples from community reports (especially for crime stats):**
- `modpcms "Murders" -5` → Reduce murders by 5
- `modpcms "Assaults" -3`
- `modpcms "Largest Bounty" -850`
- `modpcms "Total Lifetime Bounty" -1500`
- Similar patterns work for other crime-related stats (there are reportedly ~12 crime misc stats total, including thefts/items stolen, etc.).

**Common/confirmed stat names** (string-based, case-sensitive in quotes):
- "Murders" (people killed / wrongful kills)
- "Assaults"
- "Items Stolen" or theft-related
- "Locks Picked"
- "Largest Bounty"
- "Total Lifetime Bounty"

**Tips for discovery**:
- Use `GetPCMiscStat` with guessed names to test.
- Many stats mirror Skyrim/Fallout patterns (e.g., "Items Stolen", bounty stats).
- These changes are generally permanent in the save.

**Sources & discussions**:
- Reddit threads in r/Starfield and r/starfieldmods (e.g., "console command to remove crime stats", "ModPCMiscStat examples").
- Comprehensive command lists on GitHub gists and starfieldcheats.com.
- Starfield Wiki / Fandom help command pages confirm the commands exist.

**Caveats**: Console use can disable achievements (common Bethesda behavior; some mods or restarts may re-enable them). Always back up your saves first (`Documents\My Games\Starfield\Saves`).

### 2. Dedicated Mods for Easier or Safer Editing
- **Virtual Crime Absolution** (Bethesda Creations by S1nderion)  
  Excellent dedicated tool. Adds in-game gameplay options/menu to reduce any of the 12 crime miscellaneous stats by chosen amounts (e.g., "-4 Assaults"). Perfect for roleplaying "lawful good" characters or cleaning up stats without console commands. Simple and immersive.

- **Wrongful Murders Stat Fix** (Nexus Mods)  
  Prevents certain kills (e.g., robots/turrets in specific quests like on The Beagle or The Facility) from incorrectly counting toward your murder/crime stats. Includes community notes on using `modpcms murders -x` for manual cleanup.

Other Nexus mods focus more on UI/stats display or general fixes rather than direct editing of these integers.

### 3. Tracking & Companion Tools (Great for Viewing)
- **Starfield Eye** (Free web app by ManApart, GitHub + hosted at manapart.github.io/starfield-eye)  
  Highly recommended companion tool. It tracks and displays:
  - Misc stats (general)
  - Quest progress + latest objectives
  - Some achievement progress
  - Stars discovered, planets landed on
  - Fauna/flora scan percentages
  - Personal codex-style tracking for flora, fauna, and planets
  - Outpost resources, etc.

  It's excellent for planets surveyed, creatures scanned, and exploration-related integers/percentages. Data is player-tracked or parsed externally (web-based, no server uploads). Very useful alongside console editing.

Other tools exist for quest tracking or save metadata, but this one stands out for misc/exploration stats.

### 4. Save File Editing (Advanced)
- **Starfield Save Tool** (Nexus Mods)  
  Decompresses `.sfs` save files into JSON metadata for inspection. Experimental support for writing changes back exists but is not fully reliable or documented for player stats/misc data.  
  **Best for**: Reverse engineering or advanced users who want to inspect raw data. Not the easiest for casual stat editing.  
  Location of saves: `Documents\My Games\Starfield\Saves`.  
  GitHub repo available for the tool (Nexus-Mods/StarfieldSaveTool). Community notes emphasize backing up and caution due to partial reverse-engineering of the format.

General Reddit discussions (r/Starfield) exist about save manipulation for progression (quests, exploration, etc.), but most recommend console or mods over raw editing.

### Additional Resources
- **Reddit**: r/Starfield (console commands, crime stats resets) and r/starfieldmods (mod recommendations, SFSE discussions, save tools). Search terms like "ModPCMiscStat", "crime stats console", or "misc stats" yield good threads with examples.
- **Nexus Mods**: Search "crime", "stats", or browse the Save Tool page and related articles. Mod comment sections often have command examples.
- **Bethesda Creations**: Official-ish platform for mods like Virtual Crime Absolution.
- **Wikis**: Starfield Wiki (Fandom) and starfieldwiki.net have console command references.
- **Other**: Steam discussions and GitHub gists for full command dumps. SFSE (Starfield Script Extender) is widely recommended as a base for enhanced modding/console functionality.

### Quick Notes on Your Specific Examples
- **People killed / Items stolen / Assaults / Bounties**: Fully supported via the misc/crime stats + console or Virtual Crime Absolution.
- **Quests completed**: Partially trackable via quest log or Starfield Eye app. A direct misc stat may exist but is less commonly referenced.
- **Planets surveyed / Creatures scanned**: Best handled via the survey system + **Starfield Eye** app (scan % per planet, personal codex). These seem more tied to exploration/achievement tracking than simple top-level misc integers.

**Recommendation**: Start with the console commands for precision (backup saves first) or install **Virtual Crime Absolution** if you mainly want to adjust crime stats cleanly. Pair with **Starfield Eye** for great visibility into exploration stats.

If you run into specific stat names that don't work or need help with a particular value (e.g., exact name for "planets surveyed"), provide more details and I can dig further. Experiment safely!