﻿/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * X11 Client Interface
 *
 * Copyright 2013 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2013 Corey Clayton <can.of.tuna@gmail.com>
 * Copyright 2014 Thincast Technologies GmbH
 * Copyright 2014 Norbert Federa <norbert.federa@thincast.com>
 * Copyright 2016 Armin Novak <armin.novak@thincast.com>
 * Copyright 2016 Thincast Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <math.h>
#include <winpr/cast.h>
#include <winpr/assert.h>
#include <winpr/sspicli.h>

#include <float.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#ifdef WITH_XRENDER
#include <X11/extensions/Xrender.h>
#endif

#ifdef WITH_XI
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#endif

#ifdef WITH_XCURSOR
#include <X11/Xcursor/Xcursor.h>
#endif

#ifdef WITH_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#include <X11/XKBlib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <freerdp/freerdp.h>
#include <freerdp/constants.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/color.h>
#include <freerdp/codec/bitmap.h>

#include <freerdp/utils/signal.h>
#include <freerdp/utils/passphrase.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/channels.h>

#include <freerdp/client/file.h>
#include <freerdp/client/cmdline.h>

#include <winpr/crt.h>
#include <winpr/synch.h>
#include <winpr/file.h>
#include <winpr/print.h>
#include <winpr/sysinfo.h>

#include "xf_rail.h"
#if defined(CHANNEL_TSMF_CLIENT)
#include "xf_tsmf.h"
#endif
#include "xf_event.h"
#include "xf_input.h"
#include "xf_cliprdr.h"
#include "xf_disp.h"
#include "xf_video.h"
#include "xf_monitor.h"
#include "xf_graphics.h"
#include "xf_keyboard.h"
#include "xf_channels.h"
#include "xf_client.h"
#include "xfreerdp.h"
#include "xf_utils.h"

#include <freerdp/log.h>
#define TAG CLIENT_TAG("x11")

#define MIN_PIXEL_DIFF 0.001

struct xf_exit_code_map_t
{
	DWORD error;
	int rc;
};
static const struct xf_exit_code_map_t xf_exit_code_map[] = {
	{ FREERDP_ERROR_SUCCESS, XF_EXIT_SUCCESS },
	{ FREERDP_ERROR_AUTHENTICATION_FAILED, XF_EXIT_AUTH_FAILURE },
	{ FREERDP_ERROR_SECURITY_NEGO_CONNECT_FAILED, XF_EXIT_NEGO_FAILURE },
	{ FREERDP_ERROR_CONNECT_LOGON_FAILURE, XF_EXIT_LOGON_FAILURE },
	{ FREERDP_ERROR_CONNECT_ACCOUNT_LOCKED_OUT, XF_EXIT_ACCOUNT_LOCKED_OUT },
	{ FREERDP_ERROR_PRE_CONNECT_FAILED, XF_EXIT_PRE_CONNECT_FAILED },
	{ FREERDP_ERROR_CONNECT_UNDEFINED, XF_EXIT_CONNECT_UNDEFINED },
	{ FREERDP_ERROR_POST_CONNECT_FAILED, XF_EXIT_POST_CONNECT_FAILED },
	{ FREERDP_ERROR_DNS_ERROR, XF_EXIT_DNS_ERROR },
	{ FREERDP_ERROR_DNS_NAME_NOT_FOUND, XF_EXIT_DNS_NAME_NOT_FOUND },
	{ FREERDP_ERROR_CONNECT_FAILED, XF_EXIT_CONNECT_FAILED },
	{ FREERDP_ERROR_MCS_CONNECT_INITIAL_ERROR, XF_EXIT_MCS_CONNECT_INITIAL_ERROR },
	{ FREERDP_ERROR_TLS_CONNECT_FAILED, XF_EXIT_TLS_CONNECT_FAILED },
	{ FREERDP_ERROR_INSUFFICIENT_PRIVILEGES, XF_EXIT_INSUFFICIENT_PRIVILEGES },
	{ FREERDP_ERROR_CONNECT_CANCELLED, XF_EXIT_CONNECT_CANCELLED },
	{ FREERDP_ERROR_CONNECT_TRANSPORT_FAILED, XF_EXIT_CONNECT_TRANSPORT_FAILED },
	{ FREERDP_ERROR_CONNECT_PASSWORD_EXPIRED, XF_EXIT_CONNECT_PASSWORD_EXPIRED },
	{ FREERDP_ERROR_CONNECT_PASSWORD_MUST_CHANGE, XF_EXIT_CONNECT_PASSWORD_MUST_CHANGE },
	{ FREERDP_ERROR_CONNECT_KDC_UNREACHABLE, XF_EXIT_CONNECT_KDC_UNREACHABLE },
	{ FREERDP_ERROR_CONNECT_ACCOUNT_DISABLED, XF_EXIT_CONNECT_ACCOUNT_DISABLED },
	{ FREERDP_ERROR_CONNECT_PASSWORD_CERTAINLY_EXPIRED,
	  XF_EXIT_CONNECT_PASSWORD_CERTAINLY_EXPIRED },
	{ FREERDP_ERROR_CONNECT_CLIENT_REVOKED, XF_EXIT_CONNECT_CLIENT_REVOKED },
	{ FREERDP_ERROR_CONNECT_WRONG_PASSWORD, XF_EXIT_CONNECT_WRONG_PASSWORD },
	{ FREERDP_ERROR_CONNECT_ACCESS_DENIED, XF_EXIT_CONNECT_ACCESS_DENIED },
	{ FREERDP_ERROR_CONNECT_ACCOUNT_RESTRICTION, XF_EXIT_CONNECT_ACCOUNT_RESTRICTION },
	{ FREERDP_ERROR_CONNECT_ACCOUNT_EXPIRED, XF_EXIT_CONNECT_ACCOUNT_EXPIRED },
	{ FREERDP_ERROR_CONNECT_LOGON_TYPE_NOT_GRANTED, XF_EXIT_CONNECT_LOGON_TYPE_NOT_GRANTED },
	{ FREERDP_ERROR_CONNECT_NO_OR_MISSING_CREDENTIALS, XF_EXIT_CONNECT_NO_OR_MISSING_CREDENTIALS }
};

static BOOL xf_setup_x11(xfContext* xfc);
static void xf_teardown_x11(xfContext* xfc);

static int xf_map_error_to_exit_code(DWORD error)
{
	for (size_t x = 0; x < ARRAYSIZE(xf_exit_code_map); x++)
	{
		const struct xf_exit_code_map_t* cur = &xf_exit_code_map[x];
		if (cur->error == error)
			return cur->rc;
	}

	return XF_EXIT_CONN_FAILED;
}

static int (*def_error_handler)(Display*, XErrorEvent*);
static int xf_error_handler_ex(Display* d, XErrorEvent* ev);
static void xf_check_extensions(xfContext* context);
static void xf_window_free(xfContext* xfc);
static BOOL xf_get_pixmap_info(xfContext* xfc);

#ifdef WITH_XRENDER
static void xf_draw_screen_scaled(xfContext* xfc, int x, int y, int w, int h)
{
	XTransform transform = { 0 };
	Picture windowPicture = 0;
	Picture primaryPicture = 0;
	XRenderPictureAttributes pa;
	XRenderPictFormat* picFormat = NULL;
	int x2 = 0;
	int y2 = 0;
	const char* filter = NULL;
	WINPR_ASSERT(xfc);

	rdpSettings* settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	if (xfc->scaledWidth <= 0 || xfc->scaledHeight <= 0)
	{
		WLog_ERR(TAG, "the current window dimensions are invalid");
		return;
	}

	if (freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth) <= 0 ||
	    freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight) <= 0)
	{
		WLog_ERR(TAG, "the window dimensions are invalid");
		return;
	}

	const double xScalingFactor = 1.0 *
	                              freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth) /
	                              (double)xfc->scaledWidth;
	const double yScalingFactor = 1.0 *
	                              freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight) /
	                              (double)xfc->scaledHeight;
	XSetFillStyle(xfc->display, xfc->gc, FillSolid);
	XSetForeground(xfc->display, xfc->gc, 0);
	/* Black out possible space between desktop and window borders */
	{
		XRectangle box1 = { 0, 0, WINPR_ASSERTING_INT_CAST(UINT16, xfc->window->width),
			                WINPR_ASSERTING_INT_CAST(UINT16, xfc->window->height) };
		XRectangle box2 = { WINPR_ASSERTING_INT_CAST(INT16, xfc->offset_x),
			                WINPR_ASSERTING_INT_CAST(INT16, xfc->offset_y),
			                WINPR_ASSERTING_INT_CAST(UINT16, xfc->scaledWidth),
			                WINPR_ASSERTING_INT_CAST(UINT16, xfc->scaledHeight) };
		Region reg1 = XCreateRegion();
		Region reg2 = XCreateRegion();
		XUnionRectWithRegion(&box1, reg1, reg1);
		XUnionRectWithRegion(&box2, reg2, reg2);

		if (XSubtractRegion(reg1, reg2, reg1) && !XEmptyRegion(reg1))
		{
			XSetRegion(xfc->display, xfc->gc, reg1);
			XFillRectangle(xfc->display, xfc->window->handle, xfc->gc, 0, 0,
			               WINPR_ASSERTING_INT_CAST(UINT16, xfc->window->width),
			               WINPR_ASSERTING_INT_CAST(UINT16, xfc->window->height));
			XSetClipMask(xfc->display, xfc->gc, None);
		}

		XDestroyRegion(reg1);
		XDestroyRegion(reg2);
	}
	picFormat = XRenderFindVisualFormat(xfc->display, xfc->visual);
	pa.subwindow_mode = IncludeInferiors;
	primaryPicture =
	    XRenderCreatePicture(xfc->display, xfc->primary, picFormat, CPSubwindowMode, &pa);
	windowPicture =
	    XRenderCreatePicture(xfc->display, xfc->window->handle, picFormat, CPSubwindowMode, &pa);
	/* avoid blurry filter when scaling factor is 2x, 3x, etc
	 * useful when the client has high-dpi monitor */
	filter = FilterBilinear;
	if (fabs(xScalingFactor - yScalingFactor) < MIN_PIXEL_DIFF)
	{
		const double inverseX = 1.0 / xScalingFactor;
		const double inverseRoundedX = round(inverseX);
		const double absInverse = fabs(inverseX - inverseRoundedX);

		if (absInverse < MIN_PIXEL_DIFF)
			filter = FilterNearest;
	}
	XRenderSetPictureFilter(xfc->display, primaryPicture, filter, 0, 0);
	transform.matrix[0][0] = XDoubleToFixed(xScalingFactor);
	transform.matrix[0][1] = XDoubleToFixed(0.0);
	transform.matrix[0][2] = XDoubleToFixed(0.0);
	transform.matrix[1][0] = XDoubleToFixed(0.0);
	transform.matrix[1][1] = XDoubleToFixed(yScalingFactor);
	transform.matrix[1][2] = XDoubleToFixed(0.0);
	transform.matrix[2][0] = XDoubleToFixed(0.0);
	transform.matrix[2][1] = XDoubleToFixed(0.0);
	transform.matrix[2][2] = XDoubleToFixed(1.0);
	/* calculate and fix up scaled coordinates */
	x2 = x + w;
	y2 = y + h;

	const double dx1 = floor(x / xScalingFactor);
	const double dy1 = floor(y / yScalingFactor);
	const double dx2 = ceil(x2 / xScalingFactor);
	const double dy2 = ceil(y2 / yScalingFactor);
	x = ((int)dx1) - 1;
	y = ((int)dy1) - 1;
	w = ((int)dx2) + 1 - x;
	h = ((int)dy2) + 1 - y;
	XRenderSetPictureTransform(xfc->display, primaryPicture, &transform);
	XRenderComposite(xfc->display, PictOpSrc, primaryPicture, 0, windowPicture, x, y, 0, 0,
	                 xfc->offset_x + x, xfc->offset_y + y, WINPR_ASSERTING_INT_CAST(uint32_t, w),
	                 WINPR_ASSERTING_INT_CAST(uint32_t, h));
	XRenderFreePicture(xfc->display, primaryPicture);
	XRenderFreePicture(xfc->display, windowPicture);
}

BOOL xf_picture_transform_required(xfContext* xfc)
{
	rdpSettings* settings = NULL;

	WINPR_ASSERT(xfc);

	settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	if ((xfc->offset_x != 0) || (xfc->offset_y != 0) ||
	    (xfc->scaledWidth != (INT64)freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth)) ||
	    (xfc->scaledHeight != (INT64)freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight)))
	{
		return TRUE;
	}

	return FALSE;
}
#endif /* WITH_XRENDER defined */

void xf_draw_screen_(xfContext* xfc, int x, int y, int w, int h, const char* fkt,
                     WINPR_ATTR_UNUSED const char* file, WINPR_ATTR_UNUSED int line)
{
	if (!xfc)
	{
		WLog_DBG(TAG, "called from [%s] xfc=%p", fkt, xfc);
		return;
	}

	if (w == 0 || h == 0)
	{
		WLog_WARN(TAG, "invalid width and/or height specified: w=%d h=%d", w, h);
		return;
	}

	if (!xfc->window)
	{
		WLog_WARN(TAG, "invalid xfc->window=%p", xfc->window);
		return;
	}

#ifdef WITH_XRENDER

	if (xf_picture_transform_required(xfc))
	{
		xf_draw_screen_scaled(xfc, x, y, w, h);
		return;
	}

#endif
	XCopyArea(xfc->display, xfc->primary, xfc->window->handle, xfc->gc, x, y,
	          WINPR_ASSERTING_INT_CAST(uint32_t, w), WINPR_ASSERTING_INT_CAST(uint32_t, h), x, y);
}

static BOOL xf_desktop_resize(rdpContext* context)
{
	rdpSettings* settings = NULL;
	xfContext* xfc = (xfContext*)context;

	WINPR_ASSERT(xfc);

	settings = context->settings;
	WINPR_ASSERT(settings);

	if (xfc->primary)
	{
		BOOL same = (xfc->primary == xfc->drawing) ? TRUE : FALSE;
		XFreePixmap(xfc->display, xfc->primary);

		WINPR_ASSERT(xfc->depth != 0);
		if (!(xfc->primary =
		          XCreatePixmap(xfc->display, xfc->drawable,
		                        freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
		                        freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight),
		                        WINPR_ASSERTING_INT_CAST(uint32_t, xfc->depth))))
			return FALSE;

		if (same)
			xfc->drawing = xfc->primary;
	}

#ifdef WITH_XRENDER

	if (!freerdp_settings_get_bool(settings, FreeRDP_SmartSizing))
	{
		xfc->scaledWidth = WINPR_ASSERTING_INT_CAST(
		    int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth));
		xfc->scaledHeight = WINPR_ASSERTING_INT_CAST(
		    int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight));
	}

#endif

	if (!xfc->fullscreen)
	{
		xf_ResizeDesktopWindow(
		    xfc, xfc->window,
		    WINPR_ASSERTING_INT_CAST(int,
		                             freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth)),
		    WINPR_ASSERTING_INT_CAST(int,
		                             freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight)));
	}
	else
	{
#ifdef WITH_XRENDER

		if (!freerdp_settings_get_bool(settings, FreeRDP_SmartSizing))
#endif
		{
			/* Update the saved width and height values the window will be
			 * resized to when toggling out of fullscreen */
			xfc->savedWidth = WINPR_ASSERTING_INT_CAST(
			    int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth));
			xfc->savedHeight = WINPR_ASSERTING_INT_CAST(
			    int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight));
		}

		XSetFunction(xfc->display, xfc->gc, GXcopy);
		XSetFillStyle(xfc->display, xfc->gc, FillSolid);
		XSetForeground(xfc->display, xfc->gc, 0);
		XFillRectangle(xfc->display, xfc->drawable, xfc->gc, 0, 0,
		               freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
		               freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight));
	}

	return TRUE;
}

static BOOL xf_paint(xfContext* xfc, const GDI_RGN* region)
{
	WINPR_ASSERT(xfc);
	WINPR_ASSERT(region);

	if (xfc->remote_app)
	{
		const RECTANGLE_16 rect = { .left = WINPR_ASSERTING_INT_CAST(UINT16, region->x),
			                        .top = WINPR_ASSERTING_INT_CAST(UINT16, region->y),
			                        .right =
			                            WINPR_ASSERTING_INT_CAST(UINT16, region->x + region->w),
			                        .bottom =
			                            WINPR_ASSERTING_INT_CAST(UINT16, region->y + region->h) };
		xf_rail_paint(xfc, &rect);
	}
	else
	{
		XPutImage(xfc->display, xfc->primary, xfc->gc, xfc->image, region->x, region->y, region->x,
		          region->y, WINPR_ASSERTING_INT_CAST(UINT16, region->w),
		          WINPR_ASSERTING_INT_CAST(UINT16, region->h));
		xf_draw_screen(xfc, region->x, region->y, region->w, region->h);
	}
	return TRUE;
}

static BOOL xf_end_paint(rdpContext* context)
{
	xfContext* xfc = (xfContext*)context;
	rdpGdi* gdi = context->gdi;

	if (gdi->suppressOutput)
		return TRUE;

	HGDI_DC hdc = gdi->primary->hdc;

	if (!xfc->complex_regions)
	{
		const GDI_RGN* rgn = hdc->hwnd->invalid;
		if (rgn->null)
			return TRUE;
		xf_lock_x11(xfc);
		if (!xf_paint(xfc, rgn))
			return FALSE;
		xf_unlock_x11(xfc);
	}
	else
	{
		const INT32 ninvalid = hdc->hwnd->ninvalid;
		const GDI_RGN* cinvalid = hdc->hwnd->cinvalid;

		if (hdc->hwnd->ninvalid < 1)
			return TRUE;

		xf_lock_x11(xfc);

		for (INT32 i = 0; i < ninvalid; i++)
		{
			const GDI_RGN* rgn = &cinvalid[i];
			if (!xf_paint(xfc, rgn))
				return FALSE;
		}

		XFlush(xfc->display);
		xf_unlock_x11(xfc);
	}

	hdc->hwnd->invalid->null = TRUE;
	hdc->hwnd->ninvalid = 0;
	return TRUE;
}

static BOOL xf_sw_desktop_resize(rdpContext* context)
{
	rdpGdi* gdi = context->gdi;
	xfContext* xfc = (xfContext*)context;
	rdpSettings* settings = context->settings;
	BOOL ret = FALSE;

	if (!gdi_resize(gdi, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
	                freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight)))
		return FALSE;

	/* Do not lock during gdi_resize, there might still be drawing operations in progress.
	 * locking will deadlock. */
	xf_lock_x11(xfc);
	if (xfc->image)
	{
		xfc->image->data = NULL;
		XDestroyImage(xfc->image);
	}

	WINPR_ASSERT(xfc->depth != 0);
	if (!(xfc->image = XCreateImage(
	          xfc->display, xfc->visual, WINPR_ASSERTING_INT_CAST(uint32_t, xfc->depth), ZPixmap, 0,
	          (char*)gdi->primary_buffer, WINPR_ASSERTING_INT_CAST(uint32_t, gdi->width),
	          WINPR_ASSERTING_INT_CAST(uint32_t, gdi->height), xfc->scanline_pad,
	          WINPR_ASSERTING_INT_CAST(int, gdi->stride))))
	{
		goto out;
	}

	xfc->image->byte_order = LSBFirst;
	xfc->image->bitmap_bit_order = LSBFirst;
	ret = xf_desktop_resize(context);
out:
	xf_unlock_x11(xfc);
	return ret;
}

static BOOL xf_process_x_events(freerdp* instance)
{
	BOOL status = TRUE;
	int pending_status = 1;
	xfContext* xfc = (xfContext*)instance->context;

	while (pending_status)
	{
		xf_lock_x11(xfc);
		pending_status = XPending(xfc->display);

		if (pending_status)
		{
			XEvent xevent = { 0 };

			XNextEvent(xfc->display, &xevent);
			status = xf_event_process(instance, &xevent);
		}
		xf_unlock_x11(xfc);
		if (!status)
			break;
	}

	return status;
}

static char* xf_window_get_title(rdpSettings* settings)
{
	BOOL port = 0;
	char* windowTitle = NULL;
	size_t size = 0;
	const char* prefix = "FreeRDP:";

	if (!settings)
		return NULL;

	const char* name = freerdp_settings_get_string(settings, FreeRDP_ServerHostname);
	const char* title = freerdp_settings_get_string(settings, FreeRDP_WindowTitle);

	if (title)
		return _strdup(title);

	port = (freerdp_settings_get_uint32(settings, FreeRDP_ServerPort) != 3389);
	/* Just assume a window title is never longer than a filename... */
	size = strnlen(name, MAX_PATH) + 16;
	windowTitle = calloc(size, sizeof(char));

	if (!windowTitle)
		return NULL;

	if (!port)
		(void)sprintf_s(windowTitle, size, "%s %s", prefix, name);
	else
		(void)sprintf_s(windowTitle, size, "%s %s:%" PRIu32, prefix, name,
		                freerdp_settings_get_uint32(settings, FreeRDP_ServerPort));

	return windowTitle;
}

BOOL xf_create_window(xfContext* xfc)
{
	XGCValues gcv = { 0 };
	XEvent xevent = { 0 };
	char* windowTitle = NULL;

	WINPR_ASSERT(xfc);
	rdpSettings* settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	int width =
	    WINPR_ASSERTING_INT_CAST(int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth));
	int height =
	    WINPR_ASSERTING_INT_CAST(int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight));

	const XSetWindowAttributes empty = { 0 };
	xfc->attribs = empty;

	if (xfc->remote_app)
		xfc->depth = 32;
	else
		xfc->depth = DefaultDepthOfScreen(xfc->screen);

	XVisualInfo vinfo = { 0 };
	if (XMatchVisualInfo(xfc->display, xfc->screen_number, xfc->depth, TrueColor, &vinfo))
	{
		Window root = XDefaultRootWindow(xfc->display);
		xfc->visual = vinfo.visual;
		xfc->attribs.colormap = xfc->colormap =
		    XCreateColormap(xfc->display, root, vinfo.visual, AllocNone);
	}
	else
	{
		if (xfc->remote_app)
		{
			WLog_WARN(TAG, "running in remote app mode, but XServer does not support transparency");
			WLog_WARN(TAG, "display of remote applications might be distorted (black frames, ...)");
		}
		xfc->depth = DefaultDepthOfScreen(xfc->screen);
		xfc->visual = DefaultVisual(xfc->display, xfc->screen_number);
		xfc->attribs.colormap = xfc->colormap = DefaultColormap(xfc->display, xfc->screen_number);
	}

	/*
	 * Detect if the server visual has an inverted colormap
	 * (BGR vs RGB, or red being the least significant byte)
	 */
	if (vinfo.red_mask & 0xFF)
	{
		xfc->invert = FALSE;
	}

	if (!xfc->remote_app)
	{
		xfc->attribs.background_pixel = BlackPixelOfScreen(xfc->screen);
		xfc->attribs.border_pixel = WhitePixelOfScreen(xfc->screen);
		xfc->attribs.backing_store = xfc->primary ? NotUseful : Always;
		xfc->attribs.override_redirect = False;

		xfc->attribs.bit_gravity = NorthWestGravity;
		xfc->attribs.win_gravity = NorthWestGravity;
		xfc->attribs_mask = CWBackPixel | CWBackingStore | CWOverrideRedirect | CWColormap |
		                    CWBorderPixel | CWWinGravity | CWBitGravity;

#ifdef WITH_XRENDER
		xfc->offset_x = 0;
		xfc->offset_y = 0;
#endif
		windowTitle = xf_window_get_title(settings);

		if (!windowTitle)
			return FALSE;

#ifdef WITH_XRENDER

		if (freerdp_settings_get_bool(settings, FreeRDP_SmartSizing) && !xfc->fullscreen)
		{
			if (freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingWidth) > 0)
				width = WINPR_ASSERTING_INT_CAST(
				    int, freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingWidth));

			if (freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingHeight) > 0)
				height = WINPR_ASSERTING_INT_CAST(
				    int, freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingHeight));

			xfc->scaledWidth = width;
			xfc->scaledHeight = height;
		}

#endif
		xfc->window = xf_CreateDesktopWindow(xfc, windowTitle, width, height);
		free(windowTitle);

		if (xfc->fullscreen)
			xf_SetWindowFullscreen(xfc, xfc->window, xfc->fullscreen);

		xfc->unobscured = (xevent.xvisibility.state == VisibilityUnobscured);
		XSetWMProtocols(xfc->display, xfc->window->handle, &(xfc->WM_DELETE_WINDOW), 1);
		xfc->drawable = xfc->window->handle;
	}
	else
	{
		xfc->attribs.border_pixel = 0;
		xfc->attribs.background_pixel = 0;
		xfc->attribs.backing_store = xfc->primary ? NotUseful : Always;
		xfc->attribs.override_redirect = False;

		xfc->attribs.bit_gravity = NorthWestGravity;
		xfc->attribs.win_gravity = NorthWestGravity;
		xfc->attribs_mask = CWBackPixel | CWBackingStore | CWOverrideRedirect | CWColormap |
		                    CWBorderPixel | CWWinGravity | CWBitGravity;

		xfc->drawable = xf_CreateDummyWindow(xfc);
	}

	if (!xfc->gc)
		xfc->gc = XCreateGC(xfc->display, xfc->drawable, GCGraphicsExposures, &gcv);

	WINPR_ASSERT(xfc->depth != 0);
	if (!xfc->primary)
		xfc->primary = XCreatePixmap(xfc->display, xfc->drawable,
		                             freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
		                             freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight),
		                             WINPR_ASSERTING_INT_CAST(uint32_t, xfc->depth));

	xfc->drawing = xfc->primary;

	if (!xfc->bitmap_mono)
		xfc->bitmap_mono = XCreatePixmap(xfc->display, xfc->drawable, 8, 8, 1);

	if (!xfc->gc_mono)
		xfc->gc_mono = XCreateGC(xfc->display, xfc->bitmap_mono, GCGraphicsExposures, &gcv);

	XSetFunction(xfc->display, xfc->gc, GXcopy);
	XSetFillStyle(xfc->display, xfc->gc, FillSolid);
	XSetForeground(xfc->display, xfc->gc, BlackPixelOfScreen(xfc->screen));
	XFillRectangle(xfc->display, xfc->primary, xfc->gc, 0, 0,
	               freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
	               freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight));
	XFlush(xfc->display);

	return TRUE;
}

BOOL xf_create_image(xfContext* xfc)
{
	WINPR_ASSERT(xfc);
	if (!xfc->image)
	{
		const rdpSettings* settings = xfc->common.context.settings;
		rdpGdi* cgdi = xfc->common.context.gdi;
		WINPR_ASSERT(cgdi);

		WINPR_ASSERT(xfc->depth != 0);
		xfc->image =
		    XCreateImage(xfc->display, xfc->visual, WINPR_ASSERTING_INT_CAST(uint32_t, xfc->depth),
		                 ZPixmap, 0, (char*)cgdi->primary_buffer,
		                 freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
		                 freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight),
		                 xfc->scanline_pad, WINPR_ASSERTING_INT_CAST(int, cgdi->stride));
		xfc->image->byte_order = LSBFirst;
		xfc->image->bitmap_bit_order = LSBFirst;
	}
	return TRUE;
}

static void xf_window_free(xfContext* xfc)
{
	if (xfc->window)
	{
		xf_DestroyDesktopWindow(xfc, xfc->window);
		xfc->window = NULL;
	}

#if defined(CHANNEL_TSMF_CLIENT)
	if (xfc->xv_context)
	{
		xf_tsmf_uninit(xfc, NULL);
		xfc->xv_context = NULL;
	}
#endif

	if (xfc->image)
	{
		xfc->image->data = NULL;
		XDestroyImage(xfc->image);
		xfc->image = NULL;
	}

	if (xfc->bitmap_mono)
	{
		XFreePixmap(xfc->display, xfc->bitmap_mono);
		xfc->bitmap_mono = 0;
	}

	if (xfc->gc_mono)
	{
		XFreeGC(xfc->display, xfc->gc_mono);
		xfc->gc_mono = 0;
	}

	if (xfc->primary)
	{
		XFreePixmap(xfc->display, xfc->primary);
		xfc->primary = 0;
	}

	if (xfc->gc)
	{
		XFreeGC(xfc->display, xfc->gc);
		xfc->gc = 0;
	}
}

void xf_toggle_fullscreen(xfContext* xfc)
{
	WindowStateChangeEventArgs e = { 0 };
	rdpContext* context = (rdpContext*)xfc;
	rdpSettings* settings = context->settings;

	/*
	  when debugging, ungrab keyboard when toggling fullscreen
	  to allow keyboard usage on the debugger
	*/
	if (xfc->debug)
		xf_ungrab(xfc);

	xfc->fullscreen = (xfc->fullscreen) ? FALSE : TRUE;
	xfc->decorations =
	    (xfc->fullscreen) ? FALSE : freerdp_settings_get_bool(settings, FreeRDP_Decorations);
	xf_SetWindowFullscreen(xfc, xfc->window, xfc->fullscreen);
	EventArgsInit(&e, "xfreerdp");
	e.state = xfc->fullscreen ? FREERDP_WINDOW_STATE_FULLSCREEN : 0;
	PubSub_OnWindowStateChange(context->pubSub, context, &e);
}

void xf_minimize(xfContext* xfc)
{
	WindowStateChangeEventArgs e = { 0 };
	rdpContext* context = (rdpContext*)xfc;
	WINPR_ASSERT(context);

	/*
	  when debugging, ungrab keyboard when toggling fullscreen
	  to allow keyboard usage on the debugger
	*/
	if (xfc->debug)
		xf_ungrab(xfc);

	xf_SetWindowMinimized(xfc, xfc->window);

	e.state = xfc->fullscreen ? FREERDP_WINDOW_STATE_FULLSCREEN : 0;
	PubSub_OnWindowStateChange(context->pubSub, context, &e);
}

void xf_lock_x11_(xfContext* xfc, WINPR_ATTR_UNUSED const char* fkt)
{
	if (!xfc->UseXThreads)
		(void)WaitForSingleObject(xfc->mutex, INFINITE);
	else
		XLockDisplay(xfc->display);

	xfc->locked++;
}

void xf_unlock_x11_(xfContext* xfc, WINPR_ATTR_UNUSED const char* fkt)
{
	if (xfc->locked == 0)
		WLog_WARN(TAG, "X11: trying to unlock although not locked!");
	else
		xfc->locked--;

	if (!xfc->UseXThreads)
		(void)ReleaseMutex(xfc->mutex);
	else
		XUnlockDisplay(xfc->display);
}

static BOOL xf_get_pixmap_info(xfContext* xfc)
{
	int pf_count = 0;
	XPixmapFormatValues* pfs = NULL;

	WINPR_ASSERT(xfc->display);
	pfs = XListPixmapFormats(xfc->display, &pf_count);

	if (!pfs)
	{
		WLog_ERR(TAG, "XListPixmapFormats failed");
		return 1;
	}

	WINPR_ASSERT(xfc->depth != 0);
	for (int i = 0; i < pf_count; i++)
	{
		const XPixmapFormatValues* pf = &pfs[i];

		if (pf->depth == xfc->depth)
		{
			xfc->scanline_pad = pf->scanline_pad;
			break;
		}
	}

	XFree(pfs);
	if ((xfc->visual == NULL) || (xfc->scanline_pad == 0))
		return FALSE;

	return TRUE;
}

static int xf_error_handler(Display* d, XErrorEvent* ev)
{
	char buf[256] = { 0 };
	XGetErrorText(d, ev->error_code, buf, sizeof(buf));
	WLog_ERR(TAG, "%s", buf);
	winpr_log_backtrace(TAG, WLOG_ERROR, 20);

	if (def_error_handler)
		return def_error_handler(d, ev);

	return 0;
}

static int xf_error_handler_ex(Display* d, XErrorEvent* ev)
{
	/*
	 * ungrab the keyboard, in case a debugger is running in
	 * another window. This make xf_error_handler() a potential
	 * debugger breakpoint.
	 */

	XUngrabKeyboard(d, CurrentTime);
	XUngrabPointer(d, CurrentTime);
	return xf_error_handler(d, ev);
}

static BOOL xf_play_sound(rdpContext* context, const PLAY_SOUND_UPDATE* play_sound)
{
	xfContext* xfc = (xfContext*)context;
	WINPR_UNUSED(play_sound);
	XkbBell(xfc->display, None, 100, 0);
	return TRUE;
}

static void xf_check_extensions(xfContext* context)
{
	int xkb_opcode = 0;
	int xkb_event = 0;
	int xkb_error = 0;
	int xkb_major = XkbMajorVersion;
	int xkb_minor = XkbMinorVersion;

	if (XkbLibraryVersion(&xkb_major, &xkb_minor) &&
	    XkbQueryExtension(context->display, &xkb_opcode, &xkb_event, &xkb_error, &xkb_major,
	                      &xkb_minor))
	{
		context->xkbAvailable = TRUE;
	}

#ifdef WITH_XRENDER
	{
		int xrender_event_base = 0;
		int xrender_error_base = 0;

		if (XRenderQueryExtension(context->display, &xrender_event_base, &xrender_error_base))
		{
			context->xrenderAvailable = TRUE;
		}
	}
#endif
}

#ifdef WITH_XI
/* Input device which does NOT have the correct mapping. We must disregard */
/* this device when trying to find the input device which is the pointer.  */
static const char TEST_PTR_STR[] = "Virtual core XTEST pointer";
static const size_t TEST_PTR_LEN = sizeof(TEST_PTR_STR) / sizeof(char);
#endif /* WITH_XI */

static void xf_get_x11_button_map(xfContext* xfc, unsigned char* x11_map)
{
#ifdef WITH_XI
	int opcode = 0;
	int event = 0;
	int error = 0;
	XDevice* ptr_dev = NULL;
	XExtensionVersion* version = NULL;
	XDeviceInfo* devices1 = NULL;
	XIDeviceInfo* devices2 = NULL;
	int num_devices = 0;

	if (XQueryExtension(xfc->display, "XInputExtension", &opcode, &event, &error))
	{
		WLog_DBG(TAG, "Searching for XInput pointer device");
		ptr_dev = NULL;
		/* loop through every device, looking for a pointer */
		version = XGetExtensionVersion(xfc->display, INAME);

		if (version->major_version >= 2)
		{
			/* XID of pointer device using XInput version 2 */
			devices2 = XIQueryDevice(xfc->display, XIAllDevices, &num_devices);

			if (devices2)
			{
				for (int i = 0; i < num_devices; ++i)
				{
					XIDeviceInfo* dev = &devices2[i];
					if ((dev->use == XISlavePointer) &&
					    (strncmp(dev->name, TEST_PTR_STR, TEST_PTR_LEN) != 0))
					{
						ptr_dev = XOpenDevice(xfc->display,
						                      WINPR_ASSERTING_INT_CAST(uint32_t, dev->deviceid));
						if (ptr_dev)
							break;
					}
				}

				XIFreeDeviceInfo(devices2);
			}
		}
		else
		{
			/* XID of pointer device using XInput version 1 */
			devices1 = XListInputDevices(xfc->display, &num_devices);

			if (devices1)
			{
				for (int i = 0; i < num_devices; ++i)
				{
					if ((devices1[i].use == IsXExtensionPointer) &&
					    (strncmp(devices1[i].name, TEST_PTR_STR, TEST_PTR_LEN) != 0))
					{
						ptr_dev = XOpenDevice(xfc->display, devices1[i].id);
						if (ptr_dev)
							break;
					}
				}

				XFreeDeviceList(devices1);
			}
		}

		XFree(version);

		/* get button mapping from input extension if there is a pointer device; */
		/* otherwise leave unchanged.                                            */
		if (ptr_dev)
		{
			WLog_DBG(TAG, "Pointer device: %d", ptr_dev->device_id);
			XGetDeviceButtonMapping(xfc->display, ptr_dev, x11_map, NUM_BUTTONS_MAPPED);
			XCloseDevice(xfc->display, ptr_dev);
		}
		else
		{
			WLog_DBG(TAG, "No pointer device found!");
		}
	}
	else
#endif /* WITH_XI */
	{
		WLog_DBG(TAG, "Get global pointer mapping (no XInput)");
		XGetPointerMapping(xfc->display, x11_map, NUM_BUTTONS_MAPPED);
	}
}

/* Assignment of physical (not logical) mouse buttons to wire flags. */
/* Notice that the middle button is 2 in X11, but 3 in RDP.          */
static const button_map xf_button_flags[NUM_BUTTONS_MAPPED] = {
	{ Button1, PTR_FLAGS_BUTTON1 },
	{ Button2, PTR_FLAGS_BUTTON3 },
	{ Button3, PTR_FLAGS_BUTTON2 },
	{ Button4, PTR_FLAGS_WHEEL | 0x78 },
	/* Negative value is 9bit twos complement */
	{ Button5, PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x100 - 0x78) },
	{ 6, PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x100 - 0x78) },
	{ 7, PTR_FLAGS_HWHEEL | 0x78 },
	{ 8, PTR_XFLAGS_BUTTON1 },
	{ 9, PTR_XFLAGS_BUTTON2 },
	{ 97, PTR_XFLAGS_BUTTON1 },
	{ 112, PTR_XFLAGS_BUTTON2 }
};

static UINT16 get_flags_for_button(size_t button)
{
	for (size_t x = 0; x < ARRAYSIZE(xf_button_flags); x++)
	{
		const button_map* map = &xf_button_flags[x];

		if (map->button == button)
			return map->flags;
	}

	return 0;
}

void xf_button_map_init(xfContext* xfc)
{
	size_t pos = 0;
	/* loop counter for array initialization */

	/* logical mouse button which is used for each physical mouse  */
	/* button (indexed from zero). This is the default map.        */
	unsigned char x11_map[112] = { 0 };

	WINPR_ASSERT(xfc);
	WINPR_ASSERT(xfc->common.context.settings);

	x11_map[0] = Button1;
	x11_map[1] = Button2;
	x11_map[2] = Button3;
	x11_map[3] = Button4;
	x11_map[4] = Button5;
	x11_map[5] = 6;
	x11_map[6] = 7;
	x11_map[7] = 8;
	x11_map[8] = 9;
	x11_map[96] = 97;
	x11_map[111] = 112;

	/* query system for actual remapping */
	if (freerdp_settings_get_bool(xfc->common.context.settings, FreeRDP_UnmapButtons))
	{
		xf_get_x11_button_map(xfc, x11_map);
	}

	/* iterate over all (mapped) physical buttons; for each of them */
	/* find the logical button in X11, and assign to this the       */
	/* appropriate value to send over the RDP wire.                 */
	for (size_t physical = 0; physical < ARRAYSIZE(x11_map); ++physical)
	{
		const unsigned char logical = x11_map[physical];
		const UINT16 flags = get_flags_for_button(logical);

		if ((logical != 0) && (flags != 0))
		{
			if (pos >= NUM_BUTTONS_MAPPED)
			{
				WLog_ERR(TAG, "Failed to map mouse button to RDP button, no space");
			}
			else
			{
				button_map* map = &xfc->button_map[pos++];
				map->button = logical;
				map->flags = get_flags_for_button(physical + Button1);
			}
		}
	}
}

/**
 * Callback given to freerdp_connect() to process the pre-connect operations.
 * It will fill the rdp_freerdp structure (instance) with the appropriate options to use for the
 * connection.
 *
 * @param instance - pointer to the rdp_freerdp structure that contains the connection's parameters,
 * and will be filled with the appropriate information.
 *
 * @return TRUE if successful. FALSE otherwise.
 * Can exit with error code XF_EXIT_PARSE_ARGUMENTS if there is an error in the parameters.
 */
static BOOL xf_pre_connect(freerdp* instance)
{
	UINT32 maxWidth = 0;
	UINT32 maxHeight = 0;

	WINPR_ASSERT(instance);

	rdpContext* context = instance->context;
	WINPR_ASSERT(context);
	xfContext* xfc = (xfContext*)context;

	rdpSettings* settings = context->settings;
	WINPR_ASSERT(settings);

	if (!freerdp_settings_set_bool(settings, FreeRDP_CertificateCallbackPreferPEM, TRUE))
		return FALSE;

	if (!freerdp_settings_get_bool(settings, FreeRDP_AuthenticationOnly))
	{
		if (!xf_setup_x11(xfc))
			return FALSE;
	}

	if (!freerdp_settings_set_uint32(settings, FreeRDP_OsMajorType, OSMAJORTYPE_UNIX))
		return FALSE;
	if (!freerdp_settings_set_uint32(settings, FreeRDP_OsMinorType, OSMINORTYPE_NATIVE_XSERVER))
		return FALSE;
	PubSub_SubscribeChannelConnected(context->pubSub, xf_OnChannelConnectedEventHandler);
	PubSub_SubscribeChannelDisconnected(context->pubSub, xf_OnChannelDisconnectedEventHandler);

	if (!freerdp_settings_get_string(settings, FreeRDP_Username) &&
	    !freerdp_settings_get_bool(settings, FreeRDP_CredentialsFromStdin) &&
	    !freerdp_settings_get_bool(settings, FreeRDP_SmartcardLogon))
	{
		char login_name[MAX_PATH] = { 0 };
		ULONG size = sizeof(login_name) - 1;

		if (GetUserNameExA(NameSamCompatible, login_name, &size))
		{
			if (!freerdp_settings_set_string(settings, FreeRDP_Username, login_name))
				return FALSE;

			WLog_INFO(TAG, "No user name set. - Using login name: %s",
			          freerdp_settings_get_string(settings, FreeRDP_Username));
		}
	}

	if (freerdp_settings_get_bool(settings, FreeRDP_AuthenticationOnly))
	{
		/* Check +auth-only has a username and password. */
		if (!freerdp_settings_get_string(settings, FreeRDP_Password))
		{
			WLog_INFO(TAG, "auth-only, but no password set. Please provide one.");
			return FALSE;
		}

		WLog_INFO(TAG, "Authentication only. Don't connect to X.");
	}

	if (!freerdp_settings_get_bool(settings, FreeRDP_AuthenticationOnly))
	{
		const char* KeyboardRemappingList = freerdp_settings_get_string(
		    xfc->common.context.settings, FreeRDP_KeyboardRemappingList);

		xfc->remap_table = freerdp_keyboard_remap_string_to_list(KeyboardRemappingList);
		if (!xfc->remap_table)
			return FALSE;
		if (!xf_keyboard_init(xfc))
			return FALSE;
		if (!xf_keyboard_action_script_init(xfc))
			return FALSE;
		if (!xf_detect_monitors(xfc, &maxWidth, &maxHeight))
			return FALSE;
	}

	if (maxWidth && maxHeight && !freerdp_settings_get_bool(settings, FreeRDP_SmartSizing))
	{
		if (!freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, maxWidth))
			return FALSE;
		if (!freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, maxHeight))
			return FALSE;
	}

#ifdef WITH_XRENDER

	/**
	 * If /f is specified in combination with /smart-sizing:widthxheight then
	 * we run the session in the /smart-sizing dimensions scaled to full screen
	 */
	if (freerdp_settings_get_bool(settings, FreeRDP_Fullscreen) &&
	    freerdp_settings_get_bool(settings, FreeRDP_SmartSizing) &&
	    (freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingWidth) > 0) &&
	    (freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingHeight) > 0))
	{
		if (!freerdp_settings_set_uint32(
		        settings, FreeRDP_DesktopWidth,
		        freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingWidth)))
			return FALSE;
		if (!freerdp_settings_set_uint32(
		        settings, FreeRDP_DesktopHeight,
		        freerdp_settings_get_uint32(settings, FreeRDP_SmartSizingHeight)))
			return FALSE;
	}

#endif
	xfc->fullscreen = freerdp_settings_get_bool(settings, FreeRDP_Fullscreen);
	xfc->decorations = freerdp_settings_get_bool(settings, FreeRDP_Decorations);
	xfc->grab_keyboard = freerdp_settings_get_bool(settings, FreeRDP_GrabKeyboard);
	xfc->fullscreen_toggle = freerdp_settings_get_bool(settings, FreeRDP_ToggleFullscreen);
	if (!freerdp_settings_get_bool(settings, FreeRDP_AuthenticationOnly))
		xf_button_map_init(xfc);
	return TRUE;
}

static BOOL xf_inject_keypress(rdpContext* context, const char* buffer, size_t size)
{
	WCHAR wbuffer[64] = { 0 };
	const SSIZE_T len = ConvertUtf8NToWChar(buffer, size, wbuffer, ARRAYSIZE(wbuffer));
	if (len < 0)
		return FALSE;

	rdpInput* input = context->input;
	WINPR_ASSERT(input);

	for (SSIZE_T x = 0; x < len; x++)
	{
		const WCHAR code = wbuffer[x];
		freerdp_input_send_unicode_keyboard_event(input, 0, code);
		Sleep(5);
		freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, code);
		Sleep(5);
	}
	return TRUE;
}

static BOOL xf_process_pipe(rdpContext* context, const char* pipe)
{
	int fd = open(pipe, O_NONBLOCK | O_RDONLY);
	if (fd < 0)
	{
		char ebuffer[256] = { 0 };
		WLog_ERR(TAG, "pipe '%s' open returned %s [%d]", pipe,
		         winpr_strerror(errno, ebuffer, sizeof(ebuffer)), errno);
		return FALSE;
	}
	while (!freerdp_shall_disconnect_context(context))
	{
		char buffer[64] = { 0 };
		ssize_t rd = read(fd, buffer, sizeof(buffer) - 1);
		if (rd == 0)
		{
			char ebuffer[256] = { 0 };
			if ((errno == EAGAIN) || (errno == 0))
			{
				Sleep(100);
				continue;
			}

			// EOF, abort reading.
			WLog_ERR(TAG, "pipe '%s' read returned %s [%d]", pipe,
			         winpr_strerror(errno, ebuffer, sizeof(ebuffer)), errno);
			break;
		}
		else if (rd < 0)
		{
			char ebuffer[256] = { 0 };
			WLog_ERR(TAG, "pipe '%s' read returned %s [%d]", pipe,
			         winpr_strerror(errno, ebuffer, sizeof(ebuffer)), errno);
			break;
		}
		else
		{
			if (!xf_inject_keypress(context, buffer, WINPR_ASSERTING_INT_CAST(size_t, rd)))
				break;
		}
	}
	close(fd);
	return TRUE;
}

static void cleanup_pipe(WINPR_ATTR_UNUSED int signum, WINPR_ATTR_UNUSED const char* signame,
                         void* context)
{
	const char* pipe = context;
	if (!pipe)
		return;
	unlink(pipe);
}

static DWORD WINAPI xf_handle_pipe(void* arg)
{
	xfContext* xfc = arg;
	WINPR_ASSERT(xfc);

	rdpContext* context = &xfc->common.context;
	WINPR_ASSERT(context);

	rdpSettings* settings = context->settings;
	WINPR_ASSERT(settings);

	const char* pipe = freerdp_settings_get_string(settings, FreeRDP_KeyboardPipeName);
	WINPR_ASSERT(pipe);

	const int rc = mkfifo(pipe, S_IWUSR | S_IRUSR);
	if (rc != 0)
	{
		char ebuffer[256] = { 0 };
		WLog_ERR(TAG, "Failed to create named pipe '%s': %s [%d]", pipe,
		         winpr_strerror(errno, ebuffer, sizeof(ebuffer)), errno);
		return 0;
	}

	void* ctx = WINPR_CAST_CONST_PTR_AWAY(pipe, void*);
	freerdp_add_signal_cleanup_handler(ctx, cleanup_pipe);

	xf_process_pipe(context, pipe);

	freerdp_del_signal_cleanup_handler(ctx, cleanup_pipe);

	unlink(pipe);
	return 0;
}

/**
 * Callback given to freerdp_connect() to perform post-connection operations.
 * It will be called only if the connection was initialized properly, and will continue the
 * initialization based on the newly created connection.
 */
static BOOL xf_post_connect(freerdp* instance)
{
	ResizeWindowEventArgs e = { 0 };

	WINPR_ASSERT(instance);
	xfContext* xfc = (xfContext*)instance->context;
	rdpContext* context = instance->context;
	WINPR_ASSERT(context);

	rdpSettings* settings = context->settings;
	WINPR_ASSERT(settings);

	rdpUpdate* update = context->update;
	WINPR_ASSERT(update);

	if (freerdp_settings_get_bool(settings, FreeRDP_RemoteApplicationMode))
		xfc->remote_app = TRUE;

	if (!xf_create_window(xfc))
		return FALSE;

	if (!xf_get_pixmap_info(xfc))
		return FALSE;

	if (!gdi_init(instance, xf_get_local_color_format(xfc, TRUE)))
		return FALSE;

	if (!xf_create_image(xfc))
		return FALSE;

	if (!xf_register_pointer(context->graphics))
		return FALSE;

#ifdef WITH_XRENDER
	xfc->scaledWidth =
	    WINPR_ASSERTING_INT_CAST(int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth));
	xfc->scaledHeight =
	    WINPR_ASSERTING_INT_CAST(int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight));
	xfc->offset_x = 0;
	xfc->offset_y = 0;
#endif

	if (!xfc->xrenderAvailable)
	{
		if (freerdp_settings_get_bool(settings, FreeRDP_SmartSizing))
		{
			WLog_ERR(TAG, "XRender not available: disabling smart-sizing");
			if (!freerdp_settings_set_bool(settings, FreeRDP_SmartSizing, FALSE))
				return FALSE;
		}

		if (freerdp_settings_get_bool(settings, FreeRDP_MultiTouchGestures))
		{
			WLog_ERR(TAG, "XRender not available: disabling local multi-touch gestures");
			if (!freerdp_settings_set_bool(settings, FreeRDP_MultiTouchGestures, FALSE))
				return FALSE;
		}
	}

	update->DesktopResize = xf_sw_desktop_resize;
	update->EndPaint = xf_end_paint;
	update->PlaySound = xf_play_sound;
	update->SetKeyboardIndicators = xf_keyboard_set_indicators;
	update->SetKeyboardImeStatus = xf_keyboard_set_ime_status;

	const BOOL serverIsWindowsPlatform =
	    (freerdp_settings_get_uint32(settings, FreeRDP_OsMajorType) == OSMAJORTYPE_WINDOWS);
	if (freerdp_settings_get_bool(settings, FreeRDP_RedirectClipboard) &&
	    !(xfc->clipboard = xf_clipboard_new(xfc, !serverIsWindowsPlatform)))
		return FALSE;

	if (!(xfc->xfDisp = xf_disp_new(xfc)))
		return FALSE;

	const char* pipe = freerdp_settings_get_string(settings, FreeRDP_KeyboardPipeName);
	if (pipe)
	{
		xfc->pipethread = CreateThread(NULL, 0, xf_handle_pipe, xfc, 0, NULL);
		if (!xfc->pipethread)
			return FALSE;
	}

	EventArgsInit(&e, "xfreerdp");
	e.width =
	    WINPR_ASSERTING_INT_CAST(int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth));
	e.height =
	    WINPR_ASSERTING_INT_CAST(int, freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight));
	PubSub_OnResizeWindow(context->pubSub, xfc, &e);
	return TRUE;
}

static void xf_post_disconnect(freerdp* instance)
{
	xfContext* xfc = NULL;
	rdpContext* context = NULL;

	if (!instance || !instance->context)
		return;

	context = instance->context;
	xfc = (xfContext*)context;
	PubSub_UnsubscribeChannelConnected(instance->context->pubSub,
	                                   xf_OnChannelConnectedEventHandler);
	PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub,
	                                      xf_OnChannelDisconnectedEventHandler);
	gdi_free(instance);

	if (xfc->pipethread)
	{
		(void)WaitForSingleObject(xfc->pipethread, INFINITE);
		(void)CloseHandle(xfc->pipethread);
		xfc->pipethread = NULL;
	}
	if (xfc->clipboard)
	{
		xf_clipboard_free(xfc->clipboard);
		xfc->clipboard = NULL;
	}

	if (xfc->xfDisp)
	{
		xf_disp_free(xfc->xfDisp);
		xfc->xfDisp = NULL;
	}

	if ((xfc->window != NULL) && (xfc->drawable == xfc->window->handle))
		xfc->drawable = 0;
	else
		xf_DestroyDummyWindow(xfc, xfc->drawable);

	freerdp_keyboard_remap_free(xfc->remap_table);
	xfc->remap_table = NULL;

	xf_window_free(xfc);
}

static void xf_post_final_disconnect(freerdp* instance)
{
	xfContext* xfc = NULL;
	rdpContext* context = NULL;

	if (!instance || !instance->context)
		return;

	context = instance->context;
	xfc = (xfContext*)context;

	xf_keyboard_free(xfc);
	xf_teardown_x11(xfc);
}

static int xf_logon_error_info(freerdp* instance, UINT32 data, UINT32 type)
{
	xfContext* xfc = (xfContext*)instance->context;
	const char* str_data = freerdp_get_logon_error_info_data(data);
	const char* str_type = freerdp_get_logon_error_info_type(type);
	WLog_INFO(TAG, "Logon Error Info %s [%s]", str_data, str_type);
	if (type != LOGON_MSG_SESSION_CONTINUE)
	{
		xf_rail_disable_remoteapp_mode(xfc);
	}
	return 1;
}

static BOOL handle_window_events(freerdp* instance)
{
	if (!xf_process_x_events(instance))
	{
		WLog_DBG(TAG, "Closed from X11");
		return FALSE;
	}

	return TRUE;
}

/** Main loop for the rdp connection.
 *  It will be run from the thread's entry point (thread_func()).
 *  It initiates the connection, and will continue to run until the session ends,
 *  processing events as they are received.
 *
 *  @param param - pointer to the rdp_freerdp structure that contains the session's settings
 *  @return A code from the enum XF_EXIT_CODE (0 if successful)
 */
static DWORD WINAPI xf_client_thread(LPVOID param)
{
	DWORD exit_code = 0;
	DWORD waitStatus = 0;
	HANDLE inputEvent = NULL;

	freerdp* instance = (freerdp*)param;
	WINPR_ASSERT(instance);

	const BOOL status = freerdp_connect(instance);
	rdpContext* context = instance->context;
	WINPR_ASSERT(context);
	xfContext* xfc = (xfContext*)instance->context;
	WINPR_ASSERT(xfc);

	rdpSettings* settings = context->settings;
	WINPR_ASSERT(settings);

	if (!status)
	{
		UINT32 error = freerdp_get_last_error(instance->context);
		exit_code = (uint32_t)xf_map_error_to_exit_code(error);
	}
	else
		exit_code = XF_EXIT_SUCCESS;

	if (!status)
		goto end;

	/* --authonly ? */
	if (freerdp_settings_get_bool(settings, FreeRDP_AuthenticationOnly))
	{
		WLog_ERR(TAG, "Authentication only, exit status %" PRId32 "", !status);
		goto disconnect;
	}

	if (!status)
	{
		WLog_ERR(TAG, "Freerdp connect error exit status %" PRId32 "", !status);
		exit_code = freerdp_error_info(instance);

		if (freerdp_get_last_error(instance->context) == FREERDP_ERROR_AUTHENTICATION_FAILED)
			exit_code = XF_EXIT_AUTH_FAILURE;
		else if (exit_code == ERRINFO_SUCCESS)
			exit_code = XF_EXIT_CONN_FAILED;

		goto disconnect;
	}

	inputEvent = xfc->x11event;

	while (!freerdp_shall_disconnect_context(instance->context))
	{
		HANDLE handles[MAXIMUM_WAIT_OBJECTS] = { 0 };
		DWORD nCount = 0;
		handles[nCount++] = inputEvent;

		/*
		 * win8 and server 2k12 seem to have some timing issue/race condition
		 * when a initial sync request is send to sync the keyboard indicators
		 * sending the sync event twice fixed this problem
		 */
		if (freerdp_focus_required(instance))
		{
			xf_keyboard_focus_in(xfc);
			xf_keyboard_focus_in(xfc);
		}

		{
			DWORD tmp =
			    freerdp_get_event_handles(context, &handles[nCount], ARRAYSIZE(handles) - nCount);

			if (tmp == 0)
			{
				WLog_ERR(TAG, "freerdp_get_event_handles failed");
				break;
			}

			nCount += tmp;
		}

		if (xfc->window)
			xf_floatbar_hide_and_show(xfc->window->floatbar);

		waitStatus = WaitForMultipleObjects(nCount, handles, FALSE, INFINITE);

		if (waitStatus == WAIT_FAILED)
			break;

		{
			if (!freerdp_check_event_handles(context))
			{
				if (client_auto_reconnect_ex(instance, handle_window_events))
					continue;
				else
				{
					/*
					 * Indicate an unsuccessful connection attempt if reconnect
					 * did not succeed and no other error was specified.
					 */
					const UINT32 error = freerdp_get_last_error(instance->context);

					if (freerdp_error_info(instance) == 0)
						exit_code = (uint32_t)xf_map_error_to_exit_code(error);
				}

				if (freerdp_get_last_error(context) == FREERDP_ERROR_SUCCESS)
					WLog_ERR(TAG, "Failed to check FreeRDP file descriptor");

				break;
			}
		}

		if (!handle_window_events(instance))
			break;
	}

	if (!exit_code)
	{
		exit_code = freerdp_error_info(instance);

		if (exit_code == XF_EXIT_DISCONNECT &&
		    freerdp_get_disconnect_ultimatum(context) == Disconnect_Ultimatum_user_requested)
		{
			/* This situation might be limited to Windows XP. */
			WLog_INFO(TAG, "Error info says user did not initiate but disconnect ultimatum says "
			               "they did; treat this as a user logoff");
			exit_code = XF_EXIT_LOGOFF;
		}
	}

disconnect:

	freerdp_disconnect(instance);
end:
	ExitThread(exit_code);
	return exit_code;
}

int xf_exit_code_from_disconnect_reason(DWORD reason)
{
	if (reason == 0 ||
	    (reason >= XF_EXIT_PARSE_ARGUMENTS && reason <= XF_EXIT_CONNECT_NO_OR_MISSING_CREDENTIALS))
		return WINPR_ASSERTING_INT_CAST(int, reason);
	/* License error set */
	else if (reason >= 0x100 && reason <= 0x10A)
		reason -= 0x100 + XF_EXIT_LICENSE_INTERNAL;
	/* RDP protocol error set */
	else if (reason >= 0x10c9 && reason <= 0x1193)
		reason = XF_EXIT_RDP;
	/* There's no need to test protocol-independent codes: they match */
	else if (!(reason <= 0xC))
		reason = XF_EXIT_UNKNOWN;

	return WINPR_ASSERTING_INT_CAST(int, reason);
}

static void xf_TerminateEventHandler(void* context, const TerminateEventArgs* e)
{
	rdpContext* ctx = (rdpContext*)context;
	WINPR_UNUSED(e);
	freerdp_abort_connect_context(ctx);
}

#ifdef WITH_XRENDER
static void xf_ZoomingChangeEventHandler(void* context, const ZoomingChangeEventArgs* e)
{
	int w = 0;
	int h = 0;
	rdpSettings* settings = NULL;
	xfContext* xfc = (xfContext*)context;

	WINPR_ASSERT(xfc);

	settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	w = xfc->scaledWidth + e->dx;
	h = xfc->scaledHeight + e->dy;

	if (e->dx == 0 && e->dy == 0)
		return;

	if (w < 10)
		w = 10;

	if (h < 10)
		h = 10;

	if (w == xfc->scaledWidth && h == xfc->scaledHeight)
		return;

	xfc->scaledWidth = w;
	xfc->scaledHeight = h;
	xf_draw_screen(xfc, 0, 0,
	               WINPR_ASSERTING_INT_CAST(
	                   int32_t, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth)),
	               WINPR_ASSERTING_INT_CAST(
	                   int32_t, freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight)));
}

static void xf_PanningChangeEventHandler(void* context, const PanningChangeEventArgs* e)
{
	xfContext* xfc = (xfContext*)context;
	rdpSettings* settings = NULL;

	WINPR_ASSERT(xfc);
	WINPR_ASSERT(e);

	settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	if (e->dx == 0 && e->dy == 0)
		return;

	xfc->offset_x += e->dx;
	xfc->offset_y += e->dy;
	xf_draw_screen(xfc, 0, 0,
	               WINPR_ASSERTING_INT_CAST(
	                   int32_t, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth)),
	               WINPR_ASSERTING_INT_CAST(
	                   int32_t, freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight)));
}
#endif

/**
 * Client Interface
 */

static BOOL xfreerdp_client_global_init(void)
{
	// NOLINTNEXTLINE(concurrency-mt-unsafe)
	(void)setlocale(LC_ALL, "");

	if (freerdp_handle_signals() != 0)
		return FALSE;

	return TRUE;
}

static void xfreerdp_client_global_uninit(void)
{
}

static int xfreerdp_client_start(rdpContext* context)
{
	xfContext* xfc = (xfContext*)context;
	rdpSettings* settings = context->settings;

	if (!freerdp_settings_get_string(settings, FreeRDP_ServerHostname))
	{
		WLog_ERR(TAG, "error: server hostname was not specified with /v:<server>[:port]");
		return -1;
	}

	if (!(xfc->common.thread = CreateThread(NULL, 0, xf_client_thread, context->instance, 0, NULL)))
	{
		WLog_ERR(TAG, "failed to create client thread");
		return -1;
	}

	return 0;
}

static Atom get_supported_atom(xfContext* xfc, const char* atomName)
{
	const Atom atom = Logging_XInternAtom(xfc->log, xfc->display, atomName, False);

	for (unsigned long i = 0; i < xfc->supportedAtomCount; i++)
	{
		if (xfc->supportedAtoms[i] == atom)
			return atom;
	}

	return None;
}

void xf_teardown_x11(xfContext* xfc)
{
	WINPR_ASSERT(xfc);

	if (xfc->display)
	{
		XCloseDisplay(xfc->display);
		xfc->display = NULL;
	}

	if (xfc->x11event)
	{
		(void)CloseHandle(xfc->x11event);
		xfc->x11event = NULL;
	}

	if (xfc->mutex)
	{
		(void)CloseHandle(xfc->mutex);
		xfc->mutex = NULL;
	}

	if (xfc->vscreen.monitors)
	{
		free(xfc->vscreen.monitors);
		xfc->vscreen.monitors = NULL;
	}
	xfc->vscreen.nmonitors = 0;

	free(xfc->supportedAtoms);
	xfc->supportedAtoms = NULL;
	xfc->supportedAtomCount = 0;
}

BOOL xf_setup_x11(xfContext* xfc)
{

	WINPR_ASSERT(xfc);
	xfc->UseXThreads = TRUE;

#if !defined(NDEBUG)
	/* uncomment below if debugging to prevent keyboard grab */
	xfc->debug = TRUE;
#endif

	if (xfc->UseXThreads)
	{
		if (!XInitThreads())
		{
			WLog_WARN(TAG, "XInitThreads() failure");
			xfc->UseXThreads = FALSE;
		}
	}

	xfc->display = XOpenDisplay(NULL);

	if (!xfc->display)
	{
		WLog_ERR(TAG, "failed to open display: %s", XDisplayName(NULL));
		WLog_ERR(TAG, "Please check that the $DISPLAY environment variable is properly set.");
		goto fail;
	}
	if (xfc->debug)
	{
		WLog_INFO(TAG, "Enabling X11 debug mode.");
		XSynchronize(xfc->display, TRUE);
	}
	def_error_handler = XSetErrorHandler(xf_error_handler_ex);

	xfc->mutex = CreateMutex(NULL, FALSE, NULL);

	if (!xfc->mutex)
	{
		WLog_ERR(TAG, "Could not create mutex!");
		goto fail;
	}

	xfc->xfds = ConnectionNumber(xfc->display);
	xfc->screen_number = DefaultScreen(xfc->display);
	xfc->screen = ScreenOfDisplay(xfc->display, xfc->screen_number);
	xfc->big_endian = (ImageByteOrder(xfc->display) == MSBFirst);
	xfc->invert = TRUE;
	xfc->complex_regions = TRUE;
	xfc->NET_SUPPORTED = Logging_XInternAtom(xfc->log, xfc->display, "_NET_SUPPORTED", True);
	xfc->NET_SUPPORTING_WM_CHECK =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_SUPPORTING_WM_CHECK", True);

	if ((xfc->NET_SUPPORTED != None) && (xfc->NET_SUPPORTING_WM_CHECK != None))
	{
		Atom actual_type = 0;
		int actual_format = 0;
		unsigned long nitems = 0;
		unsigned long after = 0;
		unsigned char* data = NULL;
		int status = LogTagAndXGetWindowProperty(
		    TAG, xfc->display, RootWindowOfScreen(xfc->screen), xfc->NET_SUPPORTED, 0, 1024, False,
		    XA_ATOM, &actual_type, &actual_format, &nitems, &after, &data);

		if ((status == Success) && (actual_type == XA_ATOM) && (actual_format == 32))
		{
			xfc->supportedAtomCount = nitems;
			xfc->supportedAtoms = calloc(xfc->supportedAtomCount, sizeof(Atom));
			WINPR_ASSERT(xfc->supportedAtoms);
			memcpy(xfc->supportedAtoms, data, nitems * sizeof(Atom));
		}

		if (data)
			XFree(data);
	}

	xfc->XWAYLAND_MAY_GRAB_KEYBOARD =
	    Logging_XInternAtom(xfc->log, xfc->display, "_XWAYLAND_MAY_GRAB_KEYBOARD", False);
	xfc->NET_WM_ICON = Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ICON", False);
	xfc->MOTIF_WM_HINTS = Logging_XInternAtom(xfc->log, xfc->display, "_MOTIF_WM_HINTS", False);
	xfc->NET_NUMBER_OF_DESKTOPS =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_NUMBER_OF_DESKTOPS", False);
	xfc->NET_CURRENT_DESKTOP =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_CURRENT_DESKTOP", False);
	xfc->NET_WORKAREA = Logging_XInternAtom(xfc->log, xfc->display, "_NET_WORKAREA", False);
	xfc->NET_WM_STATE = get_supported_atom(xfc, "_NET_WM_STATE");
	xfc->NET_WM_STATE_MODAL = get_supported_atom(xfc, "_NET_WM_STATE_MODAL");
	xfc->NET_WM_STATE_STICKY = get_supported_atom(xfc, "_NET_WM_STATE_STICKY");
	xfc->NET_WM_STATE_MAXIMIZED_HORZ =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	xfc->NET_WM_STATE_MAXIMIZED_VERT =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	xfc->NET_WM_STATE_SHADED = get_supported_atom(xfc, "_NET_WM_STATE_SHADED");
	xfc->NET_WM_STATE_SKIP_TASKBAR = get_supported_atom(xfc, "_NET_WM_STATE_SKIP_TASKBAR");
	xfc->NET_WM_STATE_SKIP_PAGER = get_supported_atom(xfc, "_NET_WM_STATE_SKIP_PAGER");
	xfc->NET_WM_STATE_HIDDEN = get_supported_atom(xfc, "_NET_WM_STATE_HIDDEN");
	xfc->NET_WM_STATE_FULLSCREEN = get_supported_atom(xfc, "_NET_WM_STATE_FULLSCREEN");
	xfc->NET_WM_STATE_ABOVE = get_supported_atom(xfc, "_NET_WM_STATE_ABOVE");
	xfc->NET_WM_STATE_BELOW = get_supported_atom(xfc, "_NET_WM_STATE_BELOW");
	xfc->NET_WM_STATE_DEMANDS_ATTENTION =
	    get_supported_atom(xfc, "_NET_WM_STATE_DEMANDS_ATTENTION");
	xfc->NET_WM_FULLSCREEN_MONITORS = get_supported_atom(xfc, "_NET_WM_FULLSCREEN_MONITORS");
	xfc->NET_WM_NAME = Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_NAME", False);
	xfc->NET_WM_PID = Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_PID", False);
	xfc->NET_WM_WINDOW_TYPE =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_WINDOW_TYPE", False);
	xfc->NET_WM_WINDOW_TYPE_NORMAL =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
	xfc->NET_WM_WINDOW_TYPE_DIALOG =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	xfc->NET_WM_WINDOW_TYPE_POPUP =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_WINDOW_TYPE_POPUP", False);
	xfc->NET_WM_WINDOW_TYPE_POPUP_MENU =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
	xfc->NET_WM_WINDOW_TYPE_UTILITY =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
	xfc->NET_WM_WINDOW_TYPE_DROPDOWN_MENU =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
	xfc->NET_WM_STATE_SKIP_TASKBAR =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_STATE_SKIP_TASKBAR", False);
	xfc->NET_WM_STATE_SKIP_PAGER =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_STATE_SKIP_PAGER", False);
	xfc->NET_WM_MOVERESIZE =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_MOVERESIZE", False);
	xfc->NET_MOVERESIZE_WINDOW =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_MOVERESIZE_WINDOW", False);
	xfc->UTF8_STRING = Logging_XInternAtom(xfc->log, xfc->display, "UTF8_STRING", FALSE);
	xfc->WM_PROTOCOLS = Logging_XInternAtom(xfc->log, xfc->display, "WM_PROTOCOLS", False);
	xfc->WM_DELETE_WINDOW = Logging_XInternAtom(xfc->log, xfc->display, "WM_DELETE_WINDOW", False);
	xfc->WM_STATE = Logging_XInternAtom(xfc->log, xfc->display, "WM_STATE", False);
	xfc->x11event = CreateFileDescriptorEvent(NULL, FALSE, FALSE, xfc->xfds, WINPR_FD_READ);

	xfc->NET_WM_ALLOWED_ACTIONS =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ALLOWED_ACTIONS", False);

	xfc->NET_WM_ACTION_CLOSE =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_CLOSE", False);
	xfc->NET_WM_ACTION_MINIMIZE =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_MINIMIZE", False);
	xfc->NET_WM_ACTION_MOVE =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_MOVE", False);
	xfc->NET_WM_ACTION_RESIZE =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_RESIZE", False);
	xfc->NET_WM_ACTION_MAXIMIZE_HORZ =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_MAXIMIZE_HORZ", False);
	xfc->NET_WM_ACTION_MAXIMIZE_VERT =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_MAXIMIZE_VERT", False);
	xfc->NET_WM_ACTION_FULLSCREEN =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_FULLSCREEN", False);
	xfc->NET_WM_ACTION_CHANGE_DESKTOP =
	    Logging_XInternAtom(xfc->log, xfc->display, "_NET_WM_ACTION_CHANGE_DESKTOP", False);

	if (!xfc->x11event)
	{
		WLog_ERR(TAG, "Could not create xfds event");
		goto fail;
	}

	xf_check_extensions(xfc);

	xfc->vscreen.monitors = calloc(16, sizeof(MONITOR_INFO));

	if (!xfc->vscreen.monitors)
		goto fail;
	return TRUE;

fail:
	xf_teardown_x11(xfc);
	return FALSE;
}

static BOOL xfreerdp_client_new(freerdp* instance, rdpContext* context)
{
	xfContext* xfc = (xfContext*)instance->context;
	WINPR_ASSERT(context);
	WINPR_ASSERT(xfc);
	WINPR_ASSERT(!xfc->display);
	WINPR_ASSERT(!xfc->mutex);
	WINPR_ASSERT(!xfc->x11event);
	instance->PreConnect = xf_pre_connect;
	instance->PostConnect = xf_post_connect;
	instance->PostDisconnect = xf_post_disconnect;
	instance->PostFinalDisconnect = xf_post_final_disconnect;
	instance->LogonErrorInfo = xf_logon_error_info;
	instance->GetAccessToken = client_cli_get_access_token;
	PubSub_SubscribeTerminate(context->pubSub, xf_TerminateEventHandler);
#ifdef WITH_XRENDER
	PubSub_SubscribeZoomingChange(context->pubSub, xf_ZoomingChangeEventHandler);
	PubSub_SubscribePanningChange(context->pubSub, xf_PanningChangeEventHandler);
#endif
	xfc->log = WLog_Get(TAG);

	return TRUE;
}

static void xfreerdp_client_free(WINPR_ATTR_UNUSED freerdp* instance, rdpContext* context)
{
	if (!context)
		return;

	if (context->pubSub)
	{
		PubSub_UnsubscribeTerminate(context->pubSub, xf_TerminateEventHandler);
#ifdef WITH_XRENDER
		PubSub_UnsubscribeZoomingChange(context->pubSub, xf_ZoomingChangeEventHandler);
		PubSub_UnsubscribePanningChange(context->pubSub, xf_PanningChangeEventHandler);
#endif
	}
}

int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints)
{
	pEntryPoints->Version = 1;
	pEntryPoints->Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);
	pEntryPoints->GlobalInit = xfreerdp_client_global_init;
	pEntryPoints->GlobalUninit = xfreerdp_client_global_uninit;
	pEntryPoints->ContextSize = sizeof(xfContext);
	pEntryPoints->ClientNew = xfreerdp_client_new;
	pEntryPoints->ClientFree = xfreerdp_client_free;
	pEntryPoints->ClientStart = xfreerdp_client_start;
	pEntryPoints->ClientStop = freerdp_client_common_stop;
	return 0;
}
