#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <sysdecode.h>

#include <echo_mod.h>

static void
usage(void)
{
	fprintf(stderr, "Usage: echoctl <command> ...\n"
			"\n"
			"where command in one of:\n"
			"\tclear\t\t- clear buffer constants\n"
			"\tpoll [-rwW]\t- display I/O status\n"
			"\tresize <size>\t- set buffer size\n"
			"\tsize\t\t- display buffer size\n");
	exit(1);
}

static int
open_device(int flags)
{
	int fd;

	fd = open("/dev/echo", flags);
	if (fd == -1)
		err(1, "/dev/echo");
	return (fd);
}

static void
clear(int argc, char **argv)
{
	int fd;

	if (argc != 2)
		usage();

	fd = open_device(O_RDWR);
	if (ioctl(fd, ECHODEV_CLEAR) == -1)
		err(1, "ioctl(ECHODEV_CLEAR)");
	close(fd);
}

static void
size(int argc, char **argv)
{
	int fd;
	size_t len;

	if (argc != 2)
		usage();

	fd = open_device(O_RDONLY);
	if (ioctl(fd, ECHODEV_GBUFSIZE, &len) == -1)
		err(1, "ioctl(ECHODEV_GBUFSIZE)");
	fprintf(stdout, "size: %zu\n", len);
	close(fd);
}

static void
resize(int argc, char **argv)
{
	int fd;
	size_t len;
	const char *errstr;

	if (argc != 3)
		usage();

	len = (size_t)strtonum(argv[2], 0, 1024, &errstr);
	if (errstr != NULL)
		err(1, "new size is %s\n", errstr);

	fd = open_device(O_WRONLY);
	if (ioctl(fd, ECHODEV_SBUFSIZE, &len) == -1)
		err(1, "ioctl(ECHODEV_SBUFSIZE)");
	close(fd);
}

static void
status(int argc, char **argv)
{
	struct pollfd pfd;
	int ch, count, events, fd;
	bool wait;

	argc--;
	argv++;

	events = 0;
	wait = false;
	while ((ch = getopt(argc, argv, "rwW")) != -1) {
		switch(ch) {
		case 'r':
			events |= POLLIN;
			break;
		case 'w':
			events |= POLLOUT;
			break;
		case 'W':
			wait = true;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (events == 0)
		events = POLLIN | POLLOUT;

	fd = open_device(O_RDONLY);
	pfd.fd = fd;
	pfd.events = events;
	pfd.revents = 0;
	if (poll(&pfd, 1, wait ? INFTIM : 0) == -1)
		err(1, "poll");

	printf("Returned events: ");
	if (!sysdecode_pollfd_events(stdout, pfd.revents, NULL))
		printf("<none>");
	printf("\n");
	if (pfd.revents & POLLIN) {
		if (ioctl(fd, FIONREAD, &count) == -1)
			err(1, "ioctl(FIONREAD)");
		printf("%d available to read.\n", count);
	}
	if (pfd.revents & POLLOUT) {
		if (ioctl(fd, FIONWRITE, &count) == -1)
			err(1, "ioctl(FIONWRITE)");
		printf("%d available to write.\n", count);
	}
	close(fd);
}

int
main(int argc, char **argv)
{
	if (argc < 2)
		usage();

	if (strcmp(argv[1], "clear") == 0)
		clear(argc, argv);
	else if (strcmp(argv[1], "size") == 0)
		size(argc, argv);
	else if (strcmp(argv[1], "resize") == 0)
		resize(argc, argv);
	else if (strcmp(argv[1], "poll") == 0)
		status(argc, argv);
	else
		usage();

	return (0);
}
