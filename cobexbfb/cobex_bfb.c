/*
 * cobex_bfb.c - Talk OBEX over a serial port (Siemens specific)
 *
 *   Copyright (c) 2002 Christian W. Zuckschwerdt <zany@triq.net>
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 2 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *   for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *     
 */
/*
 *       Don, 17 Jan 2002 18:27:25 +0100
 * v0.6  Fre, 15 Feb 2002 15:41:10 +0100
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#define sleep(n)        _sleep(n*1000)
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include <openobex/obex.h>
#include "cobex_bfb.h"
#include "cobex_bfb_private.h"
#include <bfb/bfb.h>
#include <bfb/bfb_io.h>
#include <common.h>

static void cobex_cleanup(cobex_t *c, int force)
{
        return_if_fail (c != NULL);
#ifdef _WIN32
        return_if_fail (c->fd != INVALID_HANDLE_VALUE);
#else
        return_if_fail (c->fd > 0);
#endif

       	bfb_io_close(c->fd, force);

#ifdef _WIN32
	c->fd = INVALID_HANDLE_VALUE;
#else
	c->fd = -1;
#endif
}

int cobex_connect(obex_t *self, void *data)
{
	cobex_t *c;
        return_val_if_fail (self != NULL, -1);
        return_val_if_fail (data != NULL, -1);
	c = (cobex_t *) data;

	DEBUG(3, "%s() \n", __func__);

	c->fd = bfb_io_open(c->tty);
	if(c->fd == -2)
		c->type = CT_ERICSSON;
	else
		c->type = CT_SIEMENS;

#ifdef _WIN32
	if(c->fd == INVALID_HANDLE_VALUE)
#else
	if(c->fd == -1)
#endif
		return -1;

	return 1;
}

int cobex_disconnect(obex_t *self, void *data)
{
	cobex_t *c;
        return_val_if_fail (self != NULL, -1);
        return_val_if_fail (data != NULL, -1);
	c = (cobex_t *) data;

	DEBUG(3, "%s() \n", __func__);
	cobex_cleanup(c, FALSE);
	return 1;
}

/* Called from OBEX-lib when data needs to be written */
int cobex_write(obex_t *self, void *data, uint8_t *buffer, int length)
{
	int actual;
	cobex_t *c;
        return_val_if_fail (self != NULL, -1);
        return_val_if_fail (data != NULL, -1);
	c = (cobex_t *) data;
	
	DEBUG(3, "%s() \n", __func__);

	DEBUG(3, "%s() Data %d bytes\n", __func__, length);

	if (c->type == CT_ERICSSON) {
		actual = write(c->fd, buffer, length);
		if (actual < length)	{
			DEBUG(1, "Error writing to port\n");
			return actual; /* or -1? */
		}
		return actual;
	}

	if (c->seq == 0){
		actual = bfb_send_first(c->fd, buffer, length);
		DEBUG(2, "%s() Wrote %d first packets (%d bytes)\n", __func__, actual, length);
	} else {
		actual = bfb_send_next(c->fd, buffer, length, c->seq);
		DEBUG(2, "%s() Wrote %d packets (%d bytes)\n", __func__, actual, length);
	}
	c->seq++;

	return actual;
}

/* Called when input data is needed */
int cobex_handleinput(obex_t *self, void *data, int timeout)
{
#ifdef _WIN32
	DWORD actual;
#else
	struct timeval time;
	fd_set fdset;
	int actual;
#endif
	bfb_frame_t *frame;

	cobex_t *c;

        return_val_if_fail (self != NULL, -1);
        return_val_if_fail (data != NULL, -1);
	c = (cobex_t *) data;

#ifdef _WIN32
	if (!ReadFile(c->fd, &(c->recv[c->recv_len]), sizeof(c->recv) - c->recv_len, &actual, NULL))
		DEBUG(2, "%s() Read error: %ld\n", __func__, actual);

	DEBUG(2, "%s() Read %ld bytes (%d bytes already buffered)\n", __func__, actual, c->recv_len);
	/* FIXME ... */
#else
	time.tv_sec = timeout;
	time.tv_usec = 0;

	/* Add the fd's to the set. */
	FD_ZERO(&fdset);
	FD_SET(c->fd, &fdset);

	/* Wait for input */
	actual = select(c->fd + 1, &fdset, NULL, NULL, &time);

	DEBUG(2, "%s() There is something (%d)\n", __func__, actual);

	/* Check if this is a timeout (0) or error (-1) */
	if(actual <= 0)
		return actual;

	actual = read(c->fd, &(c->recv[c->recv_len]), sizeof(c->recv) - c->recv_len);
	DEBUG(2, "%s() Read %d bytes (%d bytes already buffered)\n", __func__, actual, c->recv_len);
#endif

	if (c->type == CT_ERICSSON) {
		if (actual > 0) {
			OBEX_CustomDataFeed(self, c->recv, actual);
			return 1;
		}
		return actual;
	}

	if ((c->data_buf == NULL) || (c->data_size == 0)) {
		c->data_size = 1024;
		c->data_buf = malloc(c->data_size);
	}

	if (actual > 0) {
		c->recv_len += actual;
		DEBUGBUFFER(c->recv, c->recv_len);

		while ((frame = bfb_read_packets(c->recv, &(c->recv_len)))) {
			DEBUG(2, "%s() Parsed %x (%d bytes remaining)\n", __func__, frame->type, c->recv_len);

			c->data_buf = bfb_assemble_data(&c->data_buf, &c->data_size, &c->data_len, frame);

			if (bfb_check_data(c->data_buf, c->data_len) == 1) {
				actual = bfb_send_ack(c->fd);
#ifdef _WIN32
				DEBUG(2, "%s() Wrote ack packet (%ld)\n", __func__, actual);
#else
				DEBUG(2, "%s() Wrote ack packet (%d)\n", __func__, actual);
#endif

				OBEX_CustomDataFeed(self, c->data_buf->data, c->data_len-7);
				/*
				free(c->data);
				c->data = NULL;
				*/
				c->data_len = 0;

				if (c->recv_len > 0) {
					DEBUG(2, "%s() Data remaining after feed, this can't be good.\n", __func__);
					DEBUGBUFFER(c->recv, c->recv_len);
				}

				return 1;
			}
		}

	}
	return actual;
}


/* Well, maybe this should be somewhere in the header */
/*
static obex_ctrans_t _cobex_ctrans = {
	connect: cobex_connect,
	disconnect: cobex_disconnect,
	write: cobex_write,
	listen: NULL,
	handleinput: cobex_handleinput,
};
*/
obex_ctrans_t *cobex_ctrans (const char *tty) {
	obex_ctrans_t *ctrans;
	cobex_t *cobex;

	cobex = calloc (1, sizeof(cobex_t));
	if(tty == NULL)
		tty = SERPORT;
	cobex->tty = strdup (tty);

	ctrans = calloc (1, sizeof(obex_ctrans_t));
	ctrans->connect = cobex_connect;
	ctrans->disconnect = cobex_disconnect;
	ctrans->write = cobex_write;
	ctrans->listen = NULL;
	ctrans->handleinput = cobex_handleinput;
	ctrans->customdata = cobex;
	
	return ctrans;
}


void cobex_free (obex_ctrans_t *ctrans)
{
	cobex_t *cobex;

	return_if_fail (ctrans != NULL);

	cobex = (cobex_t *)ctrans->customdata;

	return_if_fail (cobex != NULL);

	free (cobex->tty);
	/* maybe there is a bfb_data_t left? */
	/* free(cobex->data); */

	free (cobex);
	free (ctrans);

	return;
}
