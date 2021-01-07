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
#include <ctwitch/helix.h>

/** Config **/

// App ID (registered in Twitch dev portal).
static const char *app_id = "454q3qk5jh0rzgps78fnxrwc5u1i8t";
// Client secret for getting client credentials.
static const char *client_secret = "cdefldwy95veeixq41kwtlrv2audw7";

/** Helper functions **/

extern char *immutable_string_copy(const char *);

/** State **/

static bool terminated = false;

static char *auth_token = NULL;

void sig_handler(int signo) {
  if (signo == SIGINT || signo == SIGTERM) {
    terminated = true;
  }
}

/** Adds new item to the list.
 *
 * @param dest List to expand.
 * @param item Item to add to the list.
 */
void twitch_helix_stream_list_append(twitch_helix_stream_list *dest, twitch_helix_stream *item) {
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
twitch_helix_stream_list *get_live_follows(
  const char *username,
  const char *client_id,
  const char *client_secret
) {
  if (auth_token == NULL) {
    // Get token.
    twitch_helix_auth_token *token = twitch_helix_get_app_access_token(client_id, client_secret, 0, NULL);
    if (token == NULL) {
      fprintf(stderr, "Error: failed to get auth token\n");
      return NULL;
    }
    auth_token = immutable_string_copy(token->token);
    twitch_helix_auth_token_free(token);
  }

  // Find user by login name to get their user ID.
  twitch_helix_user *user = twitch_helix_get_user(client_id, auth_token, username);
  if (user == NULL) {
    return NULL;
  }

  twitch_helix_follow_list *follows = twitch_helix_get_all_follows(client_id, auth_token, user->id, 0);
  if (follows == NULL) {
    twitch_helix_user_free(user);
    return NULL;
  }

  long long *user_ids = malloc(sizeof(long long) * follows->count);
  for (int idx = 0; idx < follows->count; idx++) {
    user_ids[idx] = follows->items[idx]->to_id;
  }
  twitch_helix_stream_list *streams = twitch_helix_get_all_streams(
    client_id,
    auth_token,
    0,
    NULL,
    follows->count,
    user_ids,
    0,
    NULL
  );

  // Cleanup.
  twitch_helix_user_free(user);
  twitch_helix_follow_list_free(follows);

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
void show_streamer_online(twitch_helix_stream *stream) {
  char title[200];
  char message[500];

  sprintf(title, "%s is online", stream->user_name);
  sprintf(message, "<b>%s</b> (%s) is online playing <b>%s</b> with status:\n\n<b>%s</b>",
    stream->user_name,
    stream->user_name,
    stream->game_name != NULL ? stream->game_name : "unknown game",
    stream->title);

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
twitch_helix_stream_list *new_streams(twitch_helix_stream_list *old, twitch_helix_stream_list *new) {
  if (old == NULL) {
    return new;
  }

  if (new == NULL) {
    return NULL;
  }

  // Iterate over new list and add each item not present in the old list to the diff list.
  twitch_helix_stream_list *list = twitch_helix_stream_list_alloc();
  bool found = false;
  for (int new_ind = 0; new_ind < new->count; new_ind++) {
    for (int old_ind = 0; old_ind < old->count; old_ind++) {
      if (old->items[old_ind]->id == new->items[new_ind]->id) {
        found = true;
        break;
      }
    }
    if (!found) {
      twitch_helix_stream_list_append(list, new->items[new_ind]);
    }
    found = false;
  }

  return list;
}

void print_current_streams(char *username) {
  twitch_helix_stream_list* streams = get_live_follows(username, app_id, client_secret);

  if (streams == NULL) {
    fprintf(stderr, "Failed to get a list of active follows\n");
    exit(EXIT_FAILURE);
  }

  for (int index = 0; index < streams->count; index++) {
    twitch_helix_stream *stream = streams->items[index];
    printf("%s\n  Game: %s\n  Status: %s\n  URL: https://twitch.tv/%s\n",
      stream->user_name,
      stream->game_name,
      stream->title,
      stream->user_name
    );
  }

  twitch_helix_stream_list_free(streams);
}

/** Main **/

void print_usage() {
  printf("Usage:\n  stream-notifier <username> [options]\n\n");
  printf("Options:\n");
  printf("  %-20s\t%s\n", "-now", "Instead of starting the daemon, just prints out currently online streams and exits.");
  printf("  %-20s\t%s\n", "-debug", "Instead of forking to background, just run in the main loop.");
}

int main(int argc, char **argv) {
  bool should_fork = true;

  if (argc < 2) {
    print_usage();
    exit(EXIT_FAILURE);
  }

  if (argc > 2) {
    for (int idx = 2; idx < argc; idx++) {
      if (strcmp(argv[idx], "-now") == 0) {
        print_current_streams(argv[1]);
        return 0;
      } else if (strcmp(argv[idx], "-debug") == 0) {
        should_fork = false;
      } else {
        print_usage();
        exit(EXIT_FAILURE);
      }
    }
  }

  if (should_fork) {
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
  }

  // Initialize daemon.
  notify_init("stream-notifier");
  twitch_helix_stream_list *streams = NULL;
  streams = get_live_follows(argv[1], app_id, client_secret);

  while (!terminated) {
    sleep(120);

    // Update the list.
    twitch_helix_stream_list *updated = get_live_follows(argv[1], app_id, client_secret);

    // Show notifications for any new streams.
    if (updated != NULL) {
      twitch_helix_stream_list *new = new_streams(streams, updated);
      for (int index = 0; index < new->count; index++) {
        show_streamer_online(new->items[index]);
      }

      // Free the memory. Since we weren't copying stream structs, we just need to shallow free the diff list.
      free(new->items);
      free(new);
    }

    // Save the updated list as current.
    if (streams != NULL) {
      twitch_helix_stream_list_free(streams);
    }

    streams = updated;
  }

  // Cleanup.
  notify_uninit();
  if (streams != NULL) {
    twitch_helix_stream_list_free(streams);
  }

  return 0;
}

