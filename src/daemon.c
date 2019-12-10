#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <libnotify/notify.h>
#include <libgen.h>
#include <ctwitch/v5.h>

/** Config **/

// App ID (registered in Twitch dev portal).
static const char *app_id = "454q3qk5jh0rzgps78fnxrwc5u1i8t";

/** Internal functions from ctwitch **/

extern char *immutable_string_copy(const char *src);
extern void **pointer_array_map(void **src, size_t src_count, void *(*getter)(void *));
extern void pointer_array_free(int count, void **src, void(*deinit)(void *));

/** State **/

static bool terminated = false;

void sig_handler(int signo) {
  if (signo == SIGINT || signo == SIGTERM) {
    terminated = true;
  }
}

/** Helpers **/

void *channel_id_from_follow(void *src) {
  twitch_v5_follow *follow = (twitch_v5_follow *)src;
  char buffer[64];

  sprintf(buffer, "%lld", follow->channel->id);
  return immutable_string_copy(buffer);
}

/** Adds new item to the list.
 *
 * @param dest List to expand.
 * @param item Item to add to the list.
 */
void twitch_v5_stream_list_append(twitch_v5_stream_list *dest, twitch_v5_stream *item) {
  if (dest->count == 0) {
    dest->items = malloc(sizeof(void *));
  } else {
    dest->items = realloc(dest->items, sizeof(void *) * (dest->count + 1));
  }
  dest->items[dest->count] = item;
  dest->count += 1;
}

/**
 * Searches for live streams from given user's follows list.
 *
 * @param username Name of the user for which to get follows.
 */
twitch_v5_stream_list *get_live_follows(const char *username, const char *client_id) {
  // Find user by login name to get their user ID.
  twitch_v5_user *user = twitch_v5_get_user_by_username(client_id, username);
  if (user == NULL) {
    return NULL;
  }

  // Convert ID to string.
  char* user_id = malloc(64 * sizeof(char));
  sprintf(user_id, "%lld", user->id);

  // Get all user's follows.
  twitch_v5_follow_list *follows = twitch_v5_get_all_user_follows(client_id, user_id, NULL, NULL);
  if (follows == NULL) {
    twitch_v5_user_free(user);
    return NULL;
  }

  // Get all streams from user's follows.
  char **channel_ids = (char **)pointer_array_map((void **)follows->items, follows->count, &channel_id_from_follow);
  int streams_count = 0;
  twitch_v5_stream_list *streams = twitch_v5_get_all_streams(
    client_id,
    follows->count,
    (const char **)channel_ids,
    NULL,
    NULL,
    NULL
  );

  // Cleanup.
  twitch_v5_user_free(user);
  twitch_v5_follow_list_free(follows);

  return streams;
}

/**
 * Sends desktop notification with given title and message.
 *
 * @param title Notification title.
 * @param message Notification message.
 *
 */
void show_update(char *title, char *message) {
  NotifyNotification *notif;
  notif = notify_notification_new(title, message, NULL);
  notify_notification_show(notif, NULL);
}

/**
 * Creates and sends notification that streamer became online.
 *
 * @param stream Stream info to show.
 */
void show_streamer_online(twitch_v5_stream *stream) {
  char title[200];
  char message[500];

  sprintf(title, "%s is online", stream->channel->display_name);
  sprintf(message, "<b>%s</b> (%s) is online playing <b>%s</b> with status:\n\n<b>%s</b>",
    stream->channel->display_name,
    stream->channel->name,
    stream->game != NULL ? stream->game : "unknown game",
    stream->channel->status);

  show_update(title, message);
}

/**
 * Returns a list of streams that are new compared to the "old" list of streams.
 *
 * @param old Original list.
 * @param new New list to compare to original.
 *
 * @return List containing only 'new' stream items.
 */
twitch_v5_stream_list *new_streams(twitch_v5_stream_list *old, twitch_v5_stream_list *new) {
  if (old == NULL) {
    return new;
  }

  if (new == NULL) {
    return NULL;
  }

  // Iterate over new list and add each item not present in the old list to the diff list.
  twitch_v5_stream_list *list = twitch_v5_stream_list_alloc();
  bool found = false;
  for (int new_ind = 0; new_ind < new->count; new_ind++) {
    for (int old_ind = 0; old_ind < old->count; old_ind++) {
      if (old->items[old_ind]->id == new->items[new_ind]->id) {
        found = true;
        break;
      }
    }
    if (!found) {
      twitch_v5_stream_list_append(list, new->items[new_ind]);
    }
    found = false;
  }

  return list;
}

/** Main **/

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: stream-notifier <username>\n");
    exit(EXIT_FAILURE);
  }

  pid_t pid, sid;
  pid = fork();

  if (pid < 0) {
    fprintf(stderr, "Failed to fork the process\n");
    exit(EXIT_FAILURE);
  }

  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  sid = setsid();
  if (sid < 0) {
    fprintf(stderr, "Failed to get a SID for a child process\n");
    exit(EXIT_FAILURE);
  }

  // Setup signal catchers.
  if (signal(SIGINT, sig_handler) == SIG_ERR) {
    fprintf(stderr, "Failed to setup SIGINT catcher\n");
    exit(EXIT_FAILURE);
  }
  if (signal(SIGTERM, sig_handler) == SIG_ERR) {
    fprintf(stderr, "Failed to setup SIGTERM catcher\n");
    exit(EXIT_FAILURE);
  }

  // Fork off for the second time.
  pid = fork();

  if (pid < 0) {
    exit(EXIT_FAILURE);
  }

  if (pid > 0) {
    fprintf(stdout, "Forked to child process %lld\n", pid);
    exit(EXIT_SUCCESS);
  }

  umask(0);
  chdir("/");

  // Close standard outputs.
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  // Initialize dameon.
  notify_init("stream-notifier");
  twitch_v5_stream_list *streams = NULL;
  streams = get_live_follows(argv[1], app_id);

  while (!terminated) {
    sleep(120);

    // Update the list.
    twitch_v5_stream_list *updated = get_live_follows(argv[1], app_id);

    // Show notifications for any new streams.
    twitch_v5_stream_list *new = new_streams(streams, updated);
    for (int index = 0; index < new->count; index++) {
      show_streamer_online(new->items[index]);
    }

    // Free the memory. Since we weren't copying stream structs, we just need to shallow free the diff list.
    free(new->items);
    free(new);

    // Save the updated list as current.
    twitch_v5_stream_list_free(streams);
    streams = updated;
  }

  // Cleanup.
  notify_uninit();
  if (streams != NULL) {
    twitch_v5_stream_list_free(streams);
  }

  return 0;
}

