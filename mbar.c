/*
 * Copyright (c) 2019 Pierre Evenou
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <string.h>
#include <time.h>
#include <sys/select.h>

#include <X11/Xlib.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/randr.h>
#include <cairo-xcb.h>

#include <libudev.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/wireless.h>
#include <iwlib.h>

#define BAR_HEIGHT 32

/* clock config */
#define CLOCK_FORMAT                            "%a %b %d, %H:%M"

/* wireless  config */
#define WIRELESS_INTERFACE                      "wlp4s0"
#define WIRELESS_RIGHT_OFFSET                   150

/* battery config */
#define BATTERY_DEVICE                          "BAT0"
#define BATTERY_WARNING_CAPACITY_THRESHOLD      30
#define BATTERY_CRITICAL_CAPACITY_THRESHOLD     10

/*
 * types
 */
enum {
    MWM_MONITOR_TAGS,
    MWM_MONITOR_TAGSET,
    MWM_FOCUSED,
    MWM_FOCUSED_TAGSET,
    MWM_ATOM_COUNT
};

typedef enum {
    WST_UNK,
    WST_OFF,
    WST_DIS,
    WST_CON
} WState;

typedef enum {
    BST_UNK,
    BST_FUL,
    BST_CHA,
    BST_DIS
} BState;

/*
 * function declarations
 */
static int on_short_timer(void*);
static int on_long_timer(void*);
static void bar_set_geometry();
static void clear_tags(GtkWidget *widget, gpointer data);
static void hints_update();
static void clock_update();
static void wireless_update();
static void battery_update();
static void battery_update();

/*
 * variables
 */
/* xcb */
static xcb_connection_t *xcb;
static xcb_screen_t     *screen;
static xcb_atom_t       atoms[MWM_ATOM_COUNT];

/* gui */
static GtkWindow    *bar;
static GtkBox       *tag_box;
static GtkLabel     *clock_label;
static GtkLabel     *focused_label;
static GtkLabel     *wireless_label;
static GtkLabel     *battery_label;

/*
 * function definitions
 */
void bar_set_geometry()
{
    GdkRectangle geometry;
    GdkDisplay *display;
    GdkMonitor *monitor;
    gulong struts[12] = {0,};
    GdkAtom strut_partial_atom = NULL;
    GdkAtom cardinal_atom = NULL;

    /* move resize the bar */
    display = gdk_display_get_default();
    monitor = gdk_display_get_primary_monitor(display);
    gdk_monitor_get_geometry(monitor, &geometry);

    gtk_window_move(bar, geometry.x, geometry.y);
    gtk_widget_set_size_request(GTK_WIDGET(bar), geometry.width, BAR_HEIGHT);
    gtk_window_set_resizable(bar, FALSE);

    /* reserve space */
    strut_partial_atom = gdk_atom_intern_static_string("_NET_WM_STRUT_PARTIAL");
    cardinal_atom = gdk_atom_intern_static_string("CARDINAL");
    struts[2] = geometry.y + BAR_HEIGHT + 1;
    struts[8] = geometry.x;
    struts[9] = geometry.x + geometry.width;
    gdk_property_change(
            gtk_widget_get_window(GTK_WIDGET(bar)),
            strut_partial_atom,
            cardinal_atom, 32, GDK_PROP_MODE_REPLACE,
            (guchar *)&struts, 12);
    gdk_display_flush(gdk_display_get_default());
}

int on_short_timer(void *data)
{
    clock_update();
    return TRUE;
}

int on_long_timer(void *data)
{
    wireless_update();
    battery_update();
    return TRUE;
}

void clear_tags(GtkWidget *widget, gpointer data)
{
    gtk_widget_destroy(widget);
}

void hints_update()
{
    int hints_focused_monitor_tags[32];
    int hints_focused_monitor_tagset;
    char hints_focused_client_class[1024];
    int hints_focused_client_tagset;

    /* retrieve info from the root window */
    xcb_get_property_cookie_t cookies[MWM_ATOM_COUNT];
    xcb_get_property_reply_t *replies[MWM_ATOM_COUNT];

    cookies[MWM_MONITOR_TAGS] = xcb_get_property(
            xcb,
            0,
            screen->root,
            atoms[MWM_MONITOR_TAGS],
            XCB_ATOM_CARDINAL,
            0, 32);

    cookies[MWM_MONITOR_TAGSET] = xcb_get_property(
            xcb,
            0,
            screen->root,
            atoms[MWM_MONITOR_TAGSET],
            XCB_ATOM_INTEGER,
            0, 1);

    cookies[MWM_FOCUSED] = xcb_get_property(
            xcb,
            0,
            screen->root,
            atoms[MWM_FOCUSED],
            XCB_ATOM_WINDOW,
            0, -1);

    cookies[MWM_FOCUSED_TAGSET] = xcb_get_property(
            xcb,
            0,
            screen->root,
            atoms[MWM_FOCUSED_TAGSET],
            XCB_ATOM_INTEGER,
            0, 1);

    for (int i = 0; i < MWM_ATOM_COUNT; ++i)
        replies[i] = xcb_get_property_reply(xcb, cookies[i], NULL);

    if (replies[MWM_MONITOR_TAGS]) {
        memcpy(hints_focused_monitor_tags,
               xcb_get_property_value(replies[MWM_MONITOR_TAGS]),
               32 * sizeof(int));
        free(replies[MWM_MONITOR_TAGS]);
    }

    if (replies[MWM_MONITOR_TAGSET]) {
        int *ts = xcb_get_property_value(replies[MWM_MONITOR_TAGSET]);
        hints_focused_monitor_tagset = *ts;
        free(replies[MWM_MONITOR_TAGSET]);
    }

    strcpy(hints_focused_client_class, "None");
    if (replies[MWM_FOCUSED]) {
        xcb_window_t *window = xcb_get_property_value(replies[MWM_FOCUSED]);
        xcb_get_property_reply_t *class = xcb_get_property_reply(
                xcb,
                xcb_get_property(
                    xcb,
                    0,
                    *window,
                    XCB_ATOM_WM_CLASS,
                    XCB_ATOM_STRING,
                    0, -1),
                NULL);
        if (class) {
            char *cls = xcb_get_property_value(class);
            strncpy(hints_focused_client_class, cls + strlen(cls) + 1, 256);
            free(class);
        }

        free(replies[MWM_FOCUSED]);
    }

    if (replies[MWM_FOCUSED_TAGSET]) {
        int *ts = xcb_get_property_value(replies[MWM_FOCUSED_TAGSET]);
        hints_focused_client_tagset = *ts;
        free(replies[MWM_FOCUSED_TAGSET]);
    }

    /* refresh the bar */
    gtk_container_foreach(GTK_CONTAINER(tag_box), clear_tags, NULL);
    for (int i = 0; i < 32; ++i) {
        if (hints_focused_monitor_tags[i] ||
                hints_focused_monitor_tagset & (1L << i)) {
            GtkWidget *frame;
            GtkWidget *label;
            gchar str[2];
            sprintf(str, "%d", i + 1);
            frame = gtk_frame_new(NULL);
            label = gtk_label_new(str);
            gtk_container_add(GTK_CONTAINER(frame), label);
            GtkStyleContext *context = gtk_widget_get_style_context(frame);
            gtk_style_context_add_class(context, "flat");
            gtk_style_context_add_class(context, "tag");
            if (hints_focused_monitor_tagset & (1L << i))
                gtk_style_context_add_class(context, "match-monitor-tagset");
            if (hints_focused_client_tagset & (1L << i))
                gtk_style_context_add_class(context, "match-client-tagset");
            gtk_box_pack_start(GTK_BOX(tag_box), frame, FALSE, FALSE, 0);
        }
    }
    gtk_widget_show_all(GTK_WIDGET(tag_box));

    gtk_label_set_label(focused_label, hints_focused_client_class);
}

void clock_update()
{
    gchar str[32];
    time_t t;
    time(&t);
    strftime(str, sizeof(str), CLOCK_FORMAT, localtime(&t));
    gtk_label_set_label(clock_label, str);
}

void wireless_update()
{
    GtkStyleContext *context;
    int iw_socket;
    char wireless_ssid[IW_ESSID_MAX_SIZE + 1];
    int wireless_quality = 0;;
    WState wireless_state;
    struct wireless_info winfo;
    char str[128];

    iw_socket = iw_sockets_open();
    memset(&winfo, 0, sizeof(struct wireless_info));

    wireless_state = WST_UNK;
    if (iw_get_basic_config(iw_socket, WIRELESS_INTERFACE, &(winfo.b)) > -1) {
        if (winfo.b.has_essid && winfo.b.essid_on) {
            wireless_state = WST_CON;
            strncpy(wireless_ssid, winfo.b.essid, IW_ESSID_MAX_SIZE + 1);
        } else {
            wireless_state = WST_DIS;
        }

        if (iw_get_range_info(iw_socket, WIRELESS_INTERFACE, &(winfo.range)) >= 0)
            winfo.has_range = 1;

        if (iw_get_stats(iw_socket,
                    WIRELESS_INTERFACE,
                    &(winfo.stats),
                    &winfo.range,
                    winfo.has_range) >= 0)
            winfo.has_stats = 1;

        if (winfo.has_range &&
                winfo.has_stats &&
                ((winfo.stats.qual.level != 0)
                || (winfo.stats.qual.updated & IW_QUAL_DBM))) {
            if (!(winfo.stats.qual.updated & IW_QUAL_QUAL_INVALID)) {
                wireless_quality = winfo.stats.qual.qual * 100 /
                    winfo.range.max_qual.qual;
            }
        }
    }

    iw_sockets_close(iw_socket);

    context = gtk_widget_get_style_context(GTK_WIDGET(wireless_label));
    gtk_style_context_remove_class(context, "warning");
    gtk_style_context_remove_class(context, "critical");
    /* refresh the wireless label */
    switch (wireless_state) {
        case WST_OFF:
            sprintf(str, "%s: Off", WIRELESS_INTERFACE);
            gtk_style_context_add_class(context, "critical");
            break;
        case WST_DIS:
            sprintf(str, "%s: Disconnected", WIRELESS_INTERFACE);
            gtk_style_context_add_class(context, "warning");
            break;
        case WST_UNK:
            sprintf(str, "%s: Error", WIRELESS_INTERFACE);
            gtk_style_context_add_class(context, "critical");
            break;
        case WST_CON:
            sprintf(str, "%s: %s, %d%%", WIRELESS_INTERFACE, wireless_ssid, wireless_quality);
            break;
    }
    gtk_label_set_label(wireless_label, str);
}

void battery_update()
{
    BState state;
    GtkStyleContext *context;
    int capacity = 0;
    struct udev_device *dev;
    const char *attr;
    long efull = 0;
    long enow = 0;
    long pnow = 1;
    char str[128];

    state = BST_UNK;
    capacity = 0;

    dev = udev_device_new_from_subsystem_sysname(
            NULL,
            "power_supply",
            BATTERY_DEVICE);
    if (! dev)
        return;

    attr = udev_device_get_property_value(dev, "POWER_SUPPLY_STATUS");
    if (attr) {
        if (strcmp(attr, "Full") == 0)
            state = BST_FUL;
        else if (strcmp(attr, "Charging") == 0)
            state = BST_CHA;
        else if (strcmp(attr, "Discharging") == 0)
            state = BST_DIS;
        else
            state = BST_UNK;
    }

    attr = udev_device_get_property_value(dev, "POWER_SUPPLY_CAPACITY");
    if (attr)
        capacity = atoi(attr);

    attr = udev_device_get_property_value(dev, "POWER_SUPPLY_ENERGY_NOW");
    if (attr)
        enow = atol(attr);

    attr = udev_device_get_property_value(dev, "POWER_SUPPLY_ENERGY_FULL");
    if (attr)
        efull = atol(attr);

    attr = udev_device_get_property_value(dev, "POWER_SUPPLY_POWER_NOW");
    if (attr)
        pnow = atol(attr);

    udev_device_unref(dev);

    context = gtk_widget_get_style_context(GTK_WIDGET(battery_label));
    gtk_style_context_remove_class(context, "warning");
    gtk_style_context_remove_class(context, "critical");
    switch (state) {
        case BST_CHA:
            {
                int rm = (int) (((float)(efull - enow) / (float)pnow) * 3600.00);
                int hr = rm / 3600;
                int mn = (rm % 3600) / 60;
                sprintf(str, "%s: Char. %d%%, %.2d:%.2d",
                        BATTERY_DEVICE,
                        capacity,
                        hr,
                        mn);
            }
            break;
        case BST_DIS:
            {
                int rm = (int) (((float)enow / (float)pnow) * 3600.00);
                int hr = rm / 3600;
                int mn = (rm % 3600) / 60;
                sprintf(str, "%s: Disc. %d%%, %.2d:%.2d",
                        BATTERY_DEVICE,
                        capacity,
                        hr,
                        mn);
                if (capacity < BATTERY_WARNING_CAPACITY_THRESHOLD)
                    gtk_style_context_add_class(context, "warning");
                if (capacity < BATTERY_CRITICAL_CAPACITY_THRESHOLD)
                    gtk_style_context_add_class(context, "critical");
            }
            break;
        case BST_FUL:
                sprintf(str, "%s: Full", BATTERY_DEVICE);
                gtk_style_context_add_class(context, "warning");
            break;
        case BST_UNK:
                sprintf(str, "%s: Error", BATTERY_DEVICE);
                gtk_style_context_add_class(context, "critical");
            break;
    }

    gtk_label_set_label(battery_label, str);

}

static GdkFilterReturn on_property_changed(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
    XEvent *e = (XEvent*)xevent;
    if (e->type == PropertyNotify) {
        XPropertyEvent *xpe = (XPropertyEvent*) xevent;
        if (xpe->atom == atoms[MWM_MONITOR_TAGS] ||
                xpe->atom == atoms[MWM_MONITOR_TAGSET] ||
                xpe->atom == atoms[MWM_FOCUSED] ||
                xpe->atom == atoms[MWM_FOCUSED_TAGSET]) {
            hints_update();
        }
    }
    return GDK_FILTER_REMOVE;
}

int main(int argc, char**argv)
{
    int sid;
    GtkBuilder *builder;
    GtkCssProvider *provider;

    /* initialize */
    gtk_init(&argc, &argv);

    xcb = xcb_connect(0, &sid);
    if (xcb_connection_has_error (xcb)) {
        fprintf(stderr, "can't get xcb connection.");
        exit(EXIT_FAILURE);
    }

    const char *atom_names[] = {
        "MWM_MONITOR_TAGS",
        "MWM_MONITOR_TAGSET",
        "MWM_FOCUSED",
        "MWM_FOCUSED_TAGSET"
    };

    xcb_intern_atom_cookie_t cookies[MWM_ATOM_COUNT];
    for (int i = 0; i < MWM_ATOM_COUNT; ++i)
        cookies[i] = xcb_intern_atom(
                xcb,
                0,
                strlen(atom_names[i]),
                atom_names[i]);

    /* find the screen */
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup (xcb));
    for (int i = 0; i < sid; ++i)
        xcb_screen_next(&it);
    screen = it.data;

    for (int i = 0; i < MWM_ATOM_COUNT; ++i) {
        xcb_intern_atom_reply_t *a = xcb_intern_atom_reply(
                xcb,
                cookies[i],
                NULL);

        if (a) {
            atoms[i] = a->atom;
            free(a);
        }
    }

    /* catch events of interest from the root */
    GdkWindow *groot = gdk_get_default_root_window();
    gdk_window_set_events(groot, GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(groot, on_property_changed, NULL);

    /* build the interface */
    builder = gtk_builder_new_from_resource("/mbar/mbar.ui");

    /* retrieve object of interrest */
    bar = GTK_WINDOW(gtk_builder_get_object(builder, "bar"));
    clock_label = GTK_LABEL(gtk_builder_get_object(builder, "clock_label"));
    tag_box = GTK_BOX(gtk_builder_get_object(builder, "tag_box"));
    focused_label = GTK_LABEL(gtk_builder_get_object(builder, "focused_label"));
    wireless_label = GTK_LABEL(gtk_builder_get_object(builder, "wireless_label"));
    battery_label = GTK_LABEL(gtk_builder_get_object(builder, "battery_label"));

    /* finalize */
    bar_set_geometry();
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(
            GTK_CSS_PROVIDER(provider),
            "/mbar/mbar.css");
    gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);

    /* first update */
    clock_update();
    hints_update();
    wireless_update();
    battery_update();

    g_timeout_add_seconds(1, on_short_timer, NULL);
    g_timeout_add_seconds(5, on_long_timer, NULL);

    gtk_main();

    return 0;
}
