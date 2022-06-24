#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xproto.h>
#include <xcb/xinerama.h>
#include <xcb/xcb_icccm.h>

#define MFACT 0.55
#define TILE

// Struct Definitions

typedef struct Client Client;
typedef struct Monitor Monitor;
typedef xcb_window_t Window;

struct Client {
	char name[256];
	int x, y, w, h;
	int bw;
	Client *next;
	Monitor *mon;
	Window window;
	bool isfixed, isfloating, isurgent, isfullscreen;
};

struct Monitor {
	float mfact;
	int nmaster;
	int num;
	int x, y, w, h;
	Client *clients;
	Client *sel;
	Monitor* next;
};

// Variable Definitions
static xcb_connection_t *conn;
static Monitor *monitors;
static Window root;
static Monitor *selmon;

// Function declarations
static void arrange(Monitor *);
static void setup(void);
static void run(void);
static void tile(Monitor *);
static void resize(Client *, int, int, int, int);
static void updategeometry(void);
static Monitor *createmonitor();
static void maprequest(xcb_generic_event_t *);
static void manage(Window, xcb_get_window_attributes_reply_t *);
static void showhide(Client *);
static void restack(Monitor *);

Monitor *
createmonitor(void) {
	Monitor *mon = malloc(sizeof(Monitor));
	mon->mfact = MFACT;
	mon->nmaster = 1;
	return mon;
}

void
updategeometry(void) {
	xcb_xinerama_query_screens_reply_t *reply;
	xcb_xinerama_screen_info_t *info;
	Monitor *mon = monitors;

	reply = xcb_xinerama_query_screens_reply(conn, xcb_xinerama_query_screens(conn), NULL);

	info = xcb_xinerama_query_screens_screen_info(reply);
	int screens = xcb_xinerama_query_screens_screen_info_length(reply);

	for (int screen = 0; screen < screens; screen++) {
		for (mon = monitors; mon && monitors->next; mon = mon->next);
		if(mon) {
			mon->next = createmonitor();
		} else {
			monitors = createmonitor();
			selmon = monitors;
			mon = monitors;
		}
		mon->x = info[screen].x_org;
		mon->y = info[screen].y_org;
		mon->w = info[screen].width;
		mon->h = info[screen].height;
	}

	free(reply);
}

void
showhide(Client *c) {
	if (!c)
		return;

	/* Since there's no tags implemented, this check always pass */
	if (1) {
		/* show clients top down */

		uint16_t mask = XCB_CONFIG_WINDOW_X
		| XCB_CONFIG_WINDOW_Y;
		const uint32_t values[] = {
			c->x,
			c->y,
		};
		xcb_configure_window(conn, c->window, mask, values);
		if (c->isfloating && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h);
		showhide(c->next);
	} else {
		/* hide clients bottom up */
		showhide(c->next);

		uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
		const uint32_t values[] = {
			c->w * -2,
			c->y,
		};
		xcb_configure_window(conn, c->window, mask, values);
	}
}

void
tile(Monitor *m) {
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = m->clients; c; c = c->next, n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->w * m->mfact;
	else
		mw = m->w;
	for (i = 0, my = ty = 0, c = m->clients; c; c = c->next, i++)
		if (i < m->nmaster) {
			h = (m->h - my) / (1 - i);
			resize(c, m->x, m->y + my, mw - (2*c->bw), h - (2*c->bw));
			if (my + c->h < m->h)
				my += c->h;
		} else {
			h = (m->h - ty) / (n - i);
			resize(c, m->x + mw, m->y + ty, m->w - mw - (2*c->bw), h - (2*c->bw));
			if (ty + c->h < m->h)
				ty += c->h;
		}
}

void 
arrange(Monitor *m) {
	if(m)
		showhide(m->clients);
	else for (m = monitors; m; m = m->next)
		showhide(m->clients);

#ifdef TILE
	if(m) {
		tile(m);
	} else for (m = monitors; m; m = m->next)
		tile(m);
#endif
}

void
manage(Window window, xcb_get_window_attributes_reply_t *wa) {
	Client *c = malloc(sizeof(Client));
	c->window = window;
	xcb_size_hints_t hints;
	xcb_icccm_get_wm_size_hints_reply(conn,
		xcb_icccm_get_wm_size_hints(conn, c->window, XCB_ATOM_WM_SIZE_HINTS),
		&hints, NULL);

	c->x = hints.x;
	c->y = hints.y;
	c->h = hints.base_height;
	c->w = hints.base_width;

	c->mon = selmon;
	selmon->sel = c;

	c->next = c->mon->clients;
	c->mon->clients = c;

	xcb_map_window(conn, window);

	arrange(c->mon);
}

void
resize(Client *c, int x, int y, int w, int h) {

	c->x = x;
	c->y = y;
	c->h = h;
	c->w = w;

	uint16_t mask = XCB_CONFIG_WINDOW_X
	| XCB_CONFIG_WINDOW_Y
	| XCB_CONFIG_WINDOW_WIDTH
	| XCB_CONFIG_WINDOW_HEIGHT;

	const uint32_t values[] = {
		c->x,
		c->y,
		c->w,
		c->h,
	};
	xcb_configure_window(conn, c->window, mask, values);
	xcb_flush(conn);
}

void maprequest(xcb_generic_event_t *e) {
	xcb_map_request_event_t *event = (xcb_map_request_event_t *)e;
	xcb_get_window_attributes_reply_t *wa;

	wa = xcb_get_window_attributes_reply(conn,
			xcb_get_window_attributes(conn, event->window), NULL);

	if(!wa || wa->override_redirect)
		return;
	
	manage(event->window, wa);
	free(event);
}

void unmapnotify(xcb_generic_event_t *e) {
	Monitor *m;
	Client *c, *old;
	xcb_unmap_notify_event_t *event = (xcb_unmap_notify_event_t *)e;
	for(m = monitors; m; m = m->next)
		for(old = c = m->clients; c; old = c, c = c->next)
			if(c->window == event->window)
				break;

	old->next = c->next;
	
	for(m = monitors; m; m = m->next)
		tile(m);

	free(c);
	free(event);
}

void
setup(void) {
	conn = xcb_connect(NULL, NULL);

	if (xcb_connection_has_error(conn)) {
		printf("Cannot open display\n");
		exit(EXIT_FAILURE);
	}

	root = xcb_setup_roots_iterator(xcb_get_setup(conn)).data->root;

	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t value[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
	};

	xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(conn, root, mask, value);

	if (xcb_request_check(conn, cookie)) {
		fprintf(stderr, "Another WM is already running\n");
	}
	updategeometry();
}

int 
main(int argc, char* argv[]) {

	setup();
	xcb_generic_event_t *event;
	while ((event = xcb_wait_for_event(conn))) {
		switch (event->response_type) {
			case XCB_MAP_REQUEST:
				printf("MAP\n");
				maprequest(event);
				break;

			case XCB_UNMAP_NOTIFY:
				printf("UNMAP\n");
				unmapnotify(event);
				break;

			default:
				printf("%d", event->response_type);
				free(event);
				break;
		}
	}
	return 0;
}
