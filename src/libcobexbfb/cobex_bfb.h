/*
 *                
 * Filename:      cobex_bfb.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Christian W. Zuckschwerdt <zany@triq.net>
 * Created at:    Don, 17 Jan 2002 18:27:25 +0100
 * Modified at:   Don, 17 Jan 2002 23:46:52 +0100
 * Modified by:   Christian W. Zuckschwerdt <zany@triq.net>
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

/* session handling */

obex_ctrans_t *cobex_ctrans (const gchar *tty);
void cobex_free (obex_ctrans_t * ctrans);

/* callbacks */

gint cobex_connect (obex_t *self, gpointer userdata);
gint cobex_disconnect (obex_t *self, gpointer userdata);
gint cobex_write (obex_t *self, gpointer userdata, guint8 *buffer, gint length);
gint cobex_handleinput (obex_t *self, gpointer userdata, gint timeout);

/* private */

int cobex_init (obex_t *self, const gchar *ttyname);
void cobex_cleanup (obex_t *self, int force);
int cobex_start_io (void);