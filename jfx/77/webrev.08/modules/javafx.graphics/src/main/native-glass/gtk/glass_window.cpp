/*
 * Copyright (c) 2011, 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
#include "glass_window.h"
#include "glass_key.h"
#include "glass_screen.h"
#include "glass_dnd.h"

#include <com_sun_glass_events_WindowEvent.h>
#include <com_sun_glass_events_ViewEvent.h>
#include <com_sun_glass_events_MouseEvent.h>
#include <com_sun_glass_events_KeyEvent.h>

#include <com_sun_glass_ui_Window_Level.h>

#include <X11/extensions/shape.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#ifdef GLASS_GTK3
#include <gtk/gtkx.h>
#endif

#include <string.h>

#include <algorithm>

#define MOUSE_BACK_BTN 8
#define MOUSE_FORWARD_BTN 9

static gboolean ctx_configure_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_configure();
    return FALSE;
}

static gboolean ctx_property_notify_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_property_notify(&event->property);
    return TRUE;
}

static gboolean ctx_focus_change_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_focus(&event->focus_change);
    return TRUE;
}

static gboolean ctx_delete_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_delete();
    return TRUE;
}

static gboolean ctx_window_state_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_state(&event->window_state);
    return FALSE;
}

static gboolean ctx_device_button_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_mouse_button(&event->button);
    return TRUE;
}

static gboolean ctx_device_motion_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_mouse_motion(&event->motion);
    gdk_event_request_motions(&event->motion);
    return TRUE;
}

static gboolean ctx_device_scroll_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_mouse_scroll(&event->scroll);
    return TRUE;
}

static gboolean ctx_enter_or_leave_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_mouse_cross(&event->crossing);
    return TRUE;
}

static gboolean ctx_key_press_or_release_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_key(&event->key);
    return TRUE;
}

static gboolean ctx_map_callback(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    ((WindowContextBase*)user_data)->process_map();
    return TRUE;
}

static void ctx_screen_changed_callback(GtkWidget *widget,
                              GdkScreen *previous_screen,
                              gpointer   user_data) {
    ((WindowContextBase*)user_data)->process_screen_changed();
}

static void connect_signals(GtkWidget* gtk_widget, WindowContextBase* ctx) {
    g_signal_connect(gtk_widget, "configure-event", G_CALLBACK(ctx_configure_callback), ctx);
    g_signal_connect(gtk_widget, "property-notify-event", G_CALLBACK(ctx_property_notify_callback), ctx);
    g_signal_connect(gtk_widget, "focus-in-event", G_CALLBACK(ctx_focus_change_callback), ctx);
    g_signal_connect(gtk_widget, "focus-out-event", G_CALLBACK(ctx_focus_change_callback), ctx);
    g_signal_connect(gtk_widget, "delete-event", G_CALLBACK(ctx_delete_callback), ctx);
    g_signal_connect(gtk_widget, "window-state-event", G_CALLBACK(ctx_window_state_callback), ctx);
    g_signal_connect(gtk_widget, "button-press-event", G_CALLBACK(ctx_device_button_callback), ctx);
    g_signal_connect(gtk_widget, "button-release-event", G_CALLBACK(ctx_device_button_callback), ctx);
    g_signal_connect(gtk_widget, "motion-notify-event", G_CALLBACK(ctx_device_motion_callback), ctx);
    g_signal_connect(gtk_widget, "scroll-event", G_CALLBACK(ctx_device_scroll_callback), ctx);
    g_signal_connect(gtk_widget, "enter-notify-event", G_CALLBACK(ctx_enter_or_leave_callback), ctx);
    g_signal_connect(gtk_widget, "leave-notify-event", G_CALLBACK(ctx_enter_or_leave_callback), ctx);
    g_signal_connect(gtk_widget, "key-press-event", G_CALLBACK(ctx_key_press_or_release_callback), ctx);
    g_signal_connect(gtk_widget, "key-release-event", G_CALLBACK(ctx_key_press_or_release_callback), ctx);
    g_signal_connect(gtk_widget, "map-event", G_CALLBACK(ctx_map_callback), ctx);
    g_signal_connect(gtk_widget, "screen-changed", G_CALLBACK(ctx_screen_changed_callback), ctx);
}

GdkWindow* WindowContextBase::get_gdk_window() {
    return gdk_window;
}

GtkWidget* WindowContextBase::get_gtk_widget() {
    return gtk_widget;
}

jobject WindowContextBase::get_jview() {
    return jview;
}

jobject WindowContextBase::get_jwindow() {
    return jwindow;
}

bool WindowContextBase::isEnabled() {
    if (jwindow) {
        bool result = (JNI_TRUE == mainEnv->CallBooleanMethod(jwindow, jWindowIsEnabled));
        LOG_EXCEPTION(mainEnv)
        return result;
    } else {
        return false;
    }
}

void WindowContextBase::notify_state(jint glass_state) {
    if (glass_state == com_sun_glass_events_WindowEvent_RESTORE) {
        if (is_maximized) {
            glass_state = com_sun_glass_events_WindowEvent_MAXIMIZE;
        }

        notify_repaint();
    }

    if (jwindow) {
       mainEnv->CallVoidMethod(jwindow,
               jGtkWindowNotifyStateChanged,
               glass_state);
       CHECK_JNI_EXCEPTION(mainEnv);
    }
}

void WindowContextBase::notify_repaint() {
    int w, h;
    glass_gdk_window_get_size(gdk_window, &w, &h);
    if (jview) {
        mainEnv->CallVoidMethod(jview,
                jViewNotifyRepaint,
                0, 0, w, h);
        CHECK_JNI_EXCEPTION(mainEnv);
    }
}

void WindowContextBase::process_state(GdkEventWindowState* event) {
    if (event->changed_mask &
            (GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_MAXIMIZED)) {

        if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
            is_iconified = event->new_window_state & GDK_WINDOW_STATE_ICONIFIED;
        }
        if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) {
            is_maximized = event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED;
        }

        jint stateChangeEvent;

        if (is_iconified) {
            stateChangeEvent = com_sun_glass_events_WindowEvent_MINIMIZE;
        } else if (is_maximized) {
            stateChangeEvent = com_sun_glass_events_WindowEvent_MAXIMIZE;
        } else {
            stateChangeEvent = com_sun_glass_events_WindowEvent_RESTORE;
            if ((gdk_windowManagerFunctions & GDK_FUNC_MINIMIZE) == 0) {
                // in this case - the window manager will not support the programatic
                // request to iconify - so we need to restore it now.
                gdk_window_set_functions(gdk_window, gdk_windowManagerFunctions);
            }
        }

        notify_state(stateChangeEvent);
    } else if (event->changed_mask & GDK_WINDOW_STATE_ABOVE) {
        notify_on_top( event->new_window_state & GDK_WINDOW_STATE_ABOVE);
    }
}

void WindowContextBase::process_focus(GdkEventFocus* event) {
    if (!event->in) {
        ungrab_focus();
    }

    if (xim.enabled && xim.ic) {
        if (event->in) {
            XSetICFocus(xim.ic);
        } else {
            XUnsetICFocus(xim.ic);
        }
    }

    if (jwindow) {
        if (!event->in || isEnabled()) {
            mainEnv->CallVoidMethod(jwindow, jWindowNotifyFocus,
                    event->in ? com_sun_glass_events_WindowEvent_FOCUS_GAINED : com_sun_glass_events_WindowEvent_FOCUS_LOST);
            CHECK_JNI_EXCEPTION(mainEnv)
        } else {
            mainEnv->CallVoidMethod(jwindow, jWindowNotifyFocusDisabled);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }
}

void WindowContextBase::increment_events_counter() {
    ++events_processing_cnt;
}

void WindowContextBase::decrement_events_counter() {
    --events_processing_cnt;
}

size_t WindowContextBase::get_events_count() {
    return events_processing_cnt;
}

bool WindowContextBase::is_dead() {
    return can_be_deleted;
}

void destroy_and_delete_ctx(WindowContext* ctx) {
    if (ctx) {
        ctx->process_destroy();

        if (!ctx->get_events_count()) {
            delete ctx;
        }
        // else: ctx will be deleted in EventsCounterHelper after completing
        // an event processing
    }
}

void WindowContextBase::process_destroy() {
    ungrab_focus();

    std::set<WindowContextTop*>::iterator it;
    for (it = children.begin(); it != children.end(); ++it) {
        // FIX JDK-8226537: this method calls set_owner(NULL) which prevents
        // WindowContextTop::process_destroy() to call remove_child() (because children
        // is being iterated here) but also prevents gtk_window_set_transient_for from
        // being called - this causes the crash on gnome.
        gtk_window_set_transient_for((*it)->get_gtk_window(), NULL);
        (*it)->set_owner(NULL);
        destroy_and_delete_ctx(*it);
    }
    children.clear();

    if (jwindow) {
        mainEnv->CallVoidMethod(jwindow, jWindowNotifyDestroy);
        EXCEPTION_OCCURED(mainEnv);
    }

    if (jview) {
        mainEnv->DeleteGlobalRef(jview);
        jview = NULL;
    }

    if (jwindow) {
        mainEnv->DeleteGlobalRef(jwindow);
        jwindow = NULL;
    }

    can_be_deleted = true;
}

void WindowContextBase::process_delete() {
    if (jwindow && isEnabled()) {
        gtk_widget_hide_on_delete(gtk_widget);
        mainEnv->CallVoidMethod(jwindow, jWindowNotifyClose);
        CHECK_JNI_EXCEPTION(mainEnv)
    }
}

void WindowContextBase::process_expose(GdkEventExpose* event) {
   if (jview && is_visible()) {
        mainEnv->CallVoidMethod(jview, jViewNotifyRepaint, event->area.x, event->area.y,
                                 event->area.width, event->area.height);
        CHECK_JNI_EXCEPTION(mainEnv)
    }
}

static inline jint gtk_button_number_to_mouse_button(guint button) {
    switch (button) {
        case 1:
            return com_sun_glass_events_MouseEvent_BUTTON_LEFT;
        case 2:
            return com_sun_glass_events_MouseEvent_BUTTON_OTHER;
        case 3:
            return com_sun_glass_events_MouseEvent_BUTTON_RIGHT;
        case MOUSE_BACK_BTN:
            return com_sun_glass_events_MouseEvent_BUTTON_BACK;
        case MOUSE_FORWARD_BTN:
            return com_sun_glass_events_MouseEvent_BUTTON_FORWARD;
        default:
            // Other buttons are not supported by quantum and are not reported by other platforms
            return com_sun_glass_events_MouseEvent_BUTTON_NONE;
    }
}

void WindowContextBase::process_mouse_button(GdkEventButton* event) {
    bool press = event->type == GDK_BUTTON_PRESS;
    guint state = event->state;
    guint mask = 0;

    // We need to add/remove current mouse button from the modifier flags
    // as X lib state represents the state just prior to the event and
    // glass needs the state just after the event
    switch (event->button) {
        case 1:
            mask = GDK_BUTTON1_MASK;
            break;
        case 2:
            mask = GDK_BUTTON2_MASK;
            break;
        case 3:
            mask = GDK_BUTTON3_MASK;
            break;
        case MOUSE_BACK_BTN:
            mask = GDK_BUTTON4_MASK;
            break;
        case MOUSE_FORWARD_BTN:
            mask = GDK_BUTTON5_MASK;
            break;
    }

    if (press) {
        state |= mask;
    } else {
        state &= ~mask;
    }

    jint button = gtk_button_number_to_mouse_button(event->button);

    if (jview && button != com_sun_glass_events_MouseEvent_BUTTON_NONE) {
        mainEnv->CallVoidMethod(jview, jViewNotifyMouse,
                press ? com_sun_glass_events_MouseEvent_DOWN : com_sun_glass_events_MouseEvent_UP,
                button,
                (jint) event->x, (jint) event->y,
                (jint) event->x_root, (jint) event->y_root,
                gdk_modifier_mask_to_glass(state),
                (event->button == 3 && press) ? JNI_TRUE : JNI_FALSE,
                JNI_FALSE);
        CHECK_JNI_EXCEPTION(mainEnv)

        if (jview && event->button == 3 && press) {
            mainEnv->CallVoidMethod(jview, jViewNotifyMenu,
                    (jint)event->x, (jint)event->y,
                    (jint)event->x_root, (jint)event->y_root,
                    JNI_FALSE);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }
}

void WindowContextBase::process_mouse_motion(GdkEventMotion* event) {
    jint glass_modifier = gdk_modifier_mask_to_glass(event->state);
    jint isDrag = glass_modifier & (
            com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_PRIMARY |
            com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_MIDDLE |
            com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_SECONDARY |
            com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_BACK |
            com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_FORWARD);
    jint button = com_sun_glass_events_MouseEvent_BUTTON_NONE;

    if (glass_modifier & com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_PRIMARY) {
        button = com_sun_glass_events_MouseEvent_BUTTON_LEFT;
    } else if (glass_modifier & com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_MIDDLE) {
        button = com_sun_glass_events_MouseEvent_BUTTON_OTHER;
    } else if (glass_modifier & com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_SECONDARY) {
        button = com_sun_glass_events_MouseEvent_BUTTON_RIGHT;
    } else if (glass_modifier & com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_BACK) {
        button = com_sun_glass_events_MouseEvent_BUTTON_BACK;
    } else if (glass_modifier & com_sun_glass_events_KeyEvent_MODIFIER_BUTTON_FORWARD) {
        button = com_sun_glass_events_MouseEvent_BUTTON_FORWARD;
    }

    if (jview) {
        mainEnv->CallVoidMethod(jview, jViewNotifyMouse,
                isDrag ? com_sun_glass_events_MouseEvent_DRAG : com_sun_glass_events_MouseEvent_MOVE,
                button,
                (jint) event->x, (jint) event->y,
                (jint) event->x_root, (jint) event->y_root,
                glass_modifier,
                JNI_FALSE,
                JNI_FALSE);
        CHECK_JNI_EXCEPTION(mainEnv)
    }
}

void WindowContextBase::process_mouse_scroll(GdkEventScroll* event) {
    jdouble dx = 0;
    jdouble dy = 0;

    // converting direction to change in pixels
    switch (event->direction) {
#if GTK_CHECK_VERSION(3, 4, 0)
        case GDK_SCROLL_SMOOTH:
            //FIXME 3.4 ???
            break;
#endif
        case GDK_SCROLL_UP:
            dy = 1;
            break;
        case GDK_SCROLL_DOWN:
            dy = -1;
            break;
        case GDK_SCROLL_LEFT:
            dx = 1;
            break;
        case GDK_SCROLL_RIGHT:
            dx = -1;
            break;
    }
    if (event->state & GDK_SHIFT_MASK) {
        jdouble t = dy;
        dy = dx;
        dx = t;
    }
    if (jview) {
        mainEnv->CallVoidMethod(jview, jViewNotifyScroll,
                (jint) event->x, (jint) event->y,
                (jint) event->x_root, (jint) event->y_root,
                dx, dy,
                gdk_modifier_mask_to_glass(event->state),
                (jint) 0, (jint) 0,
                (jint) 0, (jint) 0,
                (jdouble) 40.0, (jdouble) 40.0);
        CHECK_JNI_EXCEPTION(mainEnv)
    }
}

void WindowContextBase::process_mouse_cross(GdkEventCrossing* event) {
    bool enter = event->type == GDK_ENTER_NOTIFY;

    if (jview) {
        guint state = event->state;
        if (enter) { // workaround for RT-21590
            state &= ~MOUSE_BUTTONS_MASK;
        }

        if (enter != is_mouse_entered) {
            is_mouse_entered = enter;
            mainEnv->CallVoidMethod(jview, jViewNotifyMouse,
                    enter ? com_sun_glass_events_MouseEvent_ENTER : com_sun_glass_events_MouseEvent_EXIT,
                    com_sun_glass_events_MouseEvent_BUTTON_NONE,
                    (jint) event->x, (jint) event->y,
                    (jint) event->x_root, (jint) event->y_root,
                    gdk_modifier_mask_to_glass(state),
                    JNI_FALSE,
                    JNI_FALSE);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }
}

void WindowContextBase::process_key(GdkEventKey* event) {
    bool press = event->type == GDK_KEY_PRESS;
    jint glassKey = get_glass_key(event);
    jint glassModifier = gdk_modifier_mask_to_glass(event->state);
    if (press) {
        glassModifier |= glass_key_to_modifier(glassKey);
    } else {
        glassModifier &= ~glass_key_to_modifier(glassKey);
    }
    jcharArray jChars = NULL;
    jchar key = gdk_keyval_to_unicode(event->keyval);
    if (key >= 'a' && key <= 'z' && (event->state & GDK_CONTROL_MASK)) {
        key = key - 'a' + 1; // map 'a' to ctrl-a, and so on.
    } else {
#ifdef GLASS_GTK2
        if (key == 0) {
            // Work around "bug" fixed in gtk-3.0:
            // http://mail.gnome.org/archives/commits-list/2011-March/msg06832.html
            switch (event->keyval) {
            case 0xFF08 /* Backspace */: key =  '\b';
            case 0xFF09 /* Tab       */: key =  '\t';
            case 0xFF0A /* Linefeed  */: key =  '\n';
            case 0xFF0B /* Vert. Tab */: key =  '\v';
            case 0xFF0D /* Return    */: key =  '\r';
            case 0xFF1B /* Escape    */: key =  '\033';
            case 0xFFFF /* Delete    */: key =  '\177';
            }
        }
#endif
    }

    if (key > 0) {
        jChars = mainEnv->NewCharArray(1);
        if (jChars) {
            mainEnv->SetCharArrayRegion(jChars, 0, 1, &key);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    } else {
        jChars = mainEnv->NewCharArray(0);
    }
    if (jview) {
        if (press) {
            mainEnv->CallVoidMethod(jview, jViewNotifyKey,
                    com_sun_glass_events_KeyEvent_PRESS,
                    glassKey,
                    jChars,
                    glassModifier);
            CHECK_JNI_EXCEPTION(mainEnv)

            if (jview && key > 0) { // TYPED events should only be sent for printable characters.
                mainEnv->CallVoidMethod(jview, jViewNotifyKey,
                        com_sun_glass_events_KeyEvent_TYPED,
                        com_sun_glass_events_KeyEvent_VK_UNDEFINED,
                        jChars,
                        glassModifier);
                CHECK_JNI_EXCEPTION(mainEnv)
            }
        } else {
            mainEnv->CallVoidMethod(jview, jViewNotifyKey,
                    com_sun_glass_events_KeyEvent_RELEASE,
                    glassKey,
                    jChars,
                    glassModifier);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }
}

void WindowContextBase::paint(void* data, jint width, jint height) {
#if GTK_CHECK_VERSION(3, 0, 0)
    cairo_region_t *region = gdk_window_get_clip_region(gdk_window);
#if GTK_CHECK_VERSION(3, 22, 0)
    GdkDrawingContext *dcontext = gdk_window_begin_draw_frame(gdk_window, region);
    cairo_t *context = gdk_drawing_context_get_cairo_context(dcontext);
#else
    gdk_window_begin_paint_region(gdk_window, region);
    cairo_t* context = gdk_cairo_create(gdk_window);
#endif
#else
    cairo_t* context = gdk_cairo_create(gdk_window);
#endif
    cairo_surface_t* cairo_surface;
    cairo_surface = cairo_image_surface_create_for_data(
            (unsigned char*)data,
            CAIRO_FORMAT_ARGB32,
            width, height, width * 4);

    applyShapeMask(data, width, height);
#ifdef GLASS_GTK3
    if (bg_color.is_set) {
        cairo_set_source_rgb(context, bg_color.red, bg_color.green, bg_color.blue);
    }
#endif
    cairo_set_source_surface(context, cairo_surface, 0, 0);
    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_paint(context);

#if GTK_CHECK_VERSION(3, 0, 0)
#if GTK_CHECK_VERSION(3, 22, 0)
    gdk_window_end_draw_frame(gdk_window, dcontext);
    cairo_region_destroy(region);
#else
    gdk_window_end_paint(gdk_window);
    cairo_region_destroy(region);
    cairo_destroy(context);
#endif
#else
    cairo_destroy(context);
#endif

    cairo_surface_destroy(cairo_surface);
}

void WindowContextBase::add_child(WindowContextTop* child) {
    children.insert(child);
    gtk_window_set_transient_for(child->get_gtk_window(), this->get_gtk_window());

    if (win_group != NULL) {
        gtk_window_group_add_window(win_group, GTK_WINDOW(child->get_gtk_widget()));
    }
}

void WindowContextBase::remove_child(WindowContextTop* child) {
    children.erase(child);
    gtk_window_set_transient_for(child->get_gtk_window(), NULL);

    if (win_group != NULL) {
        gtk_window_group_remove_window(win_group, GTK_WINDOW(child->get_gtk_widget()));
    }
}

void WindowContextBase::show_or_hide_children(bool show) {
    std::set<WindowContextTop*>::iterator it;
    for (it = children.begin(); it != children.end(); ++it) {
        (*it)->set_minimized(!show);
        (*it)->show_or_hide_children(show);
    }
}

void WindowContextBase::set_visible(bool visible) {
    if (visible) {
        gtk_widget_show_all(gtk_widget);
    } else {
        gtk_widget_hide(gtk_widget);
        if (jview && is_mouse_entered) {
            is_mouse_entered = false;
            mainEnv->CallVoidMethod(jview, jViewNotifyMouse,
                    com_sun_glass_events_MouseEvent_EXIT,
                    com_sun_glass_events_MouseEvent_BUTTON_NONE,
                    0, 0,
                    0, 0,
                    0,
                    JNI_FALSE,
                    JNI_FALSE);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }
}

bool WindowContextBase::is_visible() {
    return gtk_widget_get_visible(gtk_widget);
}

bool WindowContextBase::set_view(jobject view) {

    if (jview) {
        mainEnv->CallVoidMethod(jview, jViewNotifyMouse,
                com_sun_glass_events_MouseEvent_EXIT,
                com_sun_glass_events_MouseEvent_BUTTON_NONE,
                0, 0,
                0, 0,
                0,
                JNI_FALSE,
                JNI_FALSE);
        mainEnv->DeleteGlobalRef(jview);
    }

    if (view) {
        jview = mainEnv->NewGlobalRef(view);
    } else {
        jview = NULL;
    }
    return TRUE;
}

bool WindowContextBase::grab_focus() {
    if (win_group == NULL) {
        return FALSE;
    }

    if (!gtk_widget_has_grab(gtk_widget)) {
        GtkWidget* current_grab = gtk_grab_get_current();

        if (current_grab != NULL) {
            gtk_grab_remove(current_grab);
        }

        gtk_grab_add(gtk_widget);
    }

    return TRUE;
}

void WindowContextBase::ungrab_focus() {
    if (win_group == NULL) {
        return;
    }

    if (gtk_widget_has_grab(gtk_widget)) {
        gtk_grab_remove(gtk_widget);

        if (jwindow) {
            mainEnv->CallVoidMethod(jwindow, jWindowNotifyFocusUngrab);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }
}

void WindowContextBase::set_cursor(GdkCursor* cursor) {
    gdk_window_set_cursor(gdk_window, cursor);
}

void WindowContextBase::set_background(float r, float g, float b) {
    bg_color.red = r;
    bg_color.green = g;
    bg_color.blue = b;
    bg_color.is_set = TRUE;
    notify_repaint();
}

WindowContextBase::~WindowContextBase() {
    if (xim.ic) {
        XDestroyIC(xim.ic);
        xim.ic = NULL;
    }
    if (xim.im) {
        XCloseIM(xim.im);
        xim.im = NULL;
    }

    gtk_widget_destroy(gtk_widget);
}

////////////////////////////// WindowContextTop /////////////////////////////////

static GdkAtom atom_net_wm_state = gdk_atom_intern_static_string("_NET_WM_STATE");
static GdkAtom atom_net_wm_frame_extents = gdk_atom_intern_static_string("_NET_FRAME_EXTENTS");

WindowContextTop::WindowContextTop(jobject _jwindow, WindowContext* _owner, long _screen,
        WindowFrameType _frame_type, WindowType type, GdkWMFunction wmf) :
            WindowContextBase(),
            screen(_screen),
            frame_type(_frame_type),
            window_type(type),
            owner(_owner),
            geometry(),
            map_received(false),
            visible_received(false),
            on_top(false),
            is_fullscreen(false)
{
    jwindow = mainEnv->NewGlobalRef(_jwindow);

    gtk_widget = gtk_window_new(type == POPUP ? GTK_WINDOW_POPUP : GTK_WINDOW_TOPLEVEL);

    if (type != POPUP && owner == NULL) {
        win_group = gtk_window_group_new();
    }

    if (gchar* app_name = get_application_name()) {
        gtk_window_set_wmclass(GTK_WINDOW(gtk_widget), app_name, app_name);
        g_free(app_name);
    }

    if (owner) {
        owner->add_child(this);
        if (on_top_inherited()) {
            gtk_window_set_keep_above(GTK_WINDOW(gtk_widget), TRUE);
        }
    }

    if (type == UTILITY) {
        gtk_window_set_type_hint(GTK_WINDOW(gtk_widget), GDK_WINDOW_TYPE_HINT_UTILITY);
    }

    glong xvisualID = (glong)mainEnv->GetStaticLongField(jApplicationCls, jApplicationVisualID);

    if (xvisualID != 0) {
        GdkVisual *visual = gdk_x11_screen_lookup_visual(gdk_screen_get_default(), xvisualID);
        glass_gtk_window_configure_from_visual(gtk_widget, visual);
    }

    gtk_widget_set_events(gtk_widget, GDK_ALL_EVENTS_MASK);
    gtk_widget_set_app_paintable(gtk_widget, TRUE);
    if (frame_type != TITLED) {
        gtk_window_set_decorated(GTK_WINDOW(gtk_widget), FALSE);
    }

    glass_gtk_configure_transparency_and_realize(gtk_widget, frame_type == TRANSPARENT);
    gtk_window_set_title(GTK_WINDOW(gtk_widget), "");

    gdk_window = gtk_widget_get_window(gtk_widget);

    g_object_set_data_full(G_OBJECT(gdk_window), GDK_WINDOW_DATA_CONTEXT, this, NULL);

    glass_dnd_attach_context(this);

    gdk_windowManagerFunctions = wmf;
    if (wmf) {
        gdk_window_set_functions(gdk_window, wmf);
    }

    connect_signals(gtk_widget, this);
}

// Applied to a temporary full screen window to prevent sending events to Java
void WindowContextTop::detach_from_java() {
    if (jview) {
        mainEnv->DeleteGlobalRef(jview);
        jview = NULL;
    }
    if (jwindow) {
        mainEnv->DeleteGlobalRef(jwindow);
        jwindow = NULL;
    }
}

// This function calculate the deltas between window and window + decoration (titleblar, borders).
// Only useful if the window manager does not support the frame extents extension - in this case
// it uses GDK calculation, that is primary based on _NET_FRAME_EXTENTS but will try harder
// if that's not available.
void WindowContextTop::calculate_adjustments() {
    if (frame_type != TITLED || geometry.frame_extents_received) {
        return;
    }

    gint x, y;
    gdk_window_get_origin(gdk_window, &x, &y);

    gint rx, ry;
    gdk_window_get_root_origin(gdk_window, &rx, &ry);

    if (rx != x || ry != y) {
        // the left extends are correct - the right one is guessed to be the same
        geometry.adjust_w = (rx - x) * 2;
        // guess that bottom size is the same as left and right
        geometry.adjust_h = (ry - y) + (rx - x);

        // those will be correct
        geometry.view_x = (rx - x);
        geometry.view_y = (ry - y);

        save_cached_extents();
    } else {
        CachedExtents c = (window_type == NORMAL) ? normal_extents : utility_extents;
        geometry.adjust_w = c.adjust_w;
        geometry.adjust_h = c.adjust_h;
        geometry.view_x = c.view_x;
        geometry.view_y = c.view_y;
    }

    apply_geometry();
}

void WindowContextTop::save_cached_extents() {
    if (frame_type != TITLED) {
        return;
    }

    if (window_type == NORMAL) {
        normal_extents.adjust_w = geometry.adjust_w;
        normal_extents.adjust_h = geometry.adjust_h;
        normal_extents.view_x = geometry.view_x;
        normal_extents.view_y = geometry.view_y;
    } else {
        utility_extents.adjust_w = geometry.adjust_w;
        utility_extents.adjust_h = geometry.adjust_h;
        utility_extents.view_x = geometry.view_x;
        utility_extents.view_y = geometry.view_y;
    }
}

void WindowContextTop::apply_geometry() {
    if (!map_received) {
        return;
    }

    GdkGeometry gdk_geometry;
    gdk_geometry.win_gravity = GDK_GRAVITY_NORTH_WEST;

    if ((!geometry.resizable || !geometry.enabled) && !(is_maximized || is_fullscreen)) {
        // not resizeable
        int w = (geometry.current_w - geometry.adjust_w) > 0
                    ? geometry.current_w - geometry.adjust_w
                    : geometry.current_cw;

        int h = (geometry.current_h - geometry.adjust_h) > 0
                    ? geometry.current_h - geometry.adjust_h
                    : geometry.current_ch;

        gdk_geometry.min_width = gdk_geometry.max_width = w;
        gdk_geometry.min_height = gdk_geometry.max_height = h;
    } else {
        gdk_geometry.min_width = (geometry.minw - geometry.adjust_w) > 0
                                    ? geometry.minw - geometry.adjust_w : 1;
        gdk_geometry.min_height = (geometry.minh -  geometry.adjust_h) > 0
                                    ? geometry.minh - geometry.adjust_h : 1;

        gdk_geometry.max_width = (geometry.maxw - geometry.adjust_w > 0)
                                    ? geometry.maxw - geometry.adjust_w : G_MAXINT;
        gdk_geometry.max_height = (geometry.maxh - geometry.adjust_h> 0)
                                    ? geometry.maxh - geometry.adjust_h : G_MAXINT;
    }

    gtk_window_set_geometry_hints(GTK_WINDOW(gtk_widget), NULL, &gdk_geometry,
        (GdkWindowHints) (GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE | GDK_HINT_WIN_GRAVITY));
}

void WindowContextTop::size_position_notify(bool size_changed, bool pos_changed) {
    if (jview) {
        if (size_changed) {
            mainEnv->CallVoidMethod(jview, jViewNotifyResize, geometry.current_cw, geometry.current_ch);
            CHECK_JNI_EXCEPTION(mainEnv);
        }

        if (pos_changed) {
            mainEnv->CallVoidMethod(jview, jViewNotifyView, com_sun_glass_events_ViewEvent_MOVE);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }

    if (jwindow) {
        if (size_changed || is_maximized) {
            mainEnv->CallVoidMethod(jwindow, jWindowNotifyResize,
                    (is_maximized)
                        ? com_sun_glass_events_WindowEvent_MAXIMIZE
                        : com_sun_glass_events_WindowEvent_RESIZE,
                    geometry.current_w, geometry.current_h);
            CHECK_JNI_EXCEPTION(mainEnv)
        }

        if (pos_changed) {
            mainEnv->CallVoidMethod(jwindow, jWindowNotifyMove, geometry.current_x, geometry.current_y);
            CHECK_JNI_EXCEPTION(mainEnv)
        }
    }
}

void WindowContextTop::activate_window() {
    Display *display = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (gdk_window));
    Atom navAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    if (navAtom != None) {
        XClientMessageEvent clientMessage;
        memset(&clientMessage, 0, sizeof(clientMessage));

        clientMessage.type = ClientMessage;
        clientMessage.window = GDK_WINDOW_XID(gdk_window);
        clientMessage.message_type = navAtom;
        clientMessage.format = 32;
        clientMessage.data.l[0] = 1;
        clientMessage.data.l[1] = gdk_x11_get_server_time(gdk_window);
        clientMessage.data.l[2] = 0;

        XSendEvent(display, XDefaultRootWindow(display), False,
                   SubstructureRedirectMask | SubstructureNotifyMask,
                   (XEvent *) &clientMessage);
        XFlush(display);
    }
}

bool WindowContextTop::get_frame_extents_property(int *top, int *left,
        int *bottom, int *right) {
    unsigned long *extents;

    if (gdk_property_get(gdk_window,
            atom_net_wm_frame_extents,
            gdk_atom_intern("CARDINAL", FALSE),
            0,
            sizeof (unsigned long) * 4,
            FALSE,
            NULL,
            NULL,
            NULL,
            (guchar**) & extents)) {
        *left = extents [0];
        *right = extents [1];
        *top = extents [2];
        *bottom = extents [3];

        g_free(extents);
        return true;
    }

    return false;
}

void WindowContextTop::process_net_wm_property() {
    // Workaround for https://bugs.launchpad.net/unity/+bug/998073

    static GdkAtom atom_atom = gdk_atom_intern_static_string("ATOM");
    static GdkAtom atom_net_wm_state = gdk_atom_intern_static_string("_NET_WM_STATE");
    static GdkAtom atom_net_wm_state_hidden = gdk_atom_intern_static_string("_NET_WM_STATE_HIDDEN");
    static GdkAtom atom_net_wm_state_above = gdk_atom_intern_static_string("_NET_WM_STATE_ABOVE");

    gint length;
    glong* atoms = NULL;

    if (gdk_property_get(gdk_window, atom_net_wm_state, atom_atom,
            0, G_MAXLONG, FALSE, NULL, NULL, &length, (guchar**) &atoms)) {

        bool is_hidden = false;
        bool is_above = false;
        for (gint i = 0; i < (gint)(length / sizeof(glong)); i++) {
            if (atom_net_wm_state_hidden == (GdkAtom)atoms[i]) {
                is_hidden = true;
            } else if (atom_net_wm_state_above == (GdkAtom)atoms[i]) {
                is_above = true;
            }
        }

        g_free(atoms);

        if (is_iconified != is_hidden) {
            is_iconified = is_hidden;

            notify_state((is_hidden)
                    ? com_sun_glass_events_WindowEvent_MINIMIZE
                    : com_sun_glass_events_WindowEvent_RESTORE);
        }

        notify_on_top(is_above);
    }
}

void WindowContextTop::process_property_notify(GdkEventProperty* event) {
    if (event->window == gdk_window) {
        if (event->atom == atom_net_wm_state) {
            process_net_wm_property();
        } else if (event->atom == atom_net_wm_frame_extents) {
            if (frame_type != TITLED) {
                return;
            }

            int top, left, bottom, right;

            if (get_frame_extents_property(&top, &left, &bottom, &right)) {
                if (top + left + bottom + right > 0) {
                    geometry.frame_extents_received = true;
                    geometry.adjust_w = left + right;
                    geometry.adjust_h = top + bottom;
                    geometry.view_x = left;
                    geometry.view_y = top;

                    save_cached_extents();

                    // set bounds again to set to correct window size that must
                    // be the total width and height accounting extents
                    set_bounds(0, 0, false, false, -1, -1, -1, -1);
                }
            }
        }
    }
}

void WindowContextTop::process_configure() {
    calculate_adjustments();

    gint x, y, w, h, gtk_w, gtk_h;

    gtk_window_get_position(GTK_WINDOW(gtk_widget), &x, &y);
    gtk_window_get_size(GTK_WINDOW(gtk_widget), &gtk_w, &gtk_h);
    w = gtk_w + geometry.adjust_w;
    h = gtk_h + geometry.adjust_h;

    gboolean pos_changed = geometry.current_x != x || geometry.current_y != y;
    gboolean size_changed = geometry.current_w != w || geometry.current_h != h
                            || geometry.current_cw != gtk_w || geometry.current_ch != gtk_h;

    geometry.current_x = x;
    geometry.current_y = y;
    geometry.current_w = w;
    geometry.current_h = h;
    geometry.current_cw = gtk_w;
    geometry.current_ch = gtk_h;

    size_position_notify(size_changed, pos_changed);
}

void WindowContextTop::process_screen_changed() {
    glong to_screen = getScreenPtrForLocation(geometry.current_x, geometry.current_y);
    if (to_screen != -1) {
        if (to_screen != screen) {
            if (jwindow) {
                //notify screen changed
                jobject jScreen = createJavaScreen(mainEnv, to_screen);
                mainEnv->CallVoidMethod(jwindow, jWindowNotifyMoveToAnotherScreen, jScreen);
                CHECK_JNI_EXCEPTION(mainEnv)
            }
            screen = to_screen;
        }
    }
}

void WindowContextTop::set_resizable(bool res) {
    if (res != geometry.resizable) {
        geometry.resizable = res;
        apply_geometry();
    }
}

void WindowContextTop::set_visible(bool visible) {
    WindowContextBase::set_visible(visible);

    if (visible) {
        visible_received = TRUE;
    }

    //JDK-8220272 - fire event first because GDK_FOCUS_CHANGE is not always in order
    if (visible && jwindow && isEnabled()) {
        mainEnv->CallVoidMethod(jwindow, jWindowNotifyFocus, com_sun_glass_events_WindowEvent_FOCUS_GAINED);
        CHECK_JNI_EXCEPTION(mainEnv);
    }
}

void WindowContextTop::set_bounds(int x, int y, bool xSet, bool ySet, int w, int h, int cw, int ch) {
    calculate_adjustments();

    // newW / newH always content sizes compatible with GTK+
    // if window has no decoration, adjustments will be ZERO
    int newW = w > 0 ? w - geometry.adjust_w : cw;
    int newH = h > 0 ? h - geometry.adjust_h : ch;

    gboolean size_changed = FALSE;
    gboolean pos_changed = FALSE;

    if (newW > 0 && newH > 0) {
        size_changed = TRUE;

        geometry.current_cw = newW;
        geometry.current_ch = newH;
        geometry.current_w = newW + geometry.adjust_w;
        geometry.current_h = newH + geometry.adjust_h;

        if (visible_received) {
            // update constraints if not resized by the user interface so it will
            // let gtk_window_resize succeed, because it's bound to geometry constraints
            apply_geometry();
            gtk_window_resize(GTK_WINDOW(gtk_widget), newW, newH);
        } else {
            gtk_window_set_default_size(GTK_WINDOW(gtk_widget), newW, newH);
        }
    }

    if (xSet || ySet) {
        int newX = (xSet) ? x : geometry.current_x;
        int newY = (ySet) ? y : geometry.current_y;

        if (newX != geometry.current_x || newY != geometry.current_y) {
            pos_changed = TRUE;
            geometry.current_x = newX;
            geometry.current_y = newY;
            gtk_window_move(GTK_WINDOW(gtk_widget), newX, newY);
        }
    }

    size_position_notify(size_changed, pos_changed);
}

void WindowContextTop::process_map() {
    map_received = true;
    apply_geometry();
}

void WindowContextTop::applyShapeMask(void* data, uint width, uint height) {
    if (frame_type != TRANSPARENT) {
        return;
    }

    glass_window_apply_shape_mask(gtk_widget_get_window(gtk_widget), data, width, height);
}

void WindowContextTop::set_minimized(bool minimize) {
    is_iconified = minimize;
    if (minimize) {
        if (frame_type == TRANSPARENT) {
            // https://bugs.launchpad.net/ubuntu/+source/unity/+bug/1245571
            glass_window_reset_input_shape_mask(gtk_widget_get_window(gtk_widget));
        }

        if ((gdk_windowManagerFunctions & GDK_FUNC_MINIMIZE) == 0) {
            // in this case - the window manager will not support the programatic
            // request to iconify - so we need to disable this until we are restored.
            GdkWMFunction wmf = (GdkWMFunction)(gdk_windowManagerFunctions | GDK_FUNC_MINIMIZE);
            gdk_window_set_functions(gdk_window, wmf);
        }
        gtk_window_iconify(GTK_WINDOW(gtk_widget));
    } else {
        gtk_window_deiconify(GTK_WINDOW(gtk_widget));
        activate_window();
    }
}

void WindowContextTop::set_maximized(bool maximize) {
    is_maximized = maximize;
    if (maximize) {
        gtk_window_maximize(GTK_WINDOW(gtk_widget));
    } else {
        gtk_window_unmaximize(GTK_WINDOW(gtk_widget));
    }
}

void WindowContextTop::enter_fullscreen() {
    is_fullscreen = TRUE;
    gtk_window_fullscreen(GTK_WINDOW(gtk_widget));
}

void WindowContextTop::exit_fullscreen() {
    is_fullscreen = FALSE;
    gtk_window_unfullscreen(GTK_WINDOW(gtk_widget));
}

void WindowContextTop::request_focus() {
    //JDK-8212060: Window show and then move glitch.
    //The WindowContextBase::set_visible will take care of showing the window.
    //The below code will only handle later request_focus.
    if (is_visible()) {
        gtk_window_present(GTK_WINDOW(gtk_widget));
    }
}

void WindowContextTop::set_focusable(bool focusable) {
    gtk_window_set_accept_focus(GTK_WINDOW(gtk_widget), focusable ? TRUE : FALSE);
}

void WindowContextTop::set_title(const char* title) {
    gtk_window_set_title(GTK_WINDOW(gtk_widget),title);
}

void WindowContextTop::set_alpha(double alpha) {
    gtk_window_set_opacity(GTK_WINDOW(gtk_widget), (gdouble)alpha);
}

void WindowContextTop::set_enabled(bool enabled) {
    if (enabled != geometry.enabled) {
        gtk_widget_set_sensitive(gtk_widget, enabled);
        geometry.enabled = enabled;
        apply_geometry();
    }
}

void WindowContextTop::set_minimum_size(int w, int h) {
    gboolean changed = geometry.minw != w || geometry.minh != h;

    if (!changed) {
        return;
    }

    geometry.minw = w;
    geometry.minh = h;

    apply_geometry();
}

void WindowContextTop::set_maximum_size(int w, int h) {
    gboolean changed = geometry.maxw != w || geometry.maxh != h;

    if (!changed) {
        return;
    }

    geometry.maxw = w;
    geometry.maxh = h;

    apply_geometry();
}

void WindowContextTop::set_icon(GdkPixbuf* pixbuf) {
    gtk_window_set_icon(GTK_WINDOW(gtk_widget), pixbuf);
}

void WindowContextTop::restack(bool restack) {
    gdk_window_restack(gdk_window, NULL, restack ? TRUE : FALSE);
}

void WindowContextTop::set_modal(bool modal, WindowContext* parent) {
    if (modal) {
        //gtk_window_set_type_hint(GTK_WINDOW(gtk_widget), GDK_WINDOW_TYPE_HINT_DIALOG);
        if (parent) {
            gtk_window_set_transient_for(GTK_WINDOW(gtk_widget), parent->get_gtk_window());
        }
    }
    gtk_window_set_modal(GTK_WINDOW(gtk_widget), modal ? TRUE : FALSE);
}

GtkWindow *WindowContextTop::get_gtk_window() {
    return GTK_WINDOW(gtk_widget);
}

WindowGeometry WindowContextTop::get_geometry() {
    return geometry;
}

void WindowContextTop::set_gravity(float x, float y) {
    geometry.gravity_x = x;
    geometry.gravity_y = y;
}

void WindowContextTop::update_ontop_tree(bool on_top) {
    bool effective_on_top = on_top || this->on_top;
    gtk_window_set_keep_above(GTK_WINDOW(gtk_widget), effective_on_top ? TRUE : FALSE);
    for (std::set<WindowContextTop*>::iterator it = children.begin(); it != children.end(); ++it) {
        (*it)->update_ontop_tree(effective_on_top);
    }
}

bool WindowContextTop::on_top_inherited() {
    WindowContext* o = owner;
    while (o) {
        WindowContextTop* topO = dynamic_cast<WindowContextTop*>(o);
        if (!topO) break;
        if (topO->on_top) {
            return true;
        }
        o = topO->owner;
    }
    return false;
}

bool WindowContextTop::effective_on_top() {
    if (owner) {
        WindowContextTop* topO = dynamic_cast<WindowContextTop*>(owner);
        return (topO && topO->effective_on_top()) || on_top;
    }
    return on_top;
}

void WindowContextTop::notify_on_top(bool top) {
    // Do not report effective (i.e. native) values to the FX, only if the user sets it manually
    if (top != effective_on_top() && jwindow) {
        if (on_top_inherited() && !top) {
            // Disallow user's "on top" handling on windows that inherited the property
            gtk_window_set_keep_above(GTK_WINDOW(gtk_widget), TRUE);
        } else {
            on_top = top;
            update_ontop_tree(top);
            mainEnv->CallVoidMethod(jwindow,
                    jWindowNotifyLevelChanged,
                    top ? com_sun_glass_ui_Window_Level_FLOATING :  com_sun_glass_ui_Window_Level_NORMAL);
            CHECK_JNI_EXCEPTION(mainEnv);
        }
    }
}

void WindowContextTop::set_level(int level) {
    if (level == com_sun_glass_ui_Window_Level_NORMAL) {
        on_top = false;
    } else if (level == com_sun_glass_ui_Window_Level_FLOATING
            || level == com_sun_glass_ui_Window_Level_TOPMOST) {
        on_top = true;
    }
    // We need to emulate always on top behaviour on child windows

    if (!on_top_inherited()) {
        update_ontop_tree(on_top);
    }
}

void WindowContextTop::set_owner(WindowContext * owner_ctx) {
    owner = owner_ctx;
}

void WindowContextTop::process_destroy() {
    if (owner) {
        owner->remove_child(this);
    }

    WindowContextBase::process_destroy();
}
