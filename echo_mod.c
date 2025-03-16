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

struct echodev_softc {
	struct cdev *dev;
	char *buf;
	size_t len;
	struct sx lock;
};

MALLOC_DEFINE(M_ECHODEV, "echodev", "Buffers to echodev");

static int
echo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct echodev_softc *sc = dev->si_drv1;
	size_t todo = 0;
	int error;

	if (uio->uio_offset >= sc->len)
		return (0);

	sx_slock(&sc->lock);
	todo = MIN(uio->uio_resid, sc->len - uio->uio_offset);
	error = uiomove(sc->buf + uio->uio_offset, todo, uio);
	sx_sunlock(&sc->lock);
	return (error);
}

static int
echo_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct echodev_softc *sc = dev->si_drv1;
	size_t todo = 0;
	int error;

	if (uio->uio_offset >= sc->len)
			return (EFBIG);

	sx_xlock(&sc->lock);
	todo = MIN(uio->uio_resid, sc->len - uio->uio_offset);
	error = uiomove(sc->buf + uio->uio_offset, todo, uio);
	sx_xunlock(&sc->lock);
	return (error);
}

static struct cdevsw echo_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =		"echo",
	.d_read =		&echo_read,
	.d_write =		&echo_write
};

static int
echodev_load(struct echodev_softc **scp, size_t len)
{
	struct echodev_softc *sc;
	struct make_dev_args args;
	int error;

	sc = malloc(sizeof(*sc), M_ECHODEV, M_WAITOK | M_ZERO);
	sx_init(&sc->lock, "echodev");
	sc->buf = malloc(len, M_ECHODEV, M_WAITOK | M_ZERO);
	sc->len = len;
	make_dev_args_init(&args);
	args.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	args.mda_devsw = &echo_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_WHEEL;
	args.mda_mode = 0660;
	args.mda_si_drv1 = sc;
	error = make_dev_s(&args, &sc->dev, "echo");
	if (error != 0) {
		free(sc->buf, M_ECHODEV);
		sx_destroy(&sc->lock);
		free(sc, M_ECHODEV);
		return (error);
	}
	*scp = sc;
	return (0);
}

static int
echodev_unload(struct echodev_softc *sc, size_t len)
{
	if (sc != NULL) {
			destroy_dev(sc->dev);
			sx_destroy(&sc->lock);
			free(sc, M_ECHODEV);
	}
	return (0);
}

static int
echodev_modevent(module_t mod, int type, void *data)
{
	static struct echodev_softc *scp;

	switch(type) {
		case MOD_LOAD:
			return (echodev_load(&scp, 64));
		case MOD_UNLOAD:
			return (echodev_unload(scp, 64));
		default:
			return (EOPNOTSUPP);
	}
}

DEV_MODULE(echodev, echodev_modevent, NULL);
