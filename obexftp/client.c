/*
 *  obexftp/client.c: ObexFTP client library
 *
 *  Copyright (c) 2002 Christian W. Zuckschwerdt <zany@triq.net>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *     
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* htons */
#ifdef _WIN32
#include <winsock.h>
#else
#include <netinet/in.h>
#endif

#ifdef _WIN32
#define PATH_MAX MAX_PATH
#endif /* _WIN32 */

#include <openobex/obex.h>

#include "obexftp.h"
#include "client.h"
#include "object.h"
#include "obexftp_io.h"
#include "uuid.h"

#include "dirtraverse.h"
#include <common.h>

#ifdef DEBUG_TCP
#include <unistd.h>

#ifdef _WIN32
#include <winsock.h>
#else

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#endif
#endif


typedef struct { /* fixed to 6 bytes for now */
	uint8_t code;
	uint8_t info_len;
	uint8_t info[4];
} apparam_t;


/* recursive SetPath; omit the last component (probably filename) */
/* return number of setpaths done */
static int setpath(obexftp_client_t *cli, const char *name)
{
	int depth = 0;
	char *p;
	char *tail;
	char *copy;

	return_val_if_fail(cli != NULL, -1);
	return_val_if_fail(name != NULL, -1);

	while (*name == '/') name++;
	tail = copy = strdup(name);

	for (p = strchr(tail, '/'); p; p = strchr(p, '/')) {
		*p = '\0';
		p++;
		(void) obexftp_setpath(cli, tail);
		tail = p;
		depth++;
	}
	free(copy);

	return depth;
}


/* Add more data to stream. */
static int cli_fillstream(obexftp_client_t *cli, obex_object_t *object)
{
	int actual;
	obex_headerdata_t hdd;
		
	DEBUG(3, "%s()\n", __func__);
	
	actual = read(cli->fd, cli->buf, STREAM_CHUNK);
	
	DEBUG(3, "%s() Read %d bytes\n", __func__, actual);
	
	if(actual > 0) {
		/* Read was ok! */
		hdd.bs = cli->buf;
		OBEX_ObjectAddHeader(cli->obexhandle, object, OBEX_HDR_BODY,
				hdd, actual, OBEX_FL_STREAM_DATA);
	}
	else if(actual == 0) {
		/* EOF */
		hdd.bs = cli->buf;
		close(cli->fd);
		cli->fd = -1;
		OBEX_ObjectAddHeader(cli->obexhandle, object, OBEX_HDR_BODY,
				hdd, 0, OBEX_FL_STREAM_DATAEND);
	}
        else {
		/* Error */
		hdd.bs = NULL;
		close(cli->fd);
		cli->fd = -1;
		OBEX_ObjectAddHeader(cli->obexhandle, object, OBEX_HDR_BODY,
				hdd, 0, OBEX_FL_STREAM_DATA);
	}

	return actual;
}


/* Save body from object or return application parameters */
static void client_done(obex_t *handle, obex_object_t *object, int obex_cmd, int obex_rsp)
{
        obex_headerdata_t hv;
        uint8_t hi;
        uint32_t hlen;

        const uint8_t *body = NULL;
        int body_len = 0;

	apparam_t *app = NULL;
	uint32_t info;

	obexftp_client_t *cli;

	cli = OBEX_GetUserData(handle);

        DEBUG(3, "%s()\n", __func__);

	if (cli->fd > 0)
		close(cli->fd);

        while(OBEX_ObjectGetNextHeader(handle, object, &hi, &hv, &hlen)) {
                if(hi == OBEX_HDR_BODY) {
			DEBUG(3, "%s() Found body (length: %d)\n", __func__, hlen);
                        body = hv.bs;
                        body_len = hlen;
			cli->infocb(OBEXFTP_EV_BODY, body, body_len, cli->infocb_data);
			DEBUG(3, "%s() Done body\n", __func__);
                        /* break; */
                }
                else if(hi == OBEX_HDR_CONNECTION) {
			DEBUG(3, "%s() Found connection number: %d\n", __func__, hv.bq4);
		}
                else if(hi == OBEX_HDR_WHO) {
			DEBUG(3, "%s() Sender identified\n", __func__);
		}
                else if(hi == OBEX_HDR_APPARAM) {
			DEBUG(3, "%s() Found application parameters\n", __func__);
                        if(hlen == sizeof(apparam_t)) {
				app = (apparam_t *)hv.bs;
				 /* needed for alignment */
				memcpy(&info, &(app->info), sizeof(info));
				info = ntohl(info);
				cli->infocb(OBEXFTP_EV_INFO, (void *)info, 0, cli->infocb_data);
			}
			else
				DEBUG(3, "%s() Application parameters don't fit %d vs. %d.\n", __func__, hlen, sizeof(apparam_t));
                        break;
                }
                else    {
                        DEBUG(3, "%s() Skipped header %02x\n", __func__, hi);
                }
        }

        if(body) {
		if (body_len > 0) {
			if (cli->target_fn != NULL) {
				/* simple body writer */
				int fd;
				//fd = open_safe("", cli-> target_fn);
				fd = creat(cli-> target_fn,
					   S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
				if(fd > 0) {
					write(fd, body, body_len);
					close(fd);
				} else {
					DEBUG(3, "%s() Error writing body\n", __func__);
				}
				free (cli->target_fn);
				cli->target_fn = NULL;
			} else {
				DEBUG(3, "%s() Body not written\n", __func__);
			}
		} else {
			DEBUG(3, "%s() Skipping empty body\n", __func__);
		}
        }
        if(app) {
		DEBUG(3, "%s() Appcode %d, data (%d) %d\n", __func__,
			app->code, app->info_len, info);

        }
}


/* Incoming event from OpenOBEX. */
static void cli_obex_event(obex_t *handle, obex_object_t *object, int mode, int event, int obex_cmd, int obex_rsp)
{
	obexftp_client_t *cli;

	cli = OBEX_GetUserData(handle);

	switch (event)	{
	case OBEX_EV_PROGRESS:
		cli->infocb(OBEXFTP_EV_PROGRESS, "", 0, cli->infocb_data);
		break;
	case OBEX_EV_REQDONE:
		cli->finished = TRUE;
		if(obex_rsp == OBEX_RSP_SUCCESS)
			cli->success = TRUE;
		else
			cli->success = FALSE;
		cli->obex_rsp = obex_rsp;
		client_done(handle, object, obex_cmd, obex_rsp);
		break;
	
	case OBEX_EV_LINKERR:
		cli->finished = 1;
		cli->success = FALSE;
		break;
	
	case OBEX_EV_STREAMEMPTY:
		cli_fillstream(cli, object);
		break;
	
	default:
		DEBUG(1, "%s() Unknown event %d\n", __func__, event);
		break;
	}
}


/* Do an OBEX request sync. */
int obexftp_sync(obexftp_client_t *cli)
{
	int ret;
	DEBUG(3, "%s()\n", __func__);

	/* cli->finished = FALSE; */

	while(cli->finished == FALSE) {
		ret = OBEX_HandleInput(cli->obexhandle, 20);
		DEBUG(3, "%s() OBEX_HandleInput = %d\n", __func__, ret);

		if (ret <= 0)
			return -1;
	}

	DEBUG(3, "%s() Done success=%d\n", __func__, cli->success);

	if(cli->success)
		return 1;
	else
		return -1;
}
	
static int cli_sync_request(obexftp_client_t *cli, obex_object_t *object)
{
	DEBUG(3, "%s()\n", __func__);

	cli->finished = FALSE;
	OBEX_Request(cli->obexhandle, object);

	return obexftp_sync (cli);
}
	

/* Create an obexftp client */
obexftp_client_t *obexftp_cli_open(obexftp_info_cb_t infocb, /*const*/ obex_ctrans_t *ctrans, void *infocb_data)
{
	obexftp_client_t *cli;

	DEBUG(3, "%s()\n", __func__);
	cli = calloc (1, sizeof(obexftp_client_t));
	if(cli == NULL)
		return NULL;

	cli->infocb = infocb;
	cli->infocb_data = infocb_data;
	cli->fd = -1;

#ifdef DEBUG_TCP
	cli->obexhandle = OBEX_Init(OBEX_TRANS_INET, cli_obex_event, 0);
#else
	if ( ctrans ) {
                DEBUG(2, "Do the cable-OBEX!\n");
		cli->obexhandle = OBEX_Init(OBEX_TRANS_CUST, cli_obex_event, 0);
        }
	else
		cli->obexhandle = OBEX_Init(OBEX_TRANS_IRDA, cli_obex_event, 0);
#endif

	if(cli->obexhandle == NULL) {
		goto out_err;
	}

	if ( ctrans ) {
		/* OBEX_RegisterCTransport is const to ctrans ... */
                if(OBEX_RegisterCTransport(cli->obexhandle, ctrans) < 0) {
                        DEBUG(1, "Custom transport callback-registration failed\n");
                }
        }

	OBEX_SetUserData(cli->obexhandle, cli);
	
	/* Buffer for body */
	cli->buf = malloc(STREAM_CHUNK);
	return cli;

out_err:
	if(cli->obexhandle != NULL)
		OBEX_Cleanup(cli->obexhandle);
	free(cli);
	return NULL;
}
	
/* Close an obexftp client */
void obexftp_cli_close(obexftp_client_t *cli)
{
	DEBUG(3, "%s()\n", __func__);
	return_if_fail(cli != NULL);

	OBEX_Cleanup(cli->obexhandle);
	free(cli->buf);
	free(cli);
}

/* Do connect as client */
int obexftp_cli_connect(obexftp_client_t *cli)
{
	obex_object_t *object;
	int ret;

	DEBUG(3, "%s()\n", __func__);
	return_val_if_fail(cli != NULL, -1);

	cli->infocb(OBEXFTP_EV_CONNECTING, "", 0, cli->infocb_data);
#ifdef DEBUG_TCP
	{
		struct sockaddr_in peer;
		u_long inaddr;
		if ((inaddr = inet_addr("127.0.0.1")) != INADDR_NONE) {
			memcpy((char *) &peer.sin_addr, (char *) &inaddr,
			      sizeof(inaddr));  
		}

		ret = OBEX_TransportConnect(cli->obexhandle, (struct sockaddr *) &peer,
					  sizeof(struct sockaddr_in));
	}
		
#else
	ret = IrOBEX_TransportConnect(cli->obexhandle, "OBEX");
	if (ret == -1 /* -ESOCKTNOSUPPORT */)
		ret = OBEX_TransportConnect(cli->obexhandle, NULL, 0);
#endif
	if (ret < 0) {
		/* could be -EBUSY or -ESOCKTNOSUPPORT */
		cli->infocb(OBEXFTP_EV_ERR, "connect", 0, cli->infocb_data);
		return -1;
	}

	object = OBEX_ObjectNew(cli->obexhandle, OBEX_CMD_CONNECT);
	if(OBEX_ObjectAddHeader(cli->obexhandle, object, OBEX_HDR_TARGET,
                                (obex_headerdata_t) UUID_S45,
                                sizeof(UUID_S45),
                                OBEX_FL_FIT_ONE_PACKET) < 0)    {
                DEBUG(1, "Error adding header\n");
                OBEX_ObjectDelete(cli->obexhandle, object);
                return -1;
        }
	ret = cli_sync_request(cli, object);

	if(ret < 0)
		cli->infocb(OBEXFTP_EV_ERR, "target", 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, "", 0, cli->infocb_data);

	return ret;
}

/* Do disconnect as client */
int obexftp_cli_disconnect(obexftp_client_t *cli)
{
	obex_object_t *object;
	int ret;

	DEBUG(3, "%s()\n", __func__);
	return_val_if_fail(cli != NULL, -1);

	cli->infocb(OBEXFTP_EV_DISCONNECTING, "", 0, cli->infocb_data);

	object = OBEX_ObjectNew(cli->obexhandle, OBEX_CMD_DISCONNECT);
	ret = cli_sync_request(cli, object);

	if(ret < 0)
		cli->infocb(OBEXFTP_EV_ERR, "disconnect", 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, "", 0, cli->infocb_data);

	/* don't -- obexftp_cli_close will handle this with OBEX_Cleanup */
	/* OBEX_TransportDisconnect(cli->obexhandle); */
	return ret;
}

/* Do an OBEX GET with app info opcode. */
int obexftp_info(obexftp_client_t *cli, uint8_t opcode)
{
	obex_object_t *object = NULL;
	int ret;

	return_val_if_fail(cli != NULL, -1);

	cli->infocb(OBEXFTP_EV_RECEIVING, "info", 0, cli->infocb_data);

	DEBUG(2, "%s() Retrieving info %d\n", __func__, opcode);

        object = obexftp_build_info (cli->obexhandle, opcode);
        if(object == NULL)
                return -1;
 
	ret = cli_sync_request(cli, object);
		
	if(ret < 0) 
		cli->infocb(OBEXFTP_EV_ERR, "info", 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, "info", 0, cli->infocb_data);

	return ret;
}

/* Do an OBEX GET with TYPE. Directories will be changed into first. */
int obexftp_list(obexftp_client_t *cli, const char *localname, const char *remotename)
{
	obex_object_t *object = NULL;
	int ret;

	return_val_if_fail(cli != NULL, -1);
	return_val_if_fail(remotename != NULL, -1);

	cli->infocb(OBEXFTP_EV_RECEIVING, remotename, 0, cli->infocb_data);

	DEBUG(2, "%s() Listing %s -> %s\n", __func__, remotename, localname);

	if (localname && *localname)
		cli->target_fn = strdup(localname);
	else
		cli->target_fn = NULL;

	while (*remotename == '/') remotename++;
        object = obexftp_build_list (cli->obexhandle, remotename);
        if(object == NULL)
                return -1;

	ret = cli_sync_request(cli, object);
		
	if(ret < 0) 
		cli->infocb(OBEXFTP_EV_ERR, remotename, 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, remotename, 0, cli->infocb_data);

	return ret;
}


/* Do an OBEX GET. Directories will be changed into first. */
int obexftp_get(obexftp_client_t *cli, const char *localname, const char *remotename)
{
	obex_object_t *object = NULL;
	int ret;
	int depth;
	const char *p;

	return_val_if_fail(cli != NULL, -1);
	return_val_if_fail(remotename != NULL, -1);

	cli->infocb(OBEXFTP_EV_RECEIVING, remotename, 0, cli->infocb_data);

	depth = setpath(cli, remotename);
	if ((p = strrchr(remotename, '/')))
		p++;
	else
		p = remotename;

	DEBUG(2, "%s() Getting %s -> %s\n", __func__, p, localname);

	if (localname && *localname)
		cli->target_fn = strdup(localname);
	else
		cli->target_fn = NULL;

        object = obexftp_build_get (cli->obexhandle, p);
        if(object == NULL)
                return -1;
	
	ret = cli_sync_request(cli, object);

	while (depth-- > 0)
		(void) obexftp_setpath(cli, NULL);
		
	if(ret < 0)
		cli->infocb(OBEXFTP_EV_ERR, remotename, 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, remotename, 0, cli->infocb_data);

	return ret;
}


/* Do an OBEX rename. */
int obexftp_rename(obexftp_client_t *cli, const char *sourcename, const char *targetname)
{
	obex_object_t *object = NULL;
	int ret;

	return_val_if_fail(cli != NULL, -1);

	cli->infocb(OBEXFTP_EV_SENDING, sourcename, 0, cli->infocb_data);

	DEBUG(2, "%s() Moving %s -> %s\n", __func__, sourcename, targetname);

        object = obexftp_build_rename (cli->obexhandle, sourcename, targetname);
        if(object == NULL)
                return -1;
	
	ret = cli_sync_request(cli, object);
		
	if(ret < 0)
		cli->infocb(OBEXFTP_EV_ERR, sourcename, 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, sourcename, 0, cli->infocb_data);

	return ret;
}


/* Do an OBEX PUT with empty file (delete) */
int obexftp_del(obexftp_client_t *cli, const char *name)
{
	obex_object_t *object;
	int ret;

	return_val_if_fail(cli != NULL, -1);

	cli->infocb(OBEXFTP_EV_SENDING, name, 0, cli->infocb_data);

	DEBUG(2, "%s() Deleting %s\n", __func__, name);

        object = obexftp_build_del (cli->obexhandle, name);
        if(object == NULL)
                return -1;
	
	ret = cli_sync_request(cli, object);
	
	if(ret < 0)
		cli->infocb(OBEXFTP_EV_ERR, name, 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, name, 0, cli->infocb_data);

	return ret;
}


/* Do OBEX SetPath -- change up if name is NULL */
int obexftp_setpath(obexftp_client_t *cli, const char *name)
{
	obex_object_t *object;
	int ret = 0;
	char *copy, *tail, *p;

	return_val_if_fail(cli != NULL, -1);

	cli->infocb(OBEXFTP_EV_SENDING, name, 0, cli->infocb_data); /* FIXME */

	DEBUG(2, "%s() Changing to %s\n", __func__, name);

	if (name) {
		while (*name == '/') name++;
		tail = copy = strdup(name);

		for (p = strchr(tail, '/'); tail; ) {
			if (p) {
				*p = '\0';
				p++;
			}
	
			cli->infocb(OBEXFTP_EV_SENDING, tail, 0, cli->infocb_data);
			object = obexftp_build_setpath (cli->obexhandle, tail);
			ret = cli_sync_request(cli, object);
			if(ret < 0) break;

			tail = p;
			if(p) p = strchr(p, '/');
		}
		free(copy);
	} else {
		object = obexftp_build_setpath (cli->obexhandle, name);
		ret = cli_sync_request(cli, object);
	}

	if(ret < 0)
		cli->infocb(OBEXFTP_EV_ERR, name, 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, name, 0, cli->infocb_data);

	return ret;
}

/* Do an OBEX PUT. */
int obexftp_put_file(obexftp_client_t *cli, const char *localname, const char *remotename)
{
	obex_object_t *object;
	int ret;
	int depth = 0;
	const char *p;

	return_val_if_fail(cli != NULL, -1);
	return_val_if_fail(localname != NULL, -1);

	cli->infocb(OBEXFTP_EV_SENDING, localname, 0, cli->infocb_data);

	if (!remotename) {
		remotename = strrchr(localname, '/');
		if (remotename)
			remotename++;
	}

	depth = setpath(cli, remotename);
	if ((p = strrchr(remotename, '/')))
		p++;
	else
		p = remotename;

	while (*remotename == '/') remotename++;
	DEBUG(2, "%s() Sending %s -> %s\n", __func__, localname, remotename);

	object = build_object_from_file (cli->obexhandle, localname, remotename);
	
	cli->fd = open(localname, O_RDONLY, 0);
	if(cli->fd < 0)
		ret = -1;
	else
		ret = cli_sync_request(cli, object);
	
	/* close(cli->fd); */

	while (depth-- > 0)
		(void) obexftp_setpath(cli, NULL);
		
	if(ret < 0)
		cli->infocb(OBEXFTP_EV_ERR, localname, 0, cli->infocb_data);
	else
		cli->infocb(OBEXFTP_EV_OK, localname, 0, cli->infocb_data);

	return ret;
}

/* Callback from dirtraverse. */
static int obexftp_visit(int action, const char *name, const char *path, void *userdata)
{
	const char *remotename;
	int ret = -1;

	DEBUG(3, "%s()\n", __func__);
	switch(action) {
	case VISIT_FILE:
		/* Strip /'s before sending file */
		remotename = strrchr(name, '/');
		if(remotename == NULL)
			remotename = name;
		else
			remotename++;
		ret = obexftp_put_file(userdata, name, remotename);
		break;

	case VISIT_GOING_DEEPER:
		ret = obexftp_setpath(userdata, name);
		break;

	case VISIT_GOING_UP:
		ret = obexftp_setpath(userdata, NULL);
		break;
	}
	DEBUG(3, "%s() returning %d\n", __func__, ret);
	return ret;
}

/* Put file or directory */
int obexftp_put(obexftp_client_t *cli, const char *name)
{
	struct stat statbuf;
	char *origdir;
	int ret;
	
	/* Remember cwd */
	if((origdir = malloc(PATH_MAX)) == NULL)
		return -1;
	if((origdir = getcwd(origdir, PATH_MAX)) == NULL)
		return -1;

	if(stat(name, &statbuf) == -1) {
		return -1;
	}
	
	/* This is a directory. CD into it */
	if(S_ISDIR(statbuf.st_mode)) {
		char *newdir;
		char *dirname;
		
		chdir(name);
		name = ".";
		
		/* Get real name of new wd, extract last part of and do setpath to it */
		if((newdir = malloc(PATH_MAX)) == NULL)
			return -1;
		newdir = getcwd(newdir, PATH_MAX);
		dirname = strrchr(newdir, '/') + 1;
		if(strlen(dirname) != 0)
			obexftp_setpath(cli, dirname);

		free(newdir);
	}
	
	ret = visit_all_files(name, obexftp_visit, cli);

	chdir(origdir);
	free(origdir);
	return ret;
}
