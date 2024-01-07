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

# Setting up

Since Twitch has deprecated the old app API, you now need to obtain a user
access token for the daemon to work. You can do that by opening an URL in a
browser:

```
https://id.twitch.tv/oauth2/authorize?response_type=token&client_id=454q3qk5jh0rzgps78fnxrwc5u1i8t&redirect_uri=http://localhost/twitch_redirect&scope=user%3Aread%3Afollows&state=12345
```

After granting the permission to the app, the browser will redirect you to
`http://localhost/twitch_redirect` page (and fail), and you should be able to
grab the `access_token` param from the destination URL.

# Running

To run in daemon mode:

`# stream-notifier <your-twitch-username> <access-token>`

To run a daemon without forking to background:

`# stream-notifier <your-twitch-username> <access-token> -debug`

To show current list of online streamers from your following list:

```
# stream-notifier <your-twitch-username> <access-token> -now
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

Only through source code so far. Not sure if it needs something more elaborate at this point. Most of the useful parameters are at the beginning of `src/daemon.c` file in the `/** Config */` section.

# TODO

Proper daemon ifrastructure:

- PID lock file
- Logging (at least to system logs)

To research/add:

- Will be nice to try to download and show streamers icons in notification.

