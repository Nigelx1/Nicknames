# Nicknames

A [Geode](https://geode-sdk.org) mod for Geometry Dash that changes how other players show up in your game. Give someone a nickname once and it replaces their username everywhere you'd normally see it.

Nothing leaves your game. The other person never finds out, nobody else sees it, and nothing is sent anywhere. It's a label on your screen and only your screen.

## Where the nickname shows up

- **Level leaderboards**: top 100, friends, weekly, all of them
- **Comments**: level comments and profile posts
- **The friends list and user search**
- **Their profile**, with their real username in small gray text underneath, so you always know who you're actually looking at

## Renaming someone

Open their profile and press the pencil button in the left-hand column, with the info and chat buttons. Type a nickname and press **Save**.

To undo it, open the same popup and press **Reset**. They go back to their real username right away.

Nicknames are saved by account ID, not by username, so a nickname survives the other person changing their GD name.

Only the text on screen changes. Tapping a name still opens the right profile, and messages, friend requests, scores and comments all stay attached to the real account.

## Platforms

One `.geode` file covers Windows, macOS, iOS and Android (64 and 32-bit), for Geometry Dash 2.2081 with Geode 5.10.1 or a later 5.x. Windows has been tested in game. The macOS, iOS and Android builds come from the same source but haven't been tried on a device yet.

Nicknames needs Geode's own **Node IDs** mod (`geode.node-ids`), which most Geode players already have. If Geode says a dependency is missing, install Node IDs from Geode's mod list.

## Installing

1. Get `nigelx1.nicknames.geode`: from [Releases](https://github.com/Nigelx1/Nicknames/releases) once there is one, or from the newest successful run under [Actions](https://github.com/Nigelx1/Nicknames/actions), where the **Nicknames (all platforms)** artifact is a zip with the `.geode` inside (GitHub asks you to sign in for that).
2. In the game, open Geode's mod list, press the gear for Geode's settings, press **Install From File** and pick the file. (Or put the file in Geode's mods folder yourself: **Open Mods Folder** is in the same place.)
3. Restart the game.

## Credits

- **Nigelx1**: the idea, every design call, and every round of "that's not quite right yet" that got it here
- **Claude** (Anthropic): wrote the code, and this page. Not "AI-assisted": the actual author of the source, and Nigel wants that said plainly
- **cvolton**: [BetterInfo](https://github.com/cvolton/betterinfo-geode) is the reason the profile button works. Reading how a real mod adds a button to that column settled it in one go

## License

MIT. See [LICENSE](LICENSE).
