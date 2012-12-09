#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <poll.h>
#include <err.h>
#include <curses.h>
#include <sys/inotify.h>
#include <readline/readline.h>

#define NICKLEN 56
#define NICKLEN_DISPLAY 15

char buf[LINE_MAX];
FILE *in, *out;
WINDOW *inwin, *outwin;
bool running = true;
bool displayed_completion = false;

struct nicklist {
	char nick[NICKLEN];
	struct nicklist *next;
} *nicklist = NULL;

void nicktolist(const char *nick)
{
	struct nicklist **i;
	size_t nicklen = strlen(nick);

	if (!nicklen || nicklen + 1 > NICKLEN)
		return;
	for (i = &nicklist; *i != NULL; i = &(*i)->next)
		if (!strcmp((*i)->nick, nick))
			return;
	*i = malloc(sizeof(**i));
	strncpy((*i)->nick, nick, NICKLEN);
	(*i)->next = NULL;
}

char *generatecompletion(const char *text, int state)
{
	static struct nicklist *nick;

	if (!state)
		nick = nicklist;
	for (; nick != NULL; nick = nick->next) {
		if (!strncmp(nick->nick, text, strlen(text))) {
			size_t len = strlen(nick->nick) + strlen(":") + 1;
			char *n = malloc(len);
			if (!n)
				return NULL;
			snprintf(n, len, "%s:", nick->nick);
			nick = nick->next;
			return n;
		}
	}
	return NULL;
}

void displaycompletion(char **matches, int nr_matches, int maxlen)
{
	int i;

	waddstr(inwin, " [");
	for (i = 1; i <= nr_matches; i++) {
		/* don't display last character (':') */
		waddnstr(inwin, matches[i], strlen(matches[i]) - 1);
		if (i + 1 <= nr_matches)
			waddstr(inwin, ", ");
	}
	waddch(inwin, ']');
	wrefresh(inwin);
	displayed_completion = true;
}

void refreshinwin(void)
{
	wmove(inwin, 0, 0);
	waddstr(inwin, rl_prompt);
	waddch(inwin, ' ');
	waddstr(inwin, rl_line_buffer);
	wmove(inwin, 0, strlen(rl_prompt) + 1 /*' '*/ + rl_point);
	/* don't erase if we've just displayed a completition */
	if (displayed_completion)
		displayed_completion = false;
	else
		wclrtoeol(inwin);
	wrefresh(inwin);
}

void writeoutput(char *line)
{
	char *date, *time, *nick, *mesg;
	size_t nicklen;

	date = strtok(line, " ");
	time = strtok(NULL, " ");
	nick = strtok(NULL, " ");
	mesg = strtok(NULL, "\n");
	if (!date || !time || !nick || !mesg)
		return;
	nicklen = strlen(nick);
	if (nick[0] == '<' && nick[nicklen - 1] == '>') {
		nick[nicklen - 1] = '\0';
		nick++;
		nicktolist(nick);
	}
	wprintw(outwin, "\n%s %s %*s | %s", date, time, NICKLEN_DISPLAY, nick, mesg);
}

void chaninput(void)
{
	while (fgets(buf, sizeof(buf), out))
		writeoutput(buf);
	wrefresh(outwin);
	refreshinwin(); /* move cursor back to input window */
}

void userinput(char *line)
{
	if (!line) {
		running = false;
		return;
	}
	if (line[0] == '\0') {
		free(line);
		return;
	}
	add_history(line);
	fputs(line, in);
	fputc('\n', in);
	fflush(in);
	free(line);
}

void handle(struct pollfd *pfds)
{
	int n = poll(pfds, 2, -1);
	if (n == -1 && errno != EINTR)
		err(1, "failed to poll");
	if (pfds[0].revents)
		rl_callback_read_char();
	if (pfds[1].revents) {
		read(pfds[1].fd, buf, sizeof(buf));
		chaninput();
	}
}

void openchannel(const char *channel)
{
	if (chdir(channel) == -1)
		err(1, "failed to chdir to %s", channel);
	in  = fopen("in", "w");
	out = fopen("out", "r");
	if (!in || !out)
		err(1, "failed to open file %s/%s", channel, !in ? "in" : "out");
}

int setupinotify(void)
{
	int i = inotify_init1(IN_NONBLOCK);
	if (i == -1)
		err(1, "failed to initialize inotify");
	if (inotify_add_watch(i, "out", IN_MODIFY) == -1)
		err(1, "failed to setup inotify watch for out file");
	return i;
}

void initcurses()
{
	initscr();
	cbreak();
	noecho();
	outwin = newwin(LINES - 1, 0, 0, 0);
	inwin  = newwin(1, 0, LINES - 1, 0);
	scrollok(outwin, true);
}

int rl_wgetch_wrapper(FILE *unused)
{
	return wgetch(inwin);
}

void initreadline(char *channel)
{
	rl_getc_function = rl_wgetch_wrapper;
	rl_redisplay_function = refreshinwin;
	rl_completion_entry_function = generatecompletion;
	rl_completion_display_matches_hook = displaycompletion;
	rl_callback_handler_install(basename(channel), userinput);
}

int main(int argc, char *argv[])
{
	struct pollfd pfds[2];

	if (argc != 2)
		errx(1, "usage: ini <in/out files directory>");
	initcurses();
	openchannel(argv[1]);
	initreadline(argv[1]);
	chaninput();
	pfds[0].fd = fileno(stdin);
	pfds[1].fd = setupinotify();
	pfds[0].events = pfds[1].events = POLLIN;
	while (running)
		handle(pfds);
	fclose(in);
	fclose(out);
	close(pfds[1].fd);
	endwin();
	return 0;
}
