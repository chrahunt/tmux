/* $OpenBSD$ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicholas.marriott@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <string.h>

#include "tmux.h"

void
recalculate_sizes(void)
{
	struct session		*s;
	struct client		*c;
	struct window		*w;
	struct window_pane	*wp;
	u_int			 sx, sy, cx, cy;
	int			 flags, type, current, has, zoomed;

	/* Clear attached flags for each session. */
	RB_FOREACH(s, sessions, &sessions) {
	    s->flags |= SESSION_UNATTACHED;
	    s->attached = 0;
	    status_update_saved(s);
	}

	/* Set attached flags for any attached sessions. */
	TAILQ_FOREACH(c, &clients, entry) {
		if ((s = c->session) == NULL)
			continue;

		flags = c->flags;
		if (flags & CLIENT_SUSPENDED)
			continue;
		if ((flags & CLIENT_CONTROL) && (~flags & CLIENT_SIZECHANGED))
			continue;

		if (c->tty.sy <= tty_status_lines(c))
			c->flags |= CLIENT_STATUSOFF;

		s->flags &= ~SESSION_UNATTACHED;
		s->attached++;
	}

	/* Walk each window and adjust the size. */
	RB_FOREACH(w, windows, &windows) {
		if (w->active == NULL)
			continue;

		type = options_get_number(w->options, "window-size");
		if (type == WINDOW_SIZE_MANUAL)
			continue;
		current = !options_get_number(w->options, "aggressive-resize");

		if (type == WINDOW_SIZE_LARGEST) {
			sx = sy = 0;
			TAILQ_FOREACH(c, &clients, entry) {
				if ((s = c->session) == NULL)
					continue;
				if (current)
					has = (s->curw->window == w);
				else
					has = session_has(s, w);
				if (!has)
					continue;

				cx = c->tty.sx;
				cy = c->tty.sy - tty_status_lines(c);

				if (cx > sx)
					sx = cx;
				if (cx > sy)
					sy = cy;
			}
			if (sx == 0 || sy == 0)
				continue;
		} else if (type == WINDOW_SIZE_SMALLEST) {
			sx = sy = UINT_MAX;
			TAILQ_FOREACH(c, &clients, entry) {
				if ((s = c->session) == NULL)
					continue;
				if (current)
					has = (s->curw->window == w);
				else
					has = session_has(s, w);
				if (!has)
					continue;

				cx = c->tty.sx;
				cy = c->tty.sy - tty_status_lines(c);

				if (cx < sx)
					sx = cx;
				if (cx < sy)
					sy = cy;
			}
			if (sx == UINT_MAX || sy == UINT_MAX)
				continue;
		}

		if (w->sx == sx && w->sy == sy)
			continue;
		log_debug("%s: @%u now %u,%u (was %u,%u)", __func__, w->id,
		    sx, sy, w->sx, w->sy);

		/* Resize the window. */
		zoomed = w->flags & WINDOW_ZOOMED;
		if (zoomed)
			window_unzoom(w);
		layout_resize(w, sx, sy);
		window_resize(w, sx, sy);
		if (zoomed && window_pane_visible(w->active))
			window_zoom(w->active);

		/* If the current pane is now not visible, move to the next. */
		wp = w->active;
		while (!window_pane_visible(w->active)) {
			w->active = TAILQ_PREV(w->active, window_panes, entry);
			if (w->active == NULL)
				w->active = TAILQ_LAST(&w->panes, window_panes);
			if (w->active == wp)
			       break;
		}
		if (w->active == w->last)
			w->last = NULL;

		server_redraw_window(w);
		notify_window("window-layout-changed", w);
	}
}
