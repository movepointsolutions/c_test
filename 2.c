#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h> //protoent
#include <limits.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ev.h>

static void
socket_cb(EV_P_ ev_io *w, int revents)
{
	fprintf(stderr, "socket revents: %i\n", revents);

	char buffer[1024];
	unsigned long r = read(w->fd, buffer, sizeof(buffer));
	if (r < 0) {
		perror("read()");
		return;
	}

	if (r > 0) {
		int wr = write(w->fd, buffer, r);
		if (wr < 0) {
			perror("write");
			return;
		}
	} else {
		//socket is closing
		ev_io_stop(EV_A_ w);
		free(w);
		perror("probably closed socket");
	}
}

static void
accept_cb(EV_P_ ev_io *w, int revents)
{
	struct sockaddr_in sin;
	socklen_t sin_len = sizeof(sin);
	int fd = accept(w->fd, (struct sockaddr *)&sin, &sin_len);
	if (fd < 0) {
		perror("accept()");
		exit(1);
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	struct ev_io *watcher = malloc(sizeof(ev_io));
	ev_io_init(watcher, socket_cb, fd, EV_READ);
	ev_io_start(loop, watcher);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s port\n", argv[0]);
		exit(1);
	}

	struct protoent *pe = getprotobyname("tcp");
	int s = socket(AF_INET, SOCK_STREAM, pe->p_proto);
	if (s < 0) {
		perror("socket()");
		exit(1);
	}
	printf("%i\n", s);

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(argv[1]));
	int b = bind(s, (struct sockaddr *)&sin, sizeof(sin));
	if (b < 0) {
		perror("bind()");
		exit(1);
	}

	fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);

	if (listen(s, 3) < 0) {
		perror("listen()");
		exit(1);
	}

	struct ev_loop *loop = EV_DEFAULT;
	struct ev_io accept_watcher;
	ev_io_init(&accept_watcher, accept_cb, s, EV_READ);
	ev_io_start(loop, &accept_watcher);

	fprintf(stderr, "Starting event loop\n");
	ev_loop(loop, 0);
}
