# stream-notifier

Simple daemon app that polls your Twitch follow list and sends desktop notificatons when streamers go online.

Uses libnotify, so should be compliant with any DE supporting Freedesktop notification standard.

# Requirements

- glib
- libnotify
- libctwitch (https://github.com/cog1to/libctwitch)
- pkg-config for searching dependencies

# Building

Standard Cmake build. Something like

```
mkdir build
cd build
cmake ..
make
```

# Installing

From the build dir:

`make install`

# Usage

`# stream-notifier <your-twitch-username>`

OR

```
# stream-notifier <your-twitch-username> -now
ESL_SC2
   Game: StarCraft II
   Status: RERUN: INnoVation [T] vs. RagnaroK [Z] - Group C Round 4 - IEM Katowice 2019
   URL: https://www.twitch.tv/esl_sc2
GOGcom
   Game: Sally Face
   Status: DanVanDam plays the twisted Sally Face [Blind]
   URL: https://www.twitch.tv/gogcom
```

# Configuration

Only through source code so far. Not sure if it needs something more elaborate at this point.

# TODO

Proper daemon ifrastructure:

- PID lock file
- Logging (at least to system logs)

To research/add:

- Will be nice to try to download and show streamers icons in notification.
- Notification actions. Ideally customizable.

