#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/malloc.h>

#include "echo_mod.h"

static struct cdev *echocdev;
static char *echobuf;
static struct sx echolock;

MALLOC_DEFINE(M_ECHODEV, "echodev", "Buffers to echodev");

static int
echo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	size_t todo = 0;
	int error;

	if (uio->uio_offset >= sizeof(echobuf))
		return (0);

	sx_slock(&echolock);
	todo = MIN(uio->uio_resid, sizeof(echobuf) - uio->uio_offset);
	error = uiomove(echobuf + uio->uio_offset, todo, uio);
	sx_sunlock(&echolock);
	return (error);
}

static int
echo_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	size_t todo = 0;
	int error;

	if (uio->uio_offset >= sizeof(echobuf))
			return (EFBIG);

	sx_xlock(&echolock);
	todo = MIN(uio->uio_resid, sizeof(echobuf) - uio->uio_offset);
	error = uiomove(echobuf + uio->uio_offset, todo, uio);
	sx_xunlock(&echolock);
	return (error);
}

static struct cdevsw echo_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =		"echo",
	.d_read =		&echo_read,
	.d_write =		&echo_write
};

static int
echodev_load(size_t len)
{
	struct make_dev_args args;
	int error;

	make_dev_args_init(&args);
	args.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	args.mda_devsw = &echo_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_WHEEL;
	args.mda_mode = 0600;
	error = make_dev_s(&args, &echocdev, "echo");
	if (!error) {
		echobuf = (char *)malloc(len, M_ECHODEV , M_WAITOK | M_ZERO);
		sx_init(&echolock, "");
	}
	return (error);
}

static int
echodev_unload(size_t len)
{
	if (echocdev != NULL) {
			destroy_dev(echocdev);
			sx_destroy(&echolock);
			free(echobuf, M_ECHODEV);
	}
	return (0);
}

static int
echodev_modevent(module_t mod, int type, void *data)
{
	switch(type) {
		case MOD_LOAD:
			return (echodev_load(64));
		case MOD_UNLOAD:
			return (echodev_unload(64));
		default:
			return (EOPNOTSUPP);
	}
}

DEV_MODULE(echodev, echodev_modevent, NULL);
