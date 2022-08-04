#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h> //protoent
#include <limits.h>
#include <pthread.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ev.h>

#define MAX_PACKET_SIZE 65536

static int port;
static pthread_mutex_t mutex;

struct buffer_t
{
	void *data;
	unsigned long size;
	int fd;
	struct buffer_t *prev;
	struct buffer_t *next;
};

struct buffer_t queue = {NULL, 0, 0, &queue, &queue};
struct buffer_t done_queue = {NULL, 0, 0, &done_queue, &done_queue};

struct ev_loop *tcp_loop;
struct ev_async done_event;
struct ev_async buffer_event;

static struct buffer_t *
get_buffer(struct buffer_t *q)
{
	pthread_mutex_lock(&mutex);
	struct buffer_t *ret = q->next;
	q->next = ret->next;
	q->next->prev = q;
	if (ret == q->prev)
		q->prev = q;
	pthread_mutex_unlock(&mutex);
	
	if (ret == q)
		ret = NULL;

	return ret;
}

static void
put_buffer(struct buffer_t *q, struct buffer_t *b)
{
	pthread_mutex_lock(&mutex);
	b->prev = q->prev;
	if (b->prev == q)
		q->next = b;
	q->prev = b;
	b->next = q;
	if (q->next == q)
		q->next = b;
	pthread_mutex_unlock(&mutex);
}

static void
socket_cb(EV_P_ ev_io *w, int revents)
{
	fprintf(stderr, "socket revents: %i\n", revents);

	char buffer[MAX_PACKET_SIZE];
	unsigned long r = read(w->fd, buffer, sizeof(buffer));
	if (r < 0) {
		perror("read()");
		return;
	}

	if (r > 0) {
		struct buffer_t *b = malloc(sizeof(struct buffer_t));
		b->data = malloc(r);
		memcpy(b->data, buffer, r);
		b->size = r;
		b->fd = w->fd;
		put_buffer(&queue, b);
		ev_async_send(EV_DEFAULT, &buffer_event);
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

static void
send_cb()
{
	struct buffer_t *buf;
	while (buf = get_buffer(&done_queue)) {
		write(buf->fd, buf->data, buf->size);
	}
}

static void *tcp_thread(void *)
{
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
	sin.sin_port = htons(port);
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

	tcp_loop = ev_loop_new(EVFLAG_AUTO);
	struct ev_io accept_watcher;
	ev_io_init(&accept_watcher, accept_cb, s, EV_READ);
	ev_io_start(tcp_loop, &accept_watcher);
	ev_async_init(&done_event, send_cb);
	ev_async_start(tcp_loop, &done_event);

	fprintf(stderr, "Starting TCP event loop\n");
	ev_loop(tcp_loop, 0);
}

static void buffer_cb()
{
	struct buffer_t *buf;
	while (buf = get_buffer(&queue)) {
		fprintf(stderr, "buffer proc: %i\n", buf->size);
		for (int i = 0; i < buf->size / 2; i++) {
			char *d = buf->data;
			char tmp = d[i];
			d[i] = d[buf->size - 1 - i];
			d[buf->size - 1 - i] = tmp;
		}
		put_buffer(&done_queue, buf);
		ev_async_send(tcp_loop, &done_event);
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s port\n", argv[0]);
		exit(1);
	}
	port = atoi(argv[1]);

	pthread_attr_t attr;
	int a = pthread_attr_init(&attr);
	if (a) {
		perror("pthread_attr_init()");
		exit(1);
	}

	pthread_t thread;
	pthread_create(&thread, &attr, tcp_thread, NULL);
	
	struct ev_loop *loop = EV_DEFAULT;
	ev_async_init(&buffer_event, buffer_cb);
	ev_async_start(loop, &buffer_event);

	ev_loop(loop, 0);

	void *res;
	int j = pthread_join(thread, &res);
	if (j) {
		perror("pthread_join()");
		exit(1);
	}

	return 0;
}
