/* Copyright (C) 2013 Mamadou DIOP
* Copyright (C) 2013 Doubango Telecom <http://www.doubango.org>
*	
* This file is part of Open Source Doubango Framework.
*
* DOUBANGO is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*	
* DOUBANGO is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*	
* You should have received a copy of the GNU General Public License
* along with DOUBANGO.
*/
#include "plugin_win_mf_config.h"
#include "internals/mf_utils.h"

#include "tinymedia/tmedia_consumer.h"

#include "tsk_safeobj.h"
#include "tsk_string.h"
#include "tsk_thread.h"
#include "tsk_debug.h"

#include <initguid.h>
#include <assert.h>

// Whether to use Direct3D device for direct rendering or Media Foundation topology and custom source
// Using Media Foundation (MF) introduce delay when the input fps is different than the one in the custom src.
// It's very hard to have someting accurate when using MF because the input FPS change depending on the congestion control. D3D is the best choice as frames are displayed as they arrive
#if !defined(PLUGIN_MF_CV_USE_D3D9)
#	define PLUGIN_MF_CV_USE_D3D9	 1
#endif

/******* ********/

#if PLUGIN_MF_CV_USE_D3D9

#include <d3d9.h>
#include <dxva2api.h>

#ifdef _MSC_VER
#pragma comment(lib, "d3d9")
#endif

const DWORD NUM_BACK_BUFFERS = 2;

static HRESULT CreateDeviceD3D9(
	HWND hWnd,
	IDirect3DDevice9** ppDevice, 
	IDirect3D9 **ppD3D,
	D3DPRESENT_PARAMETERS &d3dpp
	);
static HRESULT TestCooperativeLevel(
	struct plugin_win_mf_consumer_video_s *pSelf
	);
static HRESULT CreateSwapChain(
	HWND hWnd, 
	UINT32 nFrameWidth, 
	UINT32 nFrameHeight, 
	IDirect3DDevice9* pDevice, 
	IDirect3DSwapChain9 **ppSwapChain);

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static inline HWND Window(struct plugin_win_mf_consumer_video_s *pSelf);
static inline LONG Width(const RECT& r);
static inline LONG Height(const RECT& r);
static inline RECT CorrectAspectRatio(const RECT& src, const MFRatio& srcPAR);
static inline RECT LetterBoxRect(const RECT& rcSrc, const RECT& rcDst);
static inline HRESULT UpdateDestinationRect(struct plugin_win_mf_consumer_video_s *pSelf, BOOL bForce = FALSE);
static HRESULT ResetDevice(struct plugin_win_mf_consumer_video_s *pSelf, BOOL bUpdateDestinationRect = FALSE);
static HRESULT SetFullscreen(struct plugin_win_mf_consumer_video_s *pSelf, BOOL bFullScreen);
static HWND CreateFullScreenWindow(struct plugin_win_mf_consumer_video_s *pSelf);


typedef struct plugin_win_mf_consumer_video_s
{
	TMEDIA_DECLARE_CONSUMER;
	
	BOOL bStarted, bPrepared, bPaused, bFullScreen;
	BOOL bPluginFireFox, bPluginWebRTC4All;
	HWND hWindow;
	HWND hWindowFullScreen;
	RECT rcWindow;
	RECT rcDest;
	MFRatio pixelAR;

	UINT32 nNegWidth;
	UINT32 nNegHeight;
	UINT32 nNegFps;

	D3DLOCKED_RECT rcLock;
	IDirect3DDevice9* pDevice;
	IDirect3D9 *pD3D;
	IDirect3DSwapChain9 *pSwapChain;
	D3DPRESENT_PARAMETERS d3dpp;

	TSK_DECLARE_SAFEOBJ;
}
plugin_win_mf_consumer_video_t;

static int _plugin_win_mf_consumer_video_unprepare(plugin_win_mf_consumer_video_t* pSelf);

/* ============ Media Consumer Interface ================= */
static int plugin_win_mf_consumer_video_set(tmedia_consumer_t *self, const tmedia_param_t* param)
{
	int ret = 0;
	HRESULT hr = S_OK;
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!self || !param)
	{
		TSK_DEBUG_ERROR("Invalid parameter");
		CHECK_HR(hr = E_POINTER);
	}

	if(param->value_type == tmedia_pvt_int64)
	{
		if(tsk_striequals(param->key, "remote-hwnd"))
		{
			HWND hWnd = reinterpret_cast<HWND>((INT64)*((int64_t*)param->value));
			if(hWnd != pSelf->hWindow)
			{
				tsk_safeobj_lock(pSelf); // block consumer thread
				pSelf->hWindow = hWnd;
				if(pSelf->bPrepared)
				{
					hr = ResetDevice(pSelf);
				}
				tsk_safeobj_unlock(pSelf); // unblock consumer thread
			}

			
		}
	}
	else if(param->value_type == tmedia_pvt_int32)
	{
		if(tsk_striequals(param->key, "fullscreen"))
		{
			BOOL bFullScreen = !!*((int32_t*)param->value);
			TSK_DEBUG_INFO("[MF video consumer] Full Screen = %d", bFullScreen);
			CHECK_HR(hr = SetFullscreen(pSelf, bFullScreen));
		}
		else if(tsk_striequals(param->key, "create-on-current-thead"))
		{
			// DSCONSUMER(self)->create_on_ui_thread = *((int32_t*)param->value) ? tsk_false : tsk_true;
		}
		else if(tsk_striequals(param->key, "plugin-firefox"))
		{
			pSelf->bPluginFireFox = (*((int32_t*)param->value) != 0);
		}
		else if(tsk_striequals(param->key, "plugin-webrtc4all"))
		{
			pSelf->bPluginWebRTC4All = (*((int32_t*)param->value) != 0);
		}
	}

	CHECK_HR(hr);

bail:
	return SUCCEEDED(hr) ?  0 : -1;
}


static int plugin_win_mf_consumer_video_prepare(tmedia_consumer_t* self, const tmedia_codec_t* codec)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf || !codec && codec->plugin){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}
	if(pSelf->bPrepared){
		TSK_DEBUG_WARN("D3D9 video consumer already prepared");
		return -1;
	}

	// FIXME: DirectShow requires flipping but not D3D9
	// The Core library always tries to flip when OSType==Win32. Must be changed
	TMEDIA_CODEC_VIDEO(codec)->in.flip = tsk_false;

	HRESULT hr = S_OK;
	HWND hWnd = Window(pSelf);
	
	TMEDIA_CONSUMER(pSelf)->video.fps = TMEDIA_CODEC_VIDEO(codec)->in.fps;
	TMEDIA_CONSUMER(pSelf)->video.in.width = TMEDIA_CODEC_VIDEO(codec)->in.width;
	TMEDIA_CONSUMER(pSelf)->video.in.height = TMEDIA_CODEC_VIDEO(codec)->in.height;

	if(!TMEDIA_CONSUMER(pSelf)->video.display.width){
		TMEDIA_CONSUMER(pSelf)->video.display.width = TMEDIA_CONSUMER(pSelf)->video.in.width;
	}
	if(!TMEDIA_CONSUMER(pSelf)->video.display.height){
		TMEDIA_CONSUMER(pSelf)->video.display.height = TMEDIA_CONSUMER(pSelf)->video.in.height;
	}
	
	pSelf->nNegFps = TMEDIA_CONSUMER(pSelf)->video.fps;
	pSelf->nNegWidth = TMEDIA_CONSUMER(pSelf)->video.display.width;
	pSelf->nNegHeight = TMEDIA_CONSUMER(pSelf)->video.display.height;

	TSK_DEBUG_INFO("D3D9 video consumer: fps=%d, width=%d, height=%d", 
		pSelf->nNegFps, 
		pSelf->nNegWidth, 
		pSelf->nNegHeight);

	TMEDIA_CONSUMER(pSelf)->video.display.chroma = tmedia_chroma_rgb32;
	TMEDIA_CONSUMER(pSelf)->decoder.codec_id = tmedia_codec_id_none; // means accept RAW fames
	
	// The window handle is not created until the call is connect (incoming only) - At least on Internet Explorer 10
	if(hWnd && !pSelf->bPluginWebRTC4All)
	{
		CHECK_HR(hr = CreateDeviceD3D9(hWnd, &pSelf->pDevice, &pSelf->pD3D, pSelf->d3dpp));
		CHECK_HR(hr = CreateSwapChain(hWnd, pSelf->nNegWidth, pSelf->nNegHeight, pSelf->pDevice, &pSelf->pSwapChain));
	}
	else
	{
		if(hWnd && pSelf->bPluginWebRTC4All)
		{
			TSK_DEBUG_INFO("[MF consumer] HWND is defined but we detected webrtc4all...delaying D3D9 device creating until session get connected");
		}
		else
		{
			TSK_DEBUG_WARN("Delaying D3D9 device creation because HWND is not defined yet");
		}
	}
	

bail:

	pSelf->bPrepared = SUCCEEDED(hr);
	return pSelf->bPrepared ? 0 : -1;
}

static int plugin_win_mf_consumer_video_start(tmedia_consumer_t* self)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}

	if(pSelf->bStarted){
		TSK_DEBUG_INFO("D3D9 video consumer already started");
		return 0;
	}
	if(!pSelf->bPrepared){
		TSK_DEBUG_ERROR("D3D9 video consumer not prepared");
		return -1;
	}

	HRESULT hr = S_OK;

	pSelf->bPaused = false;
	pSelf->bStarted = true;

	CHECK_HR(hr);

bail:
	return SUCCEEDED(hr) ? 0 : -1;
}

static int plugin_win_mf_consumer_video_consume(tmedia_consumer_t* self, const void* buffer, tsk_size_t size, const tsk_object_t* proto_hdr)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	HRESULT hr = S_OK;
	HWND hWnd = Window(pSelf);

	IDirect3DSurface9 *pSurf = NULL;
    IDirect3DSurface9 *pBB = NULL;

	if(!pSelf)
	{
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1; // because of the mutex lock do it here
	}

	tsk_safeobj_lock(pSelf);

	if(!buffer || !size)
	{
		TSK_DEBUG_ERROR("Invalid parameter");
		CHECK_HR(hr = E_INVALIDARG);
	}

	if(!pSelf->bStarted)
	{
		TSK_DEBUG_INFO("D3D9 video consumer not started");
		CHECK_HR(hr = E_FAIL);
	}
	
	if(!hWnd)
	{
		TSK_DEBUG_INFO("Do not draw frame because HWND not set");
		goto bail; // not an error as the application can decide to set the HWND at any time
	}

	if(!pSelf->pDevice || !pSelf->pD3D || !pSelf->pSwapChain)
	{
		if(pSelf->pDevice || pSelf->pD3D || pSelf->pSwapChain)
		{
			CHECK_HR(hr = E_POINTER); // They must be "all null" or "all valid"
		}

		if(hWnd)
		{
			// means HWND was not set but defined now
			pSelf->nNegWidth = TMEDIA_CONSUMER(pSelf)->video.in.width;
			pSelf->nNegHeight = TMEDIA_CONSUMER(pSelf)->video.in.height;

			CHECK_HR(hr = CreateDeviceD3D9(hWnd, &pSelf->pDevice, &pSelf->pD3D, pSelf->d3dpp));
			CHECK_HR(hr = CreateSwapChain(hWnd, pSelf->nNegWidth, pSelf->nNegHeight, pSelf->pDevice, &pSelf->pSwapChain));
		}
	}

	if(pSelf->nNegWidth != TMEDIA_CONSUMER(pSelf)->video.in.width || pSelf->nNegHeight != TMEDIA_CONSUMER(pSelf)->video.in.height){
		TSK_DEBUG_INFO("Negotiated and input video sizes are different:%d#%d or %d#%d",
			pSelf->nNegWidth, TMEDIA_CONSUMER(pSelf)->video.in.width,
			pSelf->nNegHeight, TMEDIA_CONSUMER(pSelf)->video.in.height);
		// Update media type
		
		SafeRelease(&pSelf->pSwapChain);
		CHECK_HR(hr = CreateSwapChain(hWnd, TMEDIA_CONSUMER(pSelf)->video.in.width, TMEDIA_CONSUMER(pSelf)->video.in.height, pSelf->pDevice, &pSelf->pSwapChain));

		pSelf->nNegWidth = TMEDIA_CONSUMER(pSelf)->video.in.width;
		pSelf->nNegHeight = TMEDIA_CONSUMER(pSelf)->video.in.height;

		// Update Destination will do noting if the window size haven't changed. 
		// Force updating the destination rect if negotiated size change
		CHECK_HR(hr = UpdateDestinationRect(pSelf, TRUE/* Force */));
	}
	
	CHECK_HR(hr = TestCooperativeLevel(pSelf));

	CHECK_HR(hr = UpdateDestinationRect(pSelf));

	CHECK_HR(hr = pSelf->pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pSurf));    
	CHECK_HR(hr = pSurf->LockRect(&pSelf->rcLock, NULL, D3DLOCK_NOSYSLOCK ));

	// Fast copy() using MMX, SSE, or SSE2
	 hr = MFCopyImage(
		 (BYTE*)pSelf->rcLock.pBits, 
		 pSelf->rcLock.Pitch, 
		 (BYTE*)buffer, 
		 (pSelf->nNegWidth << 2),
		 (pSelf->nNegWidth << 2),
		 pSelf->nNegHeight
	 );
	 if(FAILED(hr))
	 {
		 // unlock() before leaving
		pSurf->UnlockRect();
		CHECK_HR(hr);
	 }
	
	CHECK_HR(hr = pSurf->UnlockRect());

	// Color fill the back buffer
	CHECK_HR(hr = pSelf->pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBB));
#if METROPOLIS
	CHECK_HR(hr = pSelf->pDevice->ColorFill(pBB, NULL, D3DCOLOR_XRGB(0x00, 0x00, 0x00)));
#else
	CHECK_HR(hr = pSelf->pDevice->ColorFill(pBB, NULL, D3DCOLOR_XRGB(0xFF, 0xFF, 0xFF)));
#endif
	
	// Resize keeping aspect ratio and Blit the frame (required)
	hr = pSelf->pDevice->StretchRect(
		pSurf, 
		NULL, 
		pBB, 
		&pSelf->rcDest/*NULL*/, 
		D3DTEXF_LINEAR
	); // could fail when display is being resized
	if(SUCCEEDED(hr))
	{
		// Present the frame
		CHECK_HR(hr = pSelf->pDevice->Present(NULL, NULL, NULL, NULL));
	}
	else
	{
		TSK_DEBUG_INFO("StretchRect returned ...%x", hr);
	}

bail:
	SafeRelease(&pSurf);
	SafeRelease(&pBB);

	tsk_safeobj_unlock(pSelf);

	return SUCCEEDED(hr) ?  0 : -1;
}

static int plugin_win_mf_consumer_video_pause(tmedia_consumer_t* self)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}
	if(!pSelf->bStarted)
	{
		TSK_DEBUG_INFO("MF video producer not started");
		return 0;
	}

	HRESULT hr = S_OK;

	pSelf->bPaused = true;

	return SUCCEEDED(hr) ? 0 : -1;
}

static int plugin_win_mf_consumer_video_stop(tmedia_consumer_t* self)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf){
        TSK_DEBUG_ERROR("Invalid parameter");
        return -1;
    }

    HRESULT hr = S_OK;	
    
    pSelf->bStarted = false;
	pSelf->bPaused = false;

	// Clear last video frame
	if(pSelf->hWindow)
	{
		::InvalidateRect(pSelf->hWindow, NULL, FALSE);
	}
	if(pSelf->hWindowFullScreen)
	{
		::InvalidateRect(pSelf->hWindowFullScreen, NULL, FALSE);
		::ShowWindow(pSelf->hWindowFullScreen, SW_HIDE);
	}

	// next start() will be called after prepare()
	return _plugin_win_mf_consumer_video_unprepare(pSelf);
}

static int _plugin_win_mf_consumer_video_unprepare(plugin_win_mf_consumer_video_t* pSelf)
{
	if(!pSelf)
	{
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}

	if(pSelf->bStarted)
	{
		// plugin_win_mf_producer_video_stop(TMEDIA_PRODUCER(pSelf));
		TSK_DEBUG_ERROR("Consumer must be stopped before calling unprepare");
		return -1;
	}

	SafeRelease(&pSelf->pDevice);
	SafeRelease(&pSelf->pD3D);
	SafeRelease(&pSelf->pSwapChain);

	pSelf->bPrepared = false;

	return 0;
}


//
//	D3D9 video consumer object definition
//
/* constructor */
static tsk_object_t* plugin_win_mf_consumer_video_ctor(tsk_object_t * self, va_list * app)
{
	MFUtils::Startup();

	plugin_win_mf_consumer_video_t *pSelf = (plugin_win_mf_consumer_video_t *)self;
	if(pSelf){
		/* init base */
		tmedia_consumer_init(TMEDIA_CONSUMER(pSelf));
		TMEDIA_CONSUMER(pSelf)->video.display.chroma = tmedia_chroma_rgb32;
		TMEDIA_CONSUMER(pSelf)->decoder.codec_id = tmedia_codec_id_none; // means accept RAW fames

		/* init self */
		tsk_safeobj_init(pSelf);
		TMEDIA_CONSUMER(pSelf)->video.fps = 15;
		TMEDIA_CONSUMER(pSelf)->video.display.width = 0; // use codec value
		TMEDIA_CONSUMER(pSelf)->video.display.height = 0; // use codec value
		TMEDIA_CONSUMER(pSelf)->video.display.auto_resize = tsk_true;

		pSelf->pixelAR.Denominator = pSelf->pixelAR.Numerator = 1;
	}
	return self;
}
/* destructor */
static tsk_object_t* plugin_win_mf_consumer_video_dtor(tsk_object_t * self)
{ 
	plugin_win_mf_consumer_video_t *pSelf = (plugin_win_mf_consumer_video_t *)self;
	if(pSelf){
		/* stop */
		if(pSelf->bStarted)
		{
			plugin_win_mf_consumer_video_stop(TMEDIA_CONSUMER(pSelf));
		}

		/* deinit base */
		tmedia_consumer_deinit(TMEDIA_CONSUMER(pSelf));
		/* deinit self */
		_plugin_win_mf_consumer_video_unprepare(pSelf);
		tsk_safeobj_deinit(pSelf);
	}

	return self;
}
/* object definition */
static const tsk_object_def_t plugin_win_mf_consumer_video_def_s = 
{
	sizeof(plugin_win_mf_consumer_video_t),
	plugin_win_mf_consumer_video_ctor, 
	plugin_win_mf_consumer_video_dtor,
	tsk_null, 
};
/* plugin definition*/
static const tmedia_consumer_plugin_def_t plugin_win_mf_consumer_video_plugin_def_s = 
{
	&plugin_win_mf_consumer_video_def_s,
	
	tmedia_video,
	"D3D9 video consumer",
	
	plugin_win_mf_consumer_video_set,
	plugin_win_mf_consumer_video_prepare,
	plugin_win_mf_consumer_video_start,
	plugin_win_mf_consumer_video_consume,
	plugin_win_mf_consumer_video_pause,
	plugin_win_mf_consumer_video_stop
};
const tmedia_consumer_plugin_def_t *plugin_win_mf_consumer_video_plugin_def_t = &plugin_win_mf_consumer_video_plugin_def_s;

// Helper functions

static HRESULT CreateDeviceD3D9(
	HWND hWnd, 
	IDirect3DDevice9** ppDevice, 
	IDirect3D9 **ppD3D,
	D3DPRESENT_PARAMETERS &d3dpp
	)
{
	HRESULT hr = S_OK;

    D3DDISPLAYMODE mode = { 0 };
	D3DPRESENT_PARAMETERS pp = {0};

	if(!ppDevice || *ppDevice || !ppD3D || *ppD3D)
	{
		CHECK_HR(hr = E_POINTER);
	}
    
    if(!(*ppD3D = Direct3DCreate9(D3D_SDK_VERSION)))
    {
        CHECK_HR(hr = E_OUTOFMEMORY);
    }

    CHECK_HR(hr = (*ppD3D)->GetAdapterDisplayMode(
        D3DADAPTER_DEFAULT,
        &mode
        ));

    CHECK_HR(hr = (*ppD3D)->CheckDeviceType(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        mode.Format,
        D3DFMT_X8R8G8B8,
        TRUE    // windowed
        ));
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	pp.Windowed = TRUE;
    pp.hDeviceWindow = hWnd;
    CHECK_HR(hr = (*ppD3D)->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hWnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &pp,
        ppDevice
        ));

	d3dpp = pp;

bail:
	if(FAILED(hr))
	{
		SafeRelease(ppD3D);
		SafeRelease(ppDevice);
	}
    return hr;
}

static HRESULT TestCooperativeLevel(
	struct plugin_win_mf_consumer_video_s *pSelf
	)
{
	HRESULT hr = S_OK;

	if (!pSelf || !pSelf->pDevice)
    {
		CHECK_HR(hr = E_POINTER);
    }
    
    switch((hr = pSelf->pDevice->TestCooperativeLevel()))
    {
		case D3D_OK:
			{
				break;
			}

		case D3DERR_DEVICELOST:
			{
				hr = S_OK;
				break;
			}
			
		case D3DERR_DEVICENOTRESET:
			{
				hr = ResetDevice(pSelf, TRUE);
				break;
			}

		default:
			{
				break;
			}
    }

	CHECK_HR(hr);

bail:
    return hr;
}

static HRESULT CreateSwapChain(
	HWND hWnd, 
	UINT32 nFrameWidth, 
	UINT32 nFrameHeight, 
	IDirect3DDevice9* pDevice, 
	IDirect3DSwapChain9 **ppSwapChain
	)
{
    HRESULT hr = S_OK;

    D3DPRESENT_PARAMETERS pp = { 0 };

	if(!pDevice || !ppSwapChain || *ppSwapChain)
	{
		CHECK_HR(hr = E_POINTER);
	}
	
	pp.BackBufferWidth  = nFrameWidth;
    pp.BackBufferHeight = nFrameHeight;
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_FLIP;
    pp.hDeviceWindow = hWnd;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.Flags =
        D3DPRESENTFLAG_VIDEO | D3DPRESENTFLAG_DEVICECLIP |
        D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.BackBufferCount = NUM_BACK_BUFFERS;

     CHECK_HR(hr = pDevice->CreateAdditionalSwapChain(&pp, ppSwapChain));

bail:
    return hr;
}

static inline HWND Window(struct plugin_win_mf_consumer_video_s *pSelf)
{
	return pSelf ? (pSelf->bFullScreen ? pSelf->hWindowFullScreen : pSelf->hWindow) : NULL;
}

static inline LONG Width(const RECT& r)
{
    return r.right - r.left;
}

static inline LONG Height(const RECT& r)
{
    return r.bottom - r.top;
}

//-----------------------------------------------------------------------------
// CorrectAspectRatio
//
// Converts a rectangle from the source's pixel aspect ratio (PAR) to 1:1 PAR.
// Returns the corrected rectangle.
//
// For example, a 720 x 486 rect with a PAR of 9:10, when converted to 1x1 PAR,
// is stretched to 720 x 540.
// Copyright (C) Microsoft
//-----------------------------------------------------------------------------

static inline RECT CorrectAspectRatio(const RECT& src, const MFRatio& srcPAR)
{
    // Start with a rectangle the same size as src, but offset to the origin (0,0).
    RECT rc = {0, 0, src.right - src.left, src.bottom - src.top};

    if ((srcPAR.Numerator != 1) || (srcPAR.Denominator != 1))
    {
        // Correct for the source's PAR.

        if (srcPAR.Numerator > srcPAR.Denominator)
        {
            // The source has "wide" pixels, so stretch the width.
            rc.right = MulDiv(rc.right, srcPAR.Numerator, srcPAR.Denominator);
        }
        else if (srcPAR.Numerator < srcPAR.Denominator)
        {
            // The source has "tall" pixels, so stretch the height.
            rc.bottom = MulDiv(rc.bottom, srcPAR.Denominator, srcPAR.Numerator);
        }
        // else: PAR is 1:1, which is a no-op.
    }
    return rc;
}

//-------------------------------------------------------------------
// LetterBoxDstRect
//
// Takes a src rectangle and constructs the largest possible
// destination rectangle within the specifed destination rectangle
// such thatthe video maintains its current shape.
//
// This function assumes that pels are the same shape within both the
// source and destination rectangles.
// Copyright (C) Microsoft
//-------------------------------------------------------------------

static inline RECT LetterBoxRect(const RECT& rcSrc, const RECT& rcDst)
{
    // figure out src/dest scale ratios
    int iSrcWidth  = Width(rcSrc);
    int iSrcHeight = Height(rcSrc);

    int iDstWidth  = Width(rcDst);
    int iDstHeight = Height(rcDst);

    int iDstLBWidth;
    int iDstLBHeight;

    if (MulDiv(iSrcWidth, iDstHeight, iSrcHeight) <= iDstWidth) {

        // Column letter boxing ("pillar box")

        iDstLBWidth  = MulDiv(iDstHeight, iSrcWidth, iSrcHeight);
        iDstLBHeight = iDstHeight;
    }
    else {

        // Row letter boxing.

        iDstLBWidth  = iDstWidth;
        iDstLBHeight = MulDiv(iDstWidth, iSrcHeight, iSrcWidth);
    }


    // Create a centered rectangle within the current destination rect

    RECT rc;

    LONG left = rcDst.left + ((iDstWidth - iDstLBWidth) >> 1);
    LONG top = rcDst.top + ((iDstHeight - iDstLBHeight) >> 1);

    SetRect(&rc, left, top, left + iDstLBWidth, top + iDstLBHeight);

    return rc;
}

static inline HRESULT UpdateDestinationRect(plugin_win_mf_consumer_video_t *pSelf, BOOL bForce /*= FALSE*/)
{
	HRESULT hr = S_OK;
	HWND hwnd = Window(pSelf);

	if(!pSelf)
	{
		CHECK_HR(hr = E_POINTER);
	}

	if(!hwnd)
	{
		CHECK_HR(hr = E_HANDLE);
	}
    RECT rcClient;
	GetClientRect(hwnd, &rcClient);

	// only update destination if window size changed
	if(bForce || (rcClient.bottom != pSelf->rcWindow.bottom || rcClient.left != pSelf->rcWindow.left || rcClient.right != pSelf->rcWindow.right || rcClient.top != pSelf->rcWindow.top))
	{
		CHECK_HR(hr = ResetDevice(pSelf));

		pSelf->rcWindow = rcClient;
#if 1
		RECT rcSrc = { 0, 0, pSelf->nNegWidth, pSelf->nNegHeight };
		rcSrc = CorrectAspectRatio(rcSrc, pSelf->pixelAR);
		pSelf->rcDest = LetterBoxRect(rcSrc, rcClient);
#else
		long w = rcClient.right - rcClient.left;
		long h = rcClient.bottom - rcClient.top;
		float ratio = ((float)pSelf->nNegWidth/(float)pSelf->nNegHeight);
		// (w/h)=ratio => 
		// 1) h=w/ratio 
		// and 
		// 2) w=h*ratio
		pSelf->rcDest.right = (int)(w/ratio) > h ? (int)(h * ratio) : w;
		pSelf->rcDest.bottom = (int)(pSelf->rcDest.right/ratio) > h ? h : (int)(pSelf->rcDest.right/ratio);
		pSelf->rcDest.left = ((w - pSelf->rcDest.right) >> 1);
		pSelf->rcDest.top = ((h - pSelf->rcDest.bottom) >> 1);
#endif

		//::InvalidateRect(hwnd, NULL, FALSE);
	}	

bail:
	return hr;
}

static HRESULT ResetDevice(plugin_win_mf_consumer_video_t *pSelf, BOOL bUpdateDestinationRect /*= FALSE*/)
{
    HRESULT hr = S_OK;

	tsk_safeobj_lock(pSelf);

	HWND hWnd = Window(pSelf);

    if (pSelf->pDevice)
    {
        D3DPRESENT_PARAMETERS d3dpp = pSelf->d3dpp;

        hr = pSelf->pDevice->Reset(&d3dpp);

        if (FAILED(hr))
        {
            SafeRelease(&pSelf->pDevice);
			SafeRelease(&pSelf->pD3D);
			SafeRelease(&pSelf->pSwapChain);
        }
    }

    if (pSelf->pDevice == NULL && hWnd)
    {
        CHECK_HR(hr = CreateDeviceD3D9(hWnd, &pSelf->pDevice, &pSelf->pD3D, pSelf->d3dpp));
		CHECK_HR(hr = CreateSwapChain(hWnd, pSelf->nNegWidth, pSelf->nNegHeight, pSelf->pDevice, &pSelf->pSwapChain));
    }    

	if(bUpdateDestinationRect) // endless loop guard
	{
		CHECK_HR(hr = UpdateDestinationRect(pSelf));
	}

bail:
	tsk_safeobj_unlock(pSelf);

   return hr;
}

static HRESULT SetFullscreen(struct plugin_win_mf_consumer_video_s *pSelf, BOOL bFullScreen)
{
	HRESULT hr = S_OK;
	if(!pSelf)
	{
		CHECK_HR(hr = E_POINTER);
	}

	if(pSelf->bFullScreen != bFullScreen)
	{
		tsk_safeobj_lock(pSelf);
		if(bFullScreen)
		{
			HWND hWnd = CreateFullScreenWindow(pSelf);
			if(hWnd)
			{
				::ShowWindow(hWnd, SW_SHOWDEFAULT);
				::UpdateWindow(hWnd);
			}
		}
		else if(pSelf->hWindowFullScreen)
		{
			::ShowWindow(pSelf->hWindowFullScreen, SW_HIDE);
		}
		pSelf->bFullScreen = bFullScreen;
		if(pSelf->bPrepared)
		{
			hr = ResetDevice(pSelf);
		}
		tsk_safeobj_unlock(pSelf);

		CHECK_HR(hr);
	}

bail:
	return hr;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
		case WM_CREATE:
		case WM_SIZE:
		case WM_MOVE:
			{
				struct plugin_win_mf_consumer_video_s* pSelf = dynamic_cast<struct plugin_win_mf_consumer_video_s*>((struct plugin_win_mf_consumer_video_s*)GetPropA(hWnd, "Self"));
				if(pSelf)
				{					
					
				}
				break;
			}

		case WM_CHAR:
		case WM_KEYUP:
			{
				struct plugin_win_mf_consumer_video_s* pSelf = dynamic_cast<struct plugin_win_mf_consumer_video_s*>((struct plugin_win_mf_consumer_video_s*)GetPropA(hWnd, "Self"));
				if(pSelf)
				{	
					SetFullscreen(pSelf, FALSE);
				}
				
				break;
			}
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static HWND CreateFullScreenWindow(struct plugin_win_mf_consumer_video_s *pSelf)
{
	HRESULT hr = S_OK;
	
	if(!pSelf)
	{
		return NULL;
	}

	if(!pSelf->hWindowFullScreen)
	{
		WNDCLASS wc = {0};

		wc.lpfnWndProc   = WndProc;
		wc.hInstance     = GetModuleHandle(NULL);
		wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName =  L"WindowClass";
		RegisterClass(&wc);
		pSelf->hWindowFullScreen = ::CreateWindowEx(
				NULL,
				wc.lpszClassName, 
				L"Doubango's Video Consumer Fullscreen",
				WS_EX_TOPMOST | WS_POPUP,
				0, 0,
				GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
				NULL,
				NULL,
				GetModuleHandle(NULL),
				NULL);

		SetPropA(pSelf->hWindowFullScreen, "Self", pSelf);
	}
	return pSelf->hWindowFullScreen;
}


#else /* !PLUGIN_MF_CV_USE_D3D9 */

#include "internals/mf_custom_src.h"
#include "internals/mf_display_watcher.h"
#include "internals/mf_codec.h"

#include <KS.h>
#include <Codecapi.h>

// 0: {{[Source] -> (VideoProcessor) -> SampleGrabber}} , {{[Decoder]}} -> RTP
// 1: {{[Source] -> (VideoProcessor) -> [Decoder] -> SampleGrabber}} -> RTP
// (VideoProcessor) is optional
// "{{" and "}}" defines where the graph starts and ends respectively. For "0", [Decoder] is a stand-alone IMFTransform.
#if !defined(PLUGIN_MF_CV_BUNDLE_CODEC)
#	define PLUGIN_MF_CV_BUNDLE_CODEC 0
#endif

// Uncompressed video frame will come from Doubango core and it's up to the converter to match the requested chroma.
// Supported values: NV12, I420, RGB32 and RGB24. (RGB formats are not recommended because of performance issues)
// To avoid chroma conversion (performance issues) we use NV12 when the codec is bundled as MediaFoundation codecs most likely only support this format.
// NV12 is the native format for media foundation codecs (e.g. Intel Quick Sync) and the GPU.
// I420 is the native format for FFmpeg, libvpx and libtheora.
const GUID kDefaultUncompressedType 
#if PLUGIN_MF_CV_BUNDLE_CODEC
= MFVideoFormat_NV12;
#else
= MFVideoFormat_I420;
#endif

DEFINE_GUID(PLUGIN_MF_LOW_LATENCY,
0x9c27891a, 0xed7a, 0x40e1, 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee);

static void* TSK_STDCALL RunSessionThread(void *pArg);
static int _plugin_win_mf_consumer_video_unprepare(struct plugin_win_mf_consumer_video_s* pSelf);

typedef struct plugin_win_mf_consumer_video_s
{
	TMEDIA_DECLARE_CONSUMER;
	
	bool bStarted, bPrepared;
	HWND hWindow;
	tsk_thread_handle_t* ppTread[1];

	UINT32 nNegWidth;
	UINT32 nNegHeight;
	UINT32 nNegFps;

	MFCodecVideo *pDecoder;
    IMFMediaSession *pSession;
    CMFSource *pSource;
    IMFActivate *pSinkActivate;
	DisplayWatcher* pDisplayWatcher;
    IMFTopology *pTopologyFull;
	IMFTopology *pTopologyPartial;
	IMFMediaType *pOutType;
}
plugin_win_mf_consumer_video_t;



/* ============ Media Consumer Interface ================= */
static int plugin_win_mf_consumer_video_set(tmedia_consumer_t *self, const tmedia_param_t* param)
{
	int ret = 0;
	HRESULT hr = S_OK;
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!self || !param){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}

	if(param->value_type == tmedia_pvt_int64){
		if(tsk_striequals(param->key, "remote-hwnd")){
			HWND hWnd = reinterpret_cast<HWND>((INT64)*((int64_t*)param->value));
			if(hWnd != pSelf->hWindow)
			{
				pSelf->hWindow = hWnd;
				if(pSelf->pDisplayWatcher)
				{
					CHECK_HR(hr = pSelf->pDisplayWatcher->SetHwnd(hWnd));
				}
			}
		}
	}
	else if(param->value_type == tmedia_pvt_int32){
		if(tsk_striequals(param->key, "fullscreen")){
			if(pSelf->pDisplayWatcher)
			{
				CHECK_HR(hr = pSelf->pDisplayWatcher->SetFullscreen(!!*((int32_t*)param->value)));
			}
		}
		else if(tsk_striequals(param->key, "create-on-current-thead")){
			// DSCONSUMER(self)->create_on_ui_thread = *((int32_t*)param->value) ? tsk_false : tsk_true;
		}
		else if(tsk_striequals(param->key, "plugin-firefox")){
			/*DSCONSUMER(self)->plugin_firefox = (*((int32_t*)param->value) != 0);
			if(DSCONSUMER(self)->display){
				DSCONSUMER(self)->display->setPluginFirefox((DSCONSUMER(self)->plugin_firefox == tsk_true));
			}*/
		}
	}

bail:
	return SUCCEEDED(hr) ?  0 : -1;
}


static int plugin_win_mf_consumer_video_prepare(tmedia_consumer_t* self, const tmedia_codec_t* codec)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf || !codec && codec->plugin){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}
	if(pSelf->bPrepared){
		TSK_DEBUG_WARN("MF video consumer already prepared");
		return -1;
	}

	// FIXME: DirectShow requires flipping but not MF
	// The Core library always tries to flip when OSType==Win32. Must be changed
	TMEDIA_CODEC_VIDEO(codec)->in.flip = tsk_false;

	HRESULT hr = S_OK;
	
	TMEDIA_CONSUMER(pSelf)->video.fps = TMEDIA_CODEC_VIDEO(codec)->in.fps;
	TMEDIA_CONSUMER(pSelf)->video.in.width = TMEDIA_CODEC_VIDEO(codec)->in.width;
	TMEDIA_CONSUMER(pSelf)->video.in.height = TMEDIA_CODEC_VIDEO(codec)->in.height;

	if(!TMEDIA_CONSUMER(pSelf)->video.display.width){
		TMEDIA_CONSUMER(pSelf)->video.display.width = TMEDIA_CONSUMER(pSelf)->video.in.width;
	}
	if(!TMEDIA_CONSUMER(pSelf)->video.display.height){
		TMEDIA_CONSUMER(pSelf)->video.display.height = TMEDIA_CONSUMER(pSelf)->video.in.height;
	}
	
	pSelf->nNegFps = TMEDIA_CONSUMER(pSelf)->video.fps;
	pSelf->nNegWidth = TMEDIA_CONSUMER(pSelf)->video.display.width;
	pSelf->nNegHeight = TMEDIA_CONSUMER(pSelf)->video.display.height;

	TSK_DEBUG_INFO("MF video consumer: fps=%d, width=%d, height=%d", 
		pSelf->nNegFps, 
		pSelf->nNegWidth, 
		pSelf->nNegHeight);

	if(kDefaultUncompressedType == MFVideoFormat_NV12) {
		TMEDIA_CONSUMER(pSelf)->video.display.chroma = tmedia_chroma_nv12;
	}
	else if(kDefaultUncompressedType == MFVideoFormat_I420) {
		TMEDIA_CONSUMER(pSelf)->video.display.chroma = tmedia_chroma_yuv420p;
	}
	else if(kDefaultUncompressedType == MFVideoFormat_RGB32) {
		TMEDIA_CONSUMER(pSelf)->video.display.chroma = tmedia_chroma_rgb32;
	}
	else if(kDefaultUncompressedType == MFVideoFormat_RGB24) {
		TMEDIA_CONSUMER(pSelf)->video.display.chroma = tmedia_chroma_rgb24;
	}
	else {
		CHECK_HR(hr = E_NOTIMPL);
	}
	TMEDIA_CONSUMER(pSelf)->decoder.codec_id = tmedia_codec_id_none; // means accept RAW fames
	
	IMFMediaSink* pMediaSink = NULL;
	IMFAttributes* pSessionAttributes = NULL;

	// Set session attributes
	CHECK_HR(hr = MFCreateAttributes(&pSessionAttributes, 1));
	CHECK_HR(hr = pSessionAttributes->SetUINT32(PLUGIN_MF_LOW_LATENCY, 1));

	CHECK_HR(hr = MFCreateMediaType(&pSelf->pOutType));
	CHECK_HR(hr = pSelf->pOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));

#if PLUGIN_MF_CV_BUNDLE_CODEC
	if((codec->id == tmedia_codec_id_h264_bp || codec->id == tmedia_codec_id_h264_mp) && MFUtils::IsLowLatencyH264Supported()) {
		// both Microsoft and Intel encoders support NV12 only as input
		// static const BOOL kIsEncoder = FALSE;
		// hr = MFUtils::GetBestCodec(kIsEncoder, MFMediaType_Video, MFVideoFormat_H264, MFVideoFormat_NV12, &pSelf->pDecoder);
		pSelf->pDecoder = (codec->id == tmedia_codec_id_h264_bp) ? MFCodecVideoH264::CreateCodecH264Base(MFCodecType_Decoder) : MFCodecVideoH264::CreateCodecH264Main(MFCodecType_Decoder);
		if(pSelf->pDecoder)
		{
			hr = pSelf->pDecoder->Initialize(
				pSelf->nNegFps,
				pSelf->nNegWidth,
				pSelf->nNegHeight);
			
			if(FAILED(hr))
			{
				SafeRelease(&pSelf->pDecoder);
				hr = S_OK;
			}
		}
		if(SUCCEEDED(hr) && pSelf->pDecoder) {
			TMEDIA_CONSUMER(pSelf)->decoder.codec_id = codec->id; // means accept ENCODED fames
			CHECK_HR(hr = pSelf->pOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
		}
		else {
			SafeRelease(&pSelf->pDecoder);
			TSK_DEBUG_WARN("Failed to find H.264 HW encoder...fallback to SW implementation");
		}
	}
#endif

	if(!pSelf->pDecoder){
		CHECK_HR(hr = pSelf->pOutType->SetGUID(MF_MT_SUBTYPE, kDefaultUncompressedType));
		TMEDIA_CONSUMER(pSelf)->video.display.chroma = kDefaultUncompressedType == MFVideoFormat_NV12 ? tmedia_chroma_nv12 : tmedia_chroma_yuv420p;
	}
    CHECK_HR(hr = pSelf->pOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	CHECK_HR(hr = pSelf->pOutType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
    CHECK_HR(hr = MFSetAttributeSize(pSelf->pOutType, MF_MT_FRAME_SIZE, pSelf->nNegWidth, pSelf->nNegHeight));
    CHECK_HR(hr = MFSetAttributeRatio(pSelf->pOutType, MF_MT_FRAME_RATE, pSelf->nNegFps, 1));     
    CHECK_HR(hr = MFSetAttributeRatio(pSelf->pOutType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	CHECK_HR(hr = CMFSource::CreateInstanceEx(IID_IMFMediaSource, (void**)&pSelf->pSource, pSelf->pOutType));

	// Apply Encoder output type (must be called before SetInputType)
	//if(pSelf->pDecoder) {
	//	CHECK_HR(hr = pSelf->pDecoder->SetOutputType(0, pSelf->pOutType, 0/*MFT_SET_TYPE_TEST_ONLY*/));
	//}

	// Create the Media Session.
	CHECK_HR(hr = MFCreateMediaSession(pSessionAttributes, &pSelf->pSession));

	// Create the EVR activation object.
	CHECK_HR(hr = MFCreateVideoRendererActivate(pSelf->hWindow, &pSelf->pSinkActivate));

	// Create the topology.
	CHECK_HR(hr = MFUtils::CreateTopology(
		pSelf->pSource, 
		pSelf->pDecoder ? pSelf->pDecoder->GetMFT() : NULL, 
		pSelf->pSinkActivate, 
		NULL/*Preview*/, 
		pSelf->pOutType, 
		&pSelf->pTopologyPartial));
	// Resolve topology (adds video processors if needed).
	CHECK_HR(hr = MFUtils::ResolveTopology(pSelf->pTopologyPartial, &pSelf->pTopologyFull));

	// Find EVR
	CHECK_HR(hr = MFUtils::FindNodeObject(pSelf->pTopologyFull, MFUtils::g_ullTopoIdSinkMain, (void**)&pMediaSink));

	// Create EVR watcher
	pSelf->pDisplayWatcher = new DisplayWatcher(pSelf->hWindow, pMediaSink, hr);
	CHECK_HR(hr);

bail:
	SafeRelease(&pMediaSink);
	SafeRelease(&pSessionAttributes);
	
	pSelf->bPrepared = SUCCEEDED(hr);
	return pSelf->bPrepared ? 0 : -1;
}

static int plugin_win_mf_consumer_video_start(tmedia_consumer_t* self)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}

	if(pSelf->bStarted){
		TSK_DEBUG_INFO("MF video consumer already started");
		return 0;
	}
	if(!pSelf->bPrepared){
		TSK_DEBUG_ERROR("MF video consumer not prepared");
		return -1;
	}

	HRESULT hr = S_OK;

	// Run EVR watcher
	if(pSelf->pDisplayWatcher) {
		CHECK_HR(hr = pSelf->pDisplayWatcher->Start());
	}

	// Run the media session.
	CHECK_HR(hr = MFUtils::RunSession(pSelf->pSession, pSelf->pTopologyFull));

	// Start asynchronous watcher thread
	pSelf->bStarted = true;
	int ret = tsk_thread_create(&pSelf->ppTread[0], RunSessionThread, pSelf);
	if(ret != 0) {
		TSK_DEBUG_ERROR("Failed to create thread");
		hr = E_FAIL;
		pSelf->bStarted = false;
		if(pSelf->ppTread[0]){
			tsk_thread_join(&pSelf->ppTread[0]);
		}
		MFUtils::ShutdownSession(pSelf->pSession, pSelf->pSource);
		CHECK_HR(hr = E_FAIL);
	}

bail:
	return SUCCEEDED(hr) ? 0 : -1;
}

static int plugin_win_mf_consumer_video_consume(tmedia_consumer_t* self, const void* buffer, tsk_size_t size, const tsk_object_t* proto_hdr)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	HRESULT hr = S_OK;

	if(!pSelf || !buffer || !size) {
		TSK_DEBUG_ERROR("Invalid parameter");
		CHECK_HR(hr = E_INVALIDARG);
	}

	if(!pSelf->bStarted) {
		TSK_DEBUG_INFO("MF video consumer not started");
		CHECK_HR(hr = E_FAIL);
	}
	if(!pSelf->pSource) {
		TSK_DEBUG_ERROR("No video custom source");
		CHECK_HR(hr = E_FAIL);
	}

	if(pSelf->nNegWidth != TMEDIA_CONSUMER(pSelf)->video.in.width || pSelf->nNegHeight != TMEDIA_CONSUMER(pSelf)->video.in.height){
		TSK_DEBUG_INFO("Negotiated and input video sizes are different:%d#%d or %d#%d",
			pSelf->nNegWidth, TMEDIA_CONSUMER(pSelf)->video.in.width,
			pSelf->nNegHeight, TMEDIA_CONSUMER(pSelf)->video.in.height);
		// Update media type
		CHECK_HR(hr = MFSetAttributeSize(pSelf->pOutType, MF_MT_FRAME_SIZE, TMEDIA_CONSUMER(pSelf)->video.in.width, TMEDIA_CONSUMER(pSelf)->video.in.height));
		CHECK_HR(hr = MFSetAttributeRatio(pSelf->pOutType, MF_MT_FRAME_RATE, TMEDIA_CONSUMER(pSelf)->video.fps, 1));

		CHECK_HR(hr = pSelf->pSession->ClearTopologies());

		//
		// FIXME: Using same EVR when the size is just swapped (e.g. [640, 480] -> [480, 640]) doesn't work while other changes does (e.g. [352, 288] -> [640, 480])
		// /!\This look like a bug in Media Foundation
		//
		if(pSelf->nNegWidth == TMEDIA_CONSUMER(pSelf)->video.in.height && pSelf->nNegHeight == TMEDIA_CONSUMER(pSelf)->video.in.width)  // swapped?
		{
			TSK_DEBUG_INFO("/!\\ Size swapped");

			IMFActivate* pSinkActivate = NULL;
			IMFTopology* pTopologyPartial = NULL;
			hr = MFCreateVideoRendererActivate(pSelf->hWindow, &pSinkActivate);
			if(FAILED(hr)) goto end_of_swapping;
			hr = MFUtils::CreateTopology(
				pSelf->pSource, 
				pSelf->pDecoder ? pSelf->pDecoder->GetMFT() : NULL, 
				pSinkActivate, 
				NULL/*Preview*/, 
				pSelf->pOutType, 
				&pTopologyPartial);
			if(FAILED(hr)) goto end_of_swapping;

			if(SUCCEEDED(hr)) {
				SafeRelease(&pSelf->pSinkActivate);
				SafeRelease(&pSelf->pTopologyPartial);
				pSelf->pSinkActivate = pSinkActivate; pSinkActivate = NULL;
				pSelf->pTopologyPartial = pTopologyPartial; pTopologyPartial = NULL;
				
			}
			
end_of_swapping:
			SafeRelease(&pSinkActivate);
			SafeRelease(&pTopologyPartial);
			CHECK_HR(hr);
		}

		// Set media type again (not required but who know)
		CHECK_HR(hr = MFUtils::SetMediaType(pSelf->pSource, pSelf->pOutType));

		// Rebuild topology using the partial one
		IMFTopology* pTopologyFull = NULL;
		hr = MFUtils::ResolveTopology(pSelf->pTopologyPartial, &pTopologyFull);
		if(SUCCEEDED(hr)){
			SafeRelease(&pSelf->pTopologyFull);
			pSelf->pTopologyFull = pTopologyFull; pTopologyFull = NULL;
		}
		SafeRelease(&pTopologyFull);
		CHECK_HR(hr);

		// Find Main Sink
		IMFMediaSink* pMediaSink = NULL;
		hr = MFUtils::FindNodeObject(pSelf->pTopologyFull, MFUtils::g_ullTopoIdSinkMain, (void**)&pMediaSink);
		if(SUCCEEDED(hr)) {
			if(pSelf->pDisplayWatcher){
				delete pSelf->pDisplayWatcher, pSelf->pDisplayWatcher = NULL;
			}
			pSelf->pDisplayWatcher = new DisplayWatcher(pSelf->hWindow, pMediaSink, hr);
			if(SUCCEEDED(hr) && pSelf->bStarted) {
				hr = pSelf->pDisplayWatcher->Start();
			}
		}
		SafeRelease(&pMediaSink);		
		CHECK_HR(hr);

		// Update the topology associated to the media session
		CHECK_HR(hr = pSelf->pSession->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, pSelf->pTopologyFull));

		// Update negotiated width and height
		pSelf->nNegWidth = TMEDIA_CONSUMER(pSelf)->video.in.width;
		pSelf->nNegHeight = TMEDIA_CONSUMER(pSelf)->video.in.height;		
	}
	
	// Deliver buffer
	CHECK_HR(hr = pSelf->pSource->CopyVideoBuffer(pSelf->nNegWidth, pSelf->nNegHeight, buffer, size));

bail:
	return SUCCEEDED(hr) ?  0 : -1;
}

static int plugin_win_mf_consumer_video_pause(tmedia_consumer_t* self)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}
	if(!pSelf->bStarted)
	{
		TSK_DEBUG_INFO("MF video producer not started");
		return 0;
	}

	HRESULT hr = MFUtils::PauseSession(pSelf->pSession);

	return SUCCEEDED(hr) ? 0 : -1;
}

static int plugin_win_mf_consumer_video_stop(tmedia_consumer_t* self)
{
	plugin_win_mf_consumer_video_t* pSelf = (plugin_win_mf_consumer_video_t*)self;

	if(!pSelf){
        TSK_DEBUG_ERROR("Invalid parameter");
        return -1;
    }

    HRESULT hr = S_OK;

	// stop EVR watcher
	if(pSelf->pDisplayWatcher) {
		hr = pSelf->pDisplayWatcher->Stop();
	}

    // for the thread
    pSelf->bStarted = false;
    hr = MFUtils::ShutdownSession(pSelf->pSession, NULL); // stop session to wakeup the asynchronous thread
    if(pSelf->ppTread[0]){
        tsk_thread_join(&pSelf->ppTread[0]);
    }
    hr = MFUtils::ShutdownSession(NULL, pSelf->pSource); // stop source to release the camera

	// next start() will be called after prepare()
	return _plugin_win_mf_consumer_video_unprepare(pSelf);
}

static int _plugin_win_mf_consumer_video_unprepare(plugin_win_mf_consumer_video_t* pSelf)
{
	if(!pSelf){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}

	if(pSelf->bStarted) {
		// plugin_win_mf_producer_video_stop(TMEDIA_PRODUCER(pSelf));
		TSK_DEBUG_ERROR("Consumer must be stopped before calling unprepare");
	}

	if(pSelf->pDisplayWatcher) {
		pSelf->pDisplayWatcher->Stop();
	}
    if(pSelf->pSource){
		pSelf->pSource->Shutdown();
		pSelf->pSource = NULL;
    }
    if(pSelf->pSession){
        pSelf->pSession->Shutdown();
		pSelf->pSession = NULL;
    }

	SafeRelease(&pSelf->pDecoder);
    SafeRelease(&pSelf->pSession);
    SafeRelease(&pSelf->pSource);
    SafeRelease(&pSelf->pSinkActivate);
    SafeRelease(&pSelf->pTopologyFull);
	SafeRelease(&pSelf->pTopologyPartial);
	SafeRelease(&pSelf->pOutType);

	if(pSelf->pDisplayWatcher) {
		delete pSelf->pDisplayWatcher;
		pSelf->pDisplayWatcher = NULL;
	}

	pSelf->bPrepared = false;

	return 0;
}


//
//	Media Foundation video consumer object definition
//
/* constructor */
static tsk_object_t* plugin_win_mf_consumer_video_ctor(tsk_object_t * self, va_list * app)
{
	MFUtils::Startup();

	plugin_win_mf_consumer_video_t *pSelf = (plugin_win_mf_consumer_video_t *)self;
	if(pSelf){
		/* init base */
		tmedia_consumer_init(TMEDIA_CONSUMER(pSelf));
		TMEDIA_CONSUMER(pSelf)->video.display.chroma = tmedia_chroma_yuv420p;
		TMEDIA_CONSUMER(pSelf)->decoder.codec_id = tmedia_codec_id_none; // means accept RAW fames

		/* init self */
		// consumer->create_on_ui_thread = tsk_true;
		TMEDIA_CONSUMER(pSelf)->video.fps = 15;
		TMEDIA_CONSUMER(pSelf)->video.display.width = 0; // use codec value
		TMEDIA_CONSUMER(pSelf)->video.display.height = 0; // use codec value
		TMEDIA_CONSUMER(pSelf)->video.display.auto_resize = tsk_true;
	}
	return self;
}
/* destructor */
static tsk_object_t* plugin_win_mf_consumer_video_dtor(tsk_object_t * self)
{ 
	plugin_win_mf_consumer_video_t *pSelf = (plugin_win_mf_consumer_video_t *)self;
	if(pSelf){
		/* stop */
		if(pSelf->bStarted){
			plugin_win_mf_consumer_video_stop(TMEDIA_CONSUMER(pSelf));
		}

		/* deinit base */
		tmedia_consumer_deinit(TMEDIA_CONSUMER(pSelf));
		/* deinit self */
		_plugin_win_mf_consumer_video_unprepare(pSelf);
	}

	return self;
}
/* object definition */
static const tsk_object_def_t plugin_win_mf_consumer_video_def_s = 
{
	sizeof(plugin_win_mf_consumer_video_t),
	plugin_win_mf_consumer_video_ctor, 
	plugin_win_mf_consumer_video_dtor,
	tsk_null, 
};
/* plugin definition*/
static const tmedia_consumer_plugin_def_t plugin_win_mf_consumer_video_plugin_def_s = 
{
	&plugin_win_mf_consumer_video_def_s,
	
	tmedia_video,
	"Media Foundation video consumer",
	
	plugin_win_mf_consumer_video_set,
	plugin_win_mf_consumer_video_prepare,
	plugin_win_mf_consumer_video_start,
	plugin_win_mf_consumer_video_consume,
	plugin_win_mf_consumer_video_pause,
	plugin_win_mf_consumer_video_stop
};
const tmedia_consumer_plugin_def_t *plugin_win_mf_consumer_video_plugin_def_t = &plugin_win_mf_consumer_video_plugin_def_s;

// Run session async thread
static void* TSK_STDCALL RunSessionThread(void *pArg)
{
	plugin_win_mf_consumer_video_t *pSelf = (plugin_win_mf_consumer_video_t *)pArg;
	HRESULT hrStatus = S_OK;
	HRESULT hr = S_OK;
	IMFMediaEvent *pEvent = NULL;
	MediaEventType met;

	TSK_DEBUG_INFO("RunSessionThread (MF video consumer) - ENTER");

	while(pSelf->bStarted){
		CHECK_HR(hr = pSelf->pSession->GetEvent(0, &pEvent));
		CHECK_HR(hr = pEvent->GetStatus(&hrStatus));
		CHECK_HR(hr = pEvent->GetType(&met));
		
		if (FAILED(hrStatus) /*&& hrStatus != MF_E_NO_SAMPLE_TIMESTAMP*/)
		{
			TSK_DEBUG_ERROR("Session error: 0x%x (event id: %d)\n", hrStatus, met);
			hr = hrStatus;
			goto bail;
		}
		if (met == MESessionEnded)
		{
			break;
		}
		SafeRelease(&pEvent);
	}

bail:
	TSK_DEBUG_INFO("RunSessionThread (MF video consumer) - EXIT");

	return NULL;
}

#endif /* PLUGIN_MF_CV_USE_D3D9 */