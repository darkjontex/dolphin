// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "D3DBase.h"
#include "Render.h"
#include "FramebufferManager.h"
#include "VideoConfig.h"

namespace FBManager
{

static LPDIRECT3DTEXTURE9 s_efb_color_texture;//Texture thats contains the color data of the render target
static LPDIRECT3DTEXTURE9 s_efb_colorRead_texture;//1 pixel texture for temporal data store
static LPDIRECT3DTEXTURE9 s_efb_depth_texture;//Texture thats contains the depth data of the render target
static LPDIRECT3DTEXTURE9 s_efb_depthRead_texture;//1 pixel texture for temporal data store

static LPDIRECT3DSURFACE9 s_efb_depth_surface;//Depth Surface
static LPDIRECT3DSURFACE9 s_efb_depthColor_surface;//Depth, color encoded Surface
static LPDIRECT3DSURFACE9 s_efb_color_surface;//Color Surface
static LPDIRECT3DSURFACE9 s_efb_color_ReadBuffer;//Surface 0 of s_efb_colorRead_texture
static LPDIRECT3DSURFACE9 s_efb_depth_ReadBuffer;//Surface 0 of s_efb_depthRead_texture

static LPDIRECT3DSURFACE9 s_efb_color_OffScreenReadBuffer;//System memory Surface that can be locked to retriebe the data
static LPDIRECT3DSURFACE9 s_efb_depth_OffScreenReadBuffer;//System memory Surface that can be locked to retriebe the data


static D3DFORMAT s_efb_color_surface_Format;//Format of the color Surface
static D3DFORMAT s_efb_depth_surface_Format;//Format of the Depth color encoded Surface
#undef CHECK
#define CHECK(hr,Message) if (FAILED(hr)) { PanicAlert(__FUNCTION__ " FAIL: %s" ,Message); }



LPDIRECT3DSURFACE9 GetEFBColorRTSurface() 
{ 	
	return s_efb_color_surface; 
}
LPDIRECT3DSURFACE9 GetEFBDepthRTSurface() 
{ 
	return s_efb_depth_surface; 
}

LPDIRECT3DSURFACE9 GetEFBDepthEncodedSurface() 
{ 	
	return s_efb_depthColor_surface; 
}

LPDIRECT3DSURFACE9 GetEFBColorOffScreenRTSurface() 
{ 
	return s_efb_color_OffScreenReadBuffer; 
}
LPDIRECT3DSURFACE9 GetEFBDepthOffScreenRTSurface() 
{ 
	return s_efb_depth_OffScreenReadBuffer; 
}

LPDIRECT3DSURFACE9 GetEFBColorReadSurface() 
{ 
	
	return s_efb_color_ReadBuffer; 
}
LPDIRECT3DSURFACE9 GetEFBDepthReadSurface() 
{
	
	return s_efb_depth_ReadBuffer; 
}


D3DFORMAT GetEFBDepthRTSurfaceFormat(){return s_efb_depth_surface_Format;}
D3DFORMAT GetEFBColorRTSurfaceFormat(){return s_efb_color_surface_Format;}

LPDIRECT3DTEXTURE9 GetEFBColorTexture(const EFBRectangle& sourceRc)
{
	return s_efb_color_texture;
}


LPDIRECT3DTEXTURE9 GetEFBDepthTexture(const EFBRectangle &sourceRc)
{
	return s_efb_depth_texture;
}

void Create()
{
	
	// Simplest possible setup to start with.
	int target_width = Renderer::GetTargetWidth();
	int target_height = Renderer::GetTargetHeight();
	s_efb_color_surface_Format = D3DFMT_A8R8G8B8;
	//get the framebuffer texture
	HRESULT hr = D3D::dev->CreateTexture(target_width, target_height, 1, D3DUSAGE_RENDERTARGET, s_efb_color_surface_Format,
		                                 D3DPOOL_DEFAULT, &s_efb_color_texture, NULL);
	if(s_efb_color_texture)
	{
		s_efb_color_texture->GetSurfaceLevel(0,&s_efb_color_surface);
	}	
	CHECK(hr,"Create Color Texture");
	hr = D3D::dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, s_efb_color_surface_Format,
		                                  D3DPOOL_DEFAULT, &s_efb_colorRead_texture, NULL);
	CHECK(hr,"Create Color Read Texture");
	if(s_efb_colorRead_texture)
	{
		s_efb_colorRead_texture->GetSurfaceLevel(0,&s_efb_color_ReadBuffer);
	}
	//create an offscreen surface that we can lock to retrieve the data
	hr = D3D::dev->CreateOffscreenPlainSurface(1, 1, s_efb_color_surface_Format, D3DPOOL_SYSTEMMEM, &s_efb_color_OffScreenReadBuffer, NULL );
	CHECK(hr,"Create Color offScreen Surface");	
	
	//Select Zbuffer format supported by hadware.
	if (g_ActiveConfig.bEFBAccessEnable)
	{
		if(D3D::GetCaps().NumSimultaneousRTs > 1)	
		{
			hr = D3D::dev->CreateDepthStencilSurface(target_width, target_height, D3DFMT_D24X8,
												 D3DMULTISAMPLE_NONE, 0, FALSE, &s_efb_depth_surface, NULL);
			CHECK(hr,"CreateDepthStencilSurface");
			D3DFORMAT *DepthTexFormats = new D3DFORMAT[3];
			DepthTexFormats[0] = D3DFMT_R32F;		
			DepthTexFormats[1] = D3DFMT_A8R8G8B8;

			for(int i = 0;i<2;i++)
			{
				s_efb_depth_surface_Format = DepthTexFormats[i];
				//get the framebuffer Depth texture
				hr = D3D::dev->CreateTexture(target_width, target_height, 1, D3DUSAGE_RENDERTARGET, s_efb_depth_surface_Format,
												 D3DPOOL_DEFAULT, &s_efb_depth_texture, NULL);
				if (!FAILED(hr)) break;
			}			
			CHECK(hr,"Depth Color Texture");
			//get the Surface
			if(s_efb_depth_texture)
			{
				s_efb_depth_texture->GetSurfaceLevel(0,&s_efb_depthColor_surface);
			}
  			//create a one pixel texture to work as a buffer for peeking
			hr = D3D::dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, s_efb_depth_surface_Format,
												  D3DPOOL_DEFAULT, &s_efb_depthRead_texture, NULL);
			CHECK(hr,"Create Depth Read texture");
			if(s_efb_depthRead_texture)
			{
				s_efb_depthRead_texture->GetSurfaceLevel(0,&s_efb_depth_ReadBuffer);
			}
			//create an offscreen surface that we can lock to retrieve the data
			hr = D3D::dev->CreateOffscreenPlainSurface(1, 1, s_efb_depth_surface_Format, D3DPOOL_SYSTEMMEM, &s_efb_depth_OffScreenReadBuffer, NULL );
			CHECK(hr,"Create Depth offScreen Surface");
			delete [] DepthTexFormats;
		}
		else
		{
			//depth format in prefered order
			D3DFORMAT *DepthTexFormats = new D3DFORMAT[3];
			DepthTexFormats[0] = D3DFMT_D32F_LOCKABLE;		
			DepthTexFormats[1] = D3DFMT_D16_LOCKABLE;
			DepthTexFormats[2] = D3DFMT_D24X8;		
			for(int i = 0;i<3;i++)
			{
				s_efb_depth_surface_Format = DepthTexFormats[i];
				hr = D3D::dev->CreateDepthStencilSurface(target_width, target_height, s_efb_depth_surface_Format,
												 D3DMULTISAMPLE_NONE, 0, FALSE, &s_efb_depth_surface, NULL);
				if (!FAILED(hr)) break;
			}		
			s_efb_depth_ReadBuffer = s_efb_depth_surface;
			s_efb_depth_OffScreenReadBuffer = s_efb_depth_surface;
			//ULTRAAAAAAAAAAA ugly hack when no depth textures are supported
			s_efb_depthColor_surface = s_efb_color_surface;
			CHECK(hr,"CreateDepthStencilSurface");
			delete [] DepthTexFormats;
		}
	}
	else
	{
		s_efb_depth_surface_Format = D3DFMT_D24X8;
		hr = D3D::dev->CreateDepthStencilSurface(target_width, target_height, s_efb_depth_surface_Format,
											 D3DMULTISAMPLE_NONE, 0, FALSE, &s_efb_depth_surface, NULL);
		CHECK(hr,"CreateDepthStencilSurface");
		//ULTRAAAAAAAAAAA ugly hack when no depth textures are supported
		s_efb_depthColor_surface = s_efb_color_surface;
	}	
}

void Destroy()
{
	
if(s_efb_depth_surface)
s_efb_depth_surface->Release();
s_efb_depth_surface=NULL;

if(s_efb_depthColor_surface)
s_efb_depthColor_surface->Release();
s_efb_depthColor_surface=NULL;

if(s_efb_color_surface)
s_efb_color_surface->Release();
s_efb_color_surface=NULL;

if(s_efb_color_ReadBuffer)
s_efb_color_ReadBuffer->Release();
s_efb_color_ReadBuffer=NULL;

if(s_efb_depth_ReadBuffer)
s_efb_depth_ReadBuffer->Release();
s_efb_depth_ReadBuffer=NULL;

if(s_efb_color_OffScreenReadBuffer)
s_efb_color_OffScreenReadBuffer->Release();
s_efb_color_OffScreenReadBuffer=NULL;

if(s_efb_depth_OffScreenReadBuffer)
s_efb_depth_OffScreenReadBuffer->Release();
s_efb_depth_OffScreenReadBuffer=NULL;

if(s_efb_color_texture)
	s_efb_color_texture->Release();
s_efb_color_texture=NULL;

if(s_efb_colorRead_texture)
s_efb_colorRead_texture->Release();
s_efb_colorRead_texture=NULL;

if(s_efb_depth_texture)
s_efb_depth_texture->Release();
s_efb_depth_texture=NULL;

if(s_efb_depthRead_texture)
s_efb_depthRead_texture->Release();
s_efb_depthRead_texture=NULL;

}

}  // namespace