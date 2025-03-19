#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/selinfo.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/poll.h>

#include "echo_mod.h"

struct echodev_softc {
	struct cdev *dev;
	char *buf;
	size_t len;
	struct sx lock;
	struct selinfo rsel;
	struct selinfo wsel;
	size_t valid;
	bool dying;
	uint32_t writers;
};

MALLOC_DEFINE(M_ECHODEV, "echodev", "Buffers to echodev");

static int
echo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct echodev_softc *sc = dev->si_drv1;
	int error;

	switch (cmd) {
	case FIONREAD:
		sx_slock(&sc->lock);
		*(int *)data = MIN(INT_MAX, sc->valid);
		sx_sunlock(&sc->lock);
		error = 0;
		break;
	case FIONWRITE:
		sx_slock(&sc->lock);
		*(int *)data = MIN(INT_MAX, sc->len - sc->valid);
		sx_sunlock(&sc->lock);
		error = 0;
		break;
	case FIONBIO:
		error = 0;
		break;
	case FIOASYNC:
		if (*(int *)data != 0)
			error = EINVAL;
		else
			error = 0;
		break;
	case ECHODEV_GBUFSIZE:
		sx_slock(&sc->lock);
		*(size_t *)data = sc->len;
		sx_sunlock(&sc->lock);
		error = 0;
		break;
	case ECHODEV_SBUFSIZE:
	{
		size_t new_len;
		if ((fflag & FWRITE) == 0) {
			error = EPERM;
			break;
		}
		new_len = *(size_t *)data;
		sx_slock(&sc->lock);
		if (new_len == sc->len) {
			// nothing to do
		} else if (new_len < sc->len) {
			sc->len = new_len;
		} else {
			sc->buf = reallocf(sc->buf, new_len, M_ECHODEV, M_WAITOK | M_ZERO);
			sc->len = new_len;
		}
		sx_sunlock(&sc->lock);
		error = 0;
		break;
	}
	case ECHODEV_CLEAR:
		if ((fflag & FWRITE) == 0) {
			error = EPERM;
			break;
		}
		sx_slock(&sc->lock);
		memset(sc->buf, 0, sc->len);
		sx_sunlock(&sc->lock);
		error = 0;
		break;
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}

static int
echo_poll(struct cdev *dev, int events, struct thread *td)
{
	struct echodev_softc *sc = dev->si_drv1;
	int revents;

	revents = 0;
	sx_slock(&sc->lock);
	if (sc->valid != 0 || sc->writers == 0)
		revents |= events & (POLLIN | POLLRDNORM);
	if (sc->valid < sc->len)
		revents |= events & (POLLOUT | POLLWRNORM);
	if (revents == 0) {
		if ((events & (POLLIN | POLLRDNORM)) != 0)
			selrecord(td, &sc->rsel);
		if ((events & (POLLOUT | POLLWRNORM)) != 0)
			selrecord(td, &sc->wsel);
	}
	sx_sunlock(&sc->lock);
	return (revents);
}

static int
echo_open(struct cdev *dev, int oflag, int devtype, struct thread *td)
{
	struct echodev_softc *sc = dev->si_drv1;

	if ((oflag & FWRITE) != 0) {
		sx_xlock(&sc->lock);
		if (sc->writers == UINT_MAX) {
			sx_xunlock(&sc->lock);
			return (EBUSY);
		}
		sc->writers++;
		sx_xunlock(&sc->lock);
	}
	return (0);
}

static int
echo_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct echodev_softc *sc = dev->si_drv1;

	if ((fflag & FWRITE) != 0) {
		sx_xlock(&sc->lock);
		sc->writers--;
		if (sc->writers == 0) {
			wakeup(sc);
		}
		sx_xunlock(&sc->lock);
	}
	return (0);
}

static int
echo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct echodev_softc *sc = dev->si_drv1;
	size_t todo = 0;
	int error;

	if (uio->uio_resid == 0)
		return (0);

	sx_xlock(&sc->lock);

	/* wait for bytes to read */
	while (sc->valid == 0 && sc->writers != 0) {
		if (sc->dying == true)
			error = ENXIO;
		else if (ioflag & O_NONBLOCK)
			error = EWOULDBLOCK;
		else
			error = sx_sleep(sc, &sc->lock, PCATCH, "echord", 0);
		if (error != 0) {
			sx_xunlock(&sc->lock);
			return (error);
		}	
	}

	todo = MIN(uio->uio_resid, sc->valid);
	error = uiomove(sc->buf, todo, uio);
	if (error == 0) {
		if (sc->valid == sc->len)
			wakeup(sc);
		sc->valid -= todo;
		memmove(sc->buf, sc->buf + todo, sc->valid);
		selwakeup(&sc->wsel);
	}

	sx_xunlock(&sc->lock);
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
	todo = MIN(uio->uio_resid, sc->len - sc->valid);
	error = uiomove(sc->buf + sc->valid, todo, uio);
	if (error == 0)
		sc->valid += todo;
	wakeup(sc);
	sx_xunlock(&sc->lock);
	return (error);
}

static struct cdevsw echo_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =		"echo",
	.d_open =		&echo_open,
	.d_close =		&echo_close,
	.d_read =		&echo_read,
	.d_write =		&echo_write,
	.d_ioctl =		&echo_ioctl,
	.d_poll =		&echo_poll
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
	if (sc->dev != NULL) {
			sx_xlock(&sc->lock);
			sc->dying = true;
			wakeup(sc);
			sx_xunlock(&sc->lock);

			destroy_dev(sc->dev);
	}
	free(sc->buf, M_ECHODEV);
	sx_destroy(&sc->lock);
	free(sc, M_ECHODEV);
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
