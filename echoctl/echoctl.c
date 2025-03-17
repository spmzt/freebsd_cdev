#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>

#include <echo_mod.h>

static void
usage(void)
{
	fprintf(stderr, "Usage: echoctl <command> ...\n"
			"\n"
			"where command in one of:\n"
			"\tclear\t\t- clear buffer constants\n"
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
	else
		usage();

	return (0);
}
