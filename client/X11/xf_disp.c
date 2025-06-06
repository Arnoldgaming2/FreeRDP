/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * X11 Display Control channel
 *
 * Copyright 2017 David Fort <contact@hardening-consulting.com>
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

#include <math.h>
#include <winpr/assert.h>
#include <winpr/sysinfo.h>

#include <freerdp/timer.h>

#include <X11/Xutil.h>

#ifdef WITH_XRANDR
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>

#if (RANDR_MAJOR * 100 + RANDR_MINOR) >= 105
#define USABLE_XRANDR
#endif

#endif

#include "xf_disp.h"
#include "xf_monitor.h"

#include <freerdp/log.h>
#define TAG CLIENT_TAG("x11disp")
#define RESIZE_MIN_DELAY_NS 200000UL /* minimum delay in ms between two resizes */

struct s_xfDispContext
{
	xfContext* xfc;
	DispClientContext* disp;
	BOOL haveXRandr;
	int eventBase;
	int errorBase;
	UINT32 lastSentWidth;
	UINT32 lastSentHeight;
	BYTE reserved[4];
	UINT64 lastSentDate;
	UINT32 targetWidth;
	UINT32 targetHeight;
	BOOL activated;
	BOOL fullscreen;
	UINT16 lastSentDesktopOrientation;
	BYTE reserved2[2];
	UINT32 lastSentDesktopScaleFactor;
	UINT32 lastSentDeviceScaleFactor;
	BYTE reserved3[4];
	FreeRDP_TimerID timerID;
};

static BOOL xf_disp_check_context(void* context, xfContext** ppXfc, xfDispContext** ppXfDisp,
                                  rdpSettings** ppSettings);
static BOOL xf_disp_sendResize(xfDispContext* xfDisp, BOOL fromTimer);
static UINT xf_disp_sendLayout(DispClientContext* disp, const rdpMonitor* monitors,
                               UINT32 nmonitors);

static BOOL xf_disp_settings_changed(xfDispContext* xfDisp)
{
	rdpSettings* settings = NULL;

	WINPR_ASSERT(xfDisp);
	WINPR_ASSERT(xfDisp->xfc);

	settings = xfDisp->xfc->common.context.settings;
	WINPR_ASSERT(settings);

	if (xfDisp->lastSentWidth != xfDisp->targetWidth)
		return TRUE;

	if (xfDisp->lastSentHeight != xfDisp->targetHeight)
		return TRUE;

	if (xfDisp->lastSentDesktopOrientation !=
	    freerdp_settings_get_uint16(settings, FreeRDP_DesktopOrientation))
		return TRUE;

	if (xfDisp->lastSentDesktopScaleFactor !=
	    freerdp_settings_get_uint32(settings, FreeRDP_DesktopScaleFactor))
		return TRUE;

	if (xfDisp->lastSentDeviceScaleFactor !=
	    freerdp_settings_get_uint32(settings, FreeRDP_DeviceScaleFactor))
		return TRUE;

	if (xfDisp->fullscreen != xfDisp->xfc->fullscreen)
		return TRUE;

	return FALSE;
}

static BOOL xf_update_last_sent(xfDispContext* xfDisp)
{
	rdpSettings* settings = NULL;

	WINPR_ASSERT(xfDisp);
	WINPR_ASSERT(xfDisp->xfc);

	settings = xfDisp->xfc->common.context.settings;
	WINPR_ASSERT(settings);

	xfDisp->lastSentWidth = xfDisp->targetWidth;
	xfDisp->lastSentHeight = xfDisp->targetHeight;
	xfDisp->lastSentDesktopOrientation =
	    freerdp_settings_get_uint16(settings, FreeRDP_DesktopOrientation);
	xfDisp->lastSentDesktopScaleFactor =
	    freerdp_settings_get_uint32(settings, FreeRDP_DesktopScaleFactor);
	xfDisp->lastSentDeviceScaleFactor =
	    freerdp_settings_get_uint32(settings, FreeRDP_DeviceScaleFactor);
	xfDisp->fullscreen = xfDisp->xfc->fullscreen;
	return TRUE;
}

static uint64_t xf_disp_OnTimer(rdpContext* context, WINPR_ATTR_UNUSED void* userdata,
                                WINPR_ATTR_UNUSED FreeRDP_TimerID timerID,
                                WINPR_ATTR_UNUSED uint64_t timestamp,
                                WINPR_ATTR_UNUSED uint64_t interval)

{
	xfContext* xfc = NULL;
	xfDispContext* xfDisp = NULL;
	rdpSettings* settings = NULL;

	if (!xf_disp_check_context(context, &xfc, &xfDisp, &settings))
		return interval;

	if (!xfDisp->activated)
		return interval;

	xf_disp_sendResize(xfDisp, TRUE);
	xfDisp->timerID = 0;
	return 0;
}

static BOOL update_timer(xfDispContext* xfDisp, uint64_t intervalNS)
{
	WINPR_ASSERT(xfDisp);

	if (xfDisp->timerID == 0)
	{
		rdpContext* context = &xfDisp->xfc->common.context;

		xfDisp->timerID = freerdp_timer_add(context, intervalNS, xf_disp_OnTimer, NULL, true);
	}
	return TRUE;
}

BOOL xf_disp_sendResize(xfDispContext* xfDisp, BOOL fromTimer)
{
	DISPLAY_CONTROL_MONITOR_LAYOUT layout = { 0 };

	if (!xfDisp || !xfDisp->xfc)
		return FALSE;

	/* If there is already a timer running skip the update and wait for the timer to expire. */
	if ((xfDisp->timerID != 0) && !fromTimer)
		return TRUE;

	xfContext* xfc = xfDisp->xfc;
	rdpSettings* settings = xfc->common.context.settings;

	if (!settings)
		return FALSE;

	if (!xfDisp->activated || !xfDisp->disp)
		return update_timer(xfDisp, RESIZE_MIN_DELAY_NS);

	const uint64_t diff = winpr_GetTickCount64NS() - xfDisp->lastSentDate;
	if (diff < RESIZE_MIN_DELAY_NS)
	{
		const uint64_t interval = RESIZE_MIN_DELAY_NS - diff;
		return update_timer(xfDisp, interval);
	}

	if (!xf_disp_settings_changed(xfDisp))
		return TRUE;

	xfDisp->lastSentDate = winpr_GetTickCount64NS();

	const UINT32 mcount = freerdp_settings_get_uint32(settings, FreeRDP_MonitorCount);
	if (mcount > 1)
	{
		const rdpMonitor* monitors =
		    freerdp_settings_get_pointer(settings, FreeRDP_MonitorDefArray);
		if (xf_disp_sendLayout(xfDisp->disp, monitors, mcount) != CHANNEL_RC_OK)
			return FALSE;
	}
	else
	{
		layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
		layout.Top = layout.Left = 0;
		layout.Width = xfDisp->targetWidth;
		layout.Height = xfDisp->targetHeight;
		layout.Orientation = freerdp_settings_get_uint16(settings, FreeRDP_DesktopOrientation);
		layout.DesktopScaleFactor =
		    freerdp_settings_get_uint32(settings, FreeRDP_DesktopScaleFactor);
		layout.DeviceScaleFactor = freerdp_settings_get_uint32(settings, FreeRDP_DeviceScaleFactor);

		const double dw = xfDisp->targetWidth / 75.0 * 25.4;
		const double dh = xfDisp->targetHeight / 75.0 * 25.4;
		layout.PhysicalWidth = (UINT32)lround(dw);
		layout.PhysicalHeight = (UINT32)lround(dh);

		if (IFCALLRESULT(CHANNEL_RC_OK, xfDisp->disp->SendMonitorLayout, xfDisp->disp, 1,
		                 &layout) != CHANNEL_RC_OK)
			return FALSE;
	}

	return xf_update_last_sent(xfDisp);
}

static BOOL xf_disp_queueResize(xfDispContext* xfDisp, UINT32 width, UINT32 height)
{
	if ((xfDisp->targetWidth == (INT64)width) && (xfDisp->targetHeight == (INT64)height))
		return TRUE;
	xfDisp->targetWidth = width;
	xfDisp->targetHeight = height;
	return xf_disp_sendResize(xfDisp, FALSE);
}

static BOOL xf_disp_set_window_resizable(xfDispContext* xfDisp)
{
	XSizeHints* size_hints = NULL;

	if (!(size_hints = XAllocSizeHints()))
		return FALSE;

	size_hints->flags = PMinSize | PMaxSize | PWinGravity;
	size_hints->win_gravity = NorthWestGravity;
	size_hints->min_width = size_hints->min_height = 320;
	size_hints->max_width = size_hints->max_height = 8192;

	if (xfDisp->xfc->window)
		XSetWMNormalHints(xfDisp->xfc->display, xfDisp->xfc->window->handle, size_hints);

	XFree(size_hints);
	return TRUE;
}

BOOL xf_disp_check_context(void* context, xfContext** ppXfc, xfDispContext** ppXfDisp,
                           rdpSettings** ppSettings)
{
	xfContext* xfc = NULL;

	if (!context)
		return FALSE;

	xfc = (xfContext*)context;

	if (!(xfc->xfDisp))
		return FALSE;

	if (!xfc->common.context.settings)
		return FALSE;

	*ppXfc = xfc;
	*ppXfDisp = xfc->xfDisp;
	*ppSettings = xfc->common.context.settings;
	return TRUE;
}

static void xf_disp_OnActivated(void* context, const ActivatedEventArgs* e)
{
	xfContext* xfc = NULL;
	xfDispContext* xfDisp = NULL;
	rdpSettings* settings = NULL;

	if (!xf_disp_check_context(context, &xfc, &xfDisp, &settings))
		return;

	if (xfDisp->activated && !xfc->fullscreen)
	{
		xf_disp_set_window_resizable(xfDisp);

		if (e->firstActivation)
			return;

		xf_disp_sendResize(xfDisp, FALSE);
	}
}

static void xf_disp_OnGraphicsReset(void* context, const GraphicsResetEventArgs* e)
{
	xfContext* xfc = NULL;
	xfDispContext* xfDisp = NULL;
	rdpSettings* settings = NULL;

	WINPR_UNUSED(e);

	if (!xf_disp_check_context(context, &xfc, &xfDisp, &settings))
		return;

	if (xfDisp->activated && !freerdp_settings_get_bool(settings, FreeRDP_Fullscreen))
	{
		xf_disp_set_window_resizable(xfDisp);
		xf_disp_sendResize(xfDisp, FALSE);
	}
}

static void xf_disp_OnWindowStateChange(void* context, const WindowStateChangeEventArgs* e)
{
	xfContext* xfc = NULL;
	xfDispContext* xfDisp = NULL;
	rdpSettings* settings = NULL;

	WINPR_UNUSED(e);

	if (!xf_disp_check_context(context, &xfc, &xfDisp, &settings))
		return;

	if (!xfDisp->activated || !xfc->fullscreen)
		return;

	xf_disp_sendResize(xfDisp, FALSE);
}

xfDispContext* xf_disp_new(xfContext* xfc)
{
	xfDispContext* ret = NULL;
	const rdpSettings* settings = NULL;
	wPubSub* pubSub = NULL;

	WINPR_ASSERT(xfc);

	pubSub = xfc->common.context.pubSub;
	WINPR_ASSERT(pubSub);

	settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	ret = calloc(1, sizeof(xfDispContext));

	if (!ret)
		return NULL;

	ret->xfc = xfc;
#ifdef USABLE_XRANDR

	if (XRRQueryExtension(xfc->display, &ret->eventBase, &ret->errorBase))
	{
		ret->haveXRandr = TRUE;
	}

#endif
	ret->lastSentWidth = ret->targetWidth =
	    freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
	ret->lastSentHeight = ret->targetHeight =
	    freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
	PubSub_SubscribeActivated(pubSub, xf_disp_OnActivated);
	PubSub_SubscribeGraphicsReset(pubSub, xf_disp_OnGraphicsReset);
	PubSub_SubscribeWindowStateChange(pubSub, xf_disp_OnWindowStateChange);
	return ret;
}

void xf_disp_free(xfDispContext* disp)
{
	if (!disp)
		return;

	if (disp->xfc)
	{
		wPubSub* pubSub = disp->xfc->common.context.pubSub;
		PubSub_UnsubscribeActivated(pubSub, xf_disp_OnActivated);
		PubSub_UnsubscribeGraphicsReset(pubSub, xf_disp_OnGraphicsReset);
		PubSub_UnsubscribeWindowStateChange(pubSub, xf_disp_OnWindowStateChange);
	}

	free(disp);
}

UINT xf_disp_sendLayout(DispClientContext* disp, const rdpMonitor* monitors, UINT32 nmonitors)
{
	UINT ret = CHANNEL_RC_OK;
	xfDispContext* xfDisp = NULL;
	rdpSettings* settings = NULL;
	DISPLAY_CONTROL_MONITOR_LAYOUT* layouts = NULL;

	WINPR_ASSERT(disp);
	WINPR_ASSERT(monitors);
	WINPR_ASSERT(nmonitors > 0);

	xfDisp = (xfDispContext*)disp->custom;
	WINPR_ASSERT(xfDisp);
	WINPR_ASSERT(xfDisp->xfc);

	settings = xfDisp->xfc->common.context.settings;
	WINPR_ASSERT(settings);

	layouts = calloc(nmonitors, sizeof(DISPLAY_CONTROL_MONITOR_LAYOUT));

	if (!layouts)
		return CHANNEL_RC_NO_MEMORY;

	for (UINT32 i = 0; i < nmonitors; i++)
	{
		const rdpMonitor* monitor = &monitors[i];
		DISPLAY_CONTROL_MONITOR_LAYOUT* layout = &layouts[i];

		layout->Flags = (monitor->is_primary ? DISPLAY_CONTROL_MONITOR_PRIMARY : 0);
		layout->Left = monitor->x;
		layout->Top = monitor->y;
		layout->Width = WINPR_ASSERTING_INT_CAST(uint32_t, monitor->width);
		layout->Height = WINPR_ASSERTING_INT_CAST(uint32_t, monitor->height);
		layout->Orientation = ORIENTATION_LANDSCAPE;
		layout->PhysicalWidth = monitor->attributes.physicalWidth;
		layout->PhysicalHeight = monitor->attributes.physicalHeight;

		switch (monitor->attributes.orientation)
		{
			case 90:
				layout->Orientation = ORIENTATION_PORTRAIT;
				break;

			case 180:
				layout->Orientation = ORIENTATION_LANDSCAPE_FLIPPED;
				break;

			case 270:
				layout->Orientation = ORIENTATION_PORTRAIT_FLIPPED;
				break;

			case 0:
			default:
				/* MS-RDPEDISP - 2.2.2.2.1:
				 * Orientation (4 bytes): A 32-bit unsigned integer that specifies the
				 * orientation of the monitor in degrees. Valid values are 0, 90, 180
				 * or 270
				 *
				 * So we default to ORIENTATION_LANDSCAPE
				 */
				layout->Orientation = ORIENTATION_LANDSCAPE;
				break;
		}

		layout->DesktopScaleFactor =
		    freerdp_settings_get_uint32(settings, FreeRDP_DesktopScaleFactor);
		layout->DeviceScaleFactor =
		    freerdp_settings_get_uint32(settings, FreeRDP_DeviceScaleFactor);
	}

	ret = IFCALLRESULT(CHANNEL_RC_OK, disp->SendMonitorLayout, disp, nmonitors, layouts);
	free(layouts);
	return ret;
}

BOOL xf_disp_handle_xevent(xfContext* xfc, const XEvent* event)
{
	xfDispContext* xfDisp = NULL;
	rdpSettings* settings = NULL;
	UINT32 maxWidth = 0;
	UINT32 maxHeight = 0;

	if (!xfc || !event)
		return FALSE;

	xfDisp = xfc->xfDisp;

	if (!xfDisp)
		return FALSE;

	settings = xfc->common.context.settings;

	if (!settings)
		return FALSE;

	if (!xfDisp->haveXRandr || !xfDisp->disp)
		return TRUE;

#ifdef USABLE_XRANDR

	if (event->type != xfDisp->eventBase + RRScreenChangeNotify)
		return TRUE;

#endif
	xf_detect_monitors(xfc, &maxWidth, &maxHeight);
	const rdpMonitor* monitors = freerdp_settings_get_pointer(settings, FreeRDP_MonitorDefArray);
	const UINT32 mcount = freerdp_settings_get_uint32(settings, FreeRDP_MonitorCount);
	return xf_disp_sendLayout(xfDisp->disp, monitors, mcount) == CHANNEL_RC_OK;
}

BOOL xf_disp_handle_configureNotify(xfContext* xfc, int width, int height)
{
	xfDispContext* xfDisp = NULL;

	if (!xfc)
		return FALSE;

	xfDisp = xfc->xfDisp;

	if (!xfDisp)
		return FALSE;

	return xf_disp_queueResize(xfDisp, WINPR_ASSERTING_INT_CAST(uint32_t, width),
	                           WINPR_ASSERTING_INT_CAST(uint32_t, height));
}

static UINT xf_DisplayControlCaps(DispClientContext* disp, UINT32 maxNumMonitors,
                                  UINT32 maxMonitorAreaFactorA, UINT32 maxMonitorAreaFactorB)
{
	/* we're called only if dynamic resolution update is activated */
	xfDispContext* xfDisp = NULL;
	rdpSettings* settings = NULL;

	WINPR_ASSERT(disp);

	xfDisp = (xfDispContext*)disp->custom;
	WINPR_ASSERT(xfDisp);
	WINPR_ASSERT(xfDisp->xfc);

	settings = xfDisp->xfc->common.context.settings;
	WINPR_ASSERT(settings);

	WLog_DBG(TAG,
	         "DisplayControlCapsPdu: MaxNumMonitors: %" PRIu32 " MaxMonitorAreaFactorA: %" PRIu32
	         " MaxMonitorAreaFactorB: %" PRIu32 "",
	         maxNumMonitors, maxMonitorAreaFactorA, maxMonitorAreaFactorB);
	xfDisp->activated = TRUE;

	if (freerdp_settings_get_bool(settings, FreeRDP_Fullscreen))
		return CHANNEL_RC_OK;

	WLog_DBG(TAG, "DisplayControlCapsPdu: setting the window as resizable");
	return xf_disp_set_window_resizable(xfDisp) ? CHANNEL_RC_OK : CHANNEL_RC_NO_MEMORY;
}

BOOL xf_disp_init(xfDispContext* xfDisp, DispClientContext* disp)
{
	rdpSettings* settings = NULL;

	if (!xfDisp || !xfDisp->xfc || !disp)
		return FALSE;

	settings = xfDisp->xfc->common.context.settings;

	if (!settings)
		return FALSE;

	xfDisp->disp = disp;
	disp->custom = (void*)xfDisp;

	if (freerdp_settings_get_bool(settings, FreeRDP_DynamicResolutionUpdate))
	{
		disp->DisplayControlCaps = xf_DisplayControlCaps;
#ifdef USABLE_XRANDR

		if (freerdp_settings_get_bool(settings, FreeRDP_Fullscreen))
		{
			/* ask X11 to notify us of screen changes */
			XRRSelectInput(xfDisp->xfc->display, DefaultRootWindow(xfDisp->xfc->display),
			               RRScreenChangeNotifyMask);
		}

#endif
	}

	return TRUE;
}

BOOL xf_disp_uninit(xfDispContext* xfDisp, DispClientContext* disp)
{
	if (!xfDisp || !disp)
		return FALSE;

	xfDisp->disp = NULL;
	return TRUE;
}
