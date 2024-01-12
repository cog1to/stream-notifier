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

#include "strings.h"

/** Config **/

// App ID (registered in Twitch dev portal).
static const char *app_id = "454q3qk5jh0rzgps78fnxrwc5u1i8t";
// Action template.
static const char *action_template = "xdg-open 'https://www.twitch.tv/%s'";
// Action title.
static const char *action_title = "Open";
// Notification expiration timeout.
static const gint expiration_time_ms = 15000;
// Polling interval.
static const gint polling_interval_ms = 120000;

/** State **/

// Termination flag. Is set from the signal handler.
static bool terminated = false;
// API auth token.
static char *auth_token = NULL;
// Main loop for Glib.
GMainLoop *loop = NULL;
// Very lazy safeguard against launching multiple updates concurrently.
bool fetching = false;

/** Helper functions **/

// Copies given string to a new memory storage and returns a pointer to it.
extern char *immutable_string_copy(const char *);

// Notification action handler.
void handle_notification_action(
	NotifyNotification *notification,
	char *action,
	gpointer user_data
);

/* Signal handler. */

void sig_handler(int signo) {
	if (signo == SIGINT || signo == SIGTERM) {
		terminated = true;
	}
}

/* API call params */

typedef struct {
	char *username;
	char *token;
} call_params_t;

/** API Wrappers **/

/**
 * Adds new item to the list.
 *
 * @param dest List to expand.
 * @param item Item to add to the list.
 */
void twitch_helix_stream_list_append(
	twitch_helix_stream_list *dest,
	twitch_helix_stream *item
) {
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
 * @param client_id API client ID.
 * @param user_token API access token.
 */
twitch_helix_stream_list *get_live_follows(
	const char *username,
	const char *client_id,
	const char *user_token
) {
	// Find user by login name to get their user ID.
	twitch_helix_user *user = twitch_helix_get_user(client_id, user_token, username);
	if (user == NULL) {
		return NULL;
	}

	twitch_helix_channel_follow_list *follows = twitch_helix_get_all_channel_follows(
		client_id,
		user_token,
		user->id,
		0
	);
	if (follows == NULL) {
		twitch_helix_user_free(user);
		return NULL;
	}

	char **user_ids = malloc(sizeof(long long) * follows->count);
	for (int idx = 0; idx < follows->count; idx++) {
		user_ids[idx] = follows->items[idx]->broadcaster_id;
	}

	twitch_helix_stream_list *streams = twitch_helix_get_all_streams(
		client_id,
		user_token,
		NULL,
		NULL,
		follows->count,
		(const char **)user_ids,
		0,
		NULL
	);

	// Cleanup.
	twitch_helix_user_free(user);
	twitch_helix_channel_follow_list_free(follows);

	return streams;
}

/**
 * Sends desktop notification with given title and message.
 *
 * @param title Notification title.
 * @param message Notification message.
 * @param user_name Stream's user name.
 */
void show_update(char *title, char *message, char *user_name) {
	NotifyNotification *notif;
	notif = notify_notification_new(title, message, NULL);

	// Add an action.
	char *user_data = calloc(strlen(user_name) + 1, sizeof(char));
	strcpy(user_data, user_name);
	notify_notification_add_action(
		notif,
		"STREAM_NOTIF_ACTION",
		action_title,
		NOTIFY_ACTION_CALLBACK(handle_notification_action),
		user_data,
		free
	);

	// Set timeout.
	notify_notification_set_timeout(notif, expiration_time_ms);

	// Show notification.
	GError *error = NULL;
	notify_notification_show(notif, &error);
	if (error != NULL) {
		fprintf(stderr, "Failed to show notification: %s\n", error->message);
		g_clear_error(&error);
	}
}

/**
 * Creates and sends notification that streamer became online.
 *
 * @param stream Stream info to show.
 */
void show_streamer_online(twitch_helix_stream *stream) {
	char title[200];
	char message[500];

	char *sanitized_title = string_html_escape(stream->title);
	char *sanitized_game = string_html_escape(stream->game_name);

	sprintf(title, "%s is online", stream->user_name);
	sprintf(message, "<b>%s</b> is online playing <b>%s</b> with status:\n\n<b>%s</b>",
		stream->user_name,
		sanitized_game != NULL ? sanitized_game : "unknown game",
		sanitized_title);

	show_update(title, message, stream->user_name);

	free(sanitized_title);
	free(sanitized_game);
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
		return twitch_helix_stream_list_alloc();
	}

	if (new == NULL) {
		return twitch_helix_stream_list_alloc();
	}

	// Iterate over new list and add each item not present in the old list to the diff list.
	twitch_helix_stream_list *list = twitch_helix_stream_list_alloc();
	bool found = false;
	for (int new_ind = 0; new_ind < new->count; new_ind++) {
		for (int old_ind = 0; old_ind < old->count; old_ind++) {
			if (strcmp(old->items[old_ind]->id, new->items[new_ind]->id) == 0) {
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

/**
 * Outputs currently online followed streams to stdout.
 *
 * @param username Username for which to get following streams.
 * @param token User access token.
 */
void print_current_streams(char *username, char *user_token) {
	twitch_helix_stream_list* streams = get_live_follows(
		username,
		app_id,
		user_token
	);

	if (streams == NULL) {
		fprintf(stderr, "Failed to get a list of active follows\n");
		exit(EXIT_FAILURE);
	}

	for (int index = 0; index < streams->count; index++) {
		twitch_helix_stream *stream = streams->items[index];
		printf("%s\n	Game: %s\n	Status: %s\n	URL: https://twitch.tv/%s\n",
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
	printf("Usage:\n	stream-notifier <username> <access token> [options]\n\n");
	printf("Options:\n");
	printf("	%-20s\t%s\n", "-now", "Instead of starting the daemon, just prints out currently online streams and exits.");
	printf("	%-20s\t%s\n", "-debug", "Instead of forking to background, just run in the main loop.");
}

gboolean update_list(gpointer user_data) {
	static twitch_helix_stream_list *current = NULL;
	twitch_helix_stream_list *updated = NULL;

	if (fetching) {
		return true;
	} else {
		fetching = true;
	}

	call_params_t *params = (call_params_t *)user_data;
	char *username = params->username;
	char *token = params->token;

	// Update the list.
	updated = get_live_follows(username, app_id, token);

	// Show notifications for any new streams.
	if (updated != NULL) {
		twitch_helix_stream_list *new_list = new_streams(current, updated);
		for (int index = 0; index < new_list->count; index++) {
			show_streamer_online(new_list->items[index]);
		}

		// Free the memory. Since we weren't copying stream structs, we just need to shallow free the diff list.
		free(new_list->items);
		free(new_list);
	}

	// Save the updated list as current.
	if (current != NULL) {
		twitch_helix_stream_list_free(current);
	}

	current = updated;

	// Unblock subsequent calls.
	fetching = false;

	if (terminated) {
		g_main_loop_quit(loop);
	}

	return !terminated;
}

int main(int argc, char **argv) {
	bool should_fork = true;

	if (argc < 3) {
		print_usage();
		exit(EXIT_FAILURE);
	}

	if (argc > 3) {
		if (strcmp(argv[argc-1], "-now") == 0) {
			print_current_streams(argv[1], argv[2]);
			return 0;
		} else if (strcmp(argv[argc-1], "-debug") == 0) {
			should_fork = false;
		} else {
			print_usage();
			exit(EXIT_FAILURE);
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
	streams = get_live_follows(argv[1], app_id, argv[2]);

	// Start the loop.
	loop = g_main_loop_new(NULL, false);
	call_params_t params = {
		.username = argv[1],
		.token = argv[2]
	};
	g_timeout_add(polling_interval_ms, update_list, &params);
	g_main_loop_run(loop);

	// Cleanup.
	g_main_loop_unref(loop);
	notify_uninit();
	if (streams != NULL) {
		twitch_helix_stream_list_free(streams);
	}

	return 0;
}

/** Private **/

void handle_notification_action(NotifyNotification *notification, char *action, gpointer user_data) {
	char command[256] = { 0 };
	sprintf(command, action_template, (char *)user_data);
	system(command);
}

