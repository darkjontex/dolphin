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
#include "D3DShader.h"
#include "Statistics.h"
#include "Utils.h"
#include "Profiler.h"
#include "VideoConfig.h"
#include "PixelShaderGen.h"
#include "PixelShaderManager.h"
#include "PixelShaderCache.h"
#include "VertexLoader.h"
#include "BPMemory.h"
#include "XFMemory.h"

#include "debugger/debugger.h"

PixelShaderCache::PSCache PixelShaderCache::PixelShaders;
const PixelShaderCache::PSCacheEntry *PixelShaderCache::last_entry;
static float lastPSconstants[C_COLORMATRIX+16][4];

static LPDIRECT3DPIXELSHADER9 s_ColorMatrixProgram = 0;
static LPDIRECT3DPIXELSHADER9 s_ColorCopyProgram = 0;
static LPDIRECT3DPIXELSHADER9 s_DepthMatrixProgram = 0;


LPDIRECT3DPIXELSHADER9 PixelShaderCache::GetColorMatrixProgram()
{
	return s_ColorMatrixProgram;
}

LPDIRECT3DPIXELSHADER9 PixelShaderCache::GetDepthMatrixProgram()
{
	return s_DepthMatrixProgram;
}

LPDIRECT3DPIXELSHADER9 PixelShaderCache::GetColorCopyProgram()
{
	return s_ColorCopyProgram;
}

void SetPSConstant4f(int const_number, float f1, float f2, float f3, float f4)
{
	if( lastPSconstants[const_number][0] != f1 || lastPSconstants[const_number][1] != f2 ||
		lastPSconstants[const_number][2] != f3 || lastPSconstants[const_number][3] != f4 )
	{
		const float f[4] = {f1, f2, f3, f4};
		D3D::dev->SetPixelShaderConstantF(const_number, f, 1);
		lastPSconstants[const_number][0] = f1;
		lastPSconstants[const_number][1] = f2;
		lastPSconstants[const_number][2] = f3;
		lastPSconstants[const_number][3] = f4;
	}
}

void SetPSConstant4fv(int const_number, const float *f)
{
	if( lastPSconstants[const_number][0] != f[0] || lastPSconstants[const_number][1] != f[1] ||
		lastPSconstants[const_number][2] != f[2] || lastPSconstants[const_number][3] != f[3] )
	{
		D3D::dev->SetPixelShaderConstantF(const_number, f, 1);
		lastPSconstants[const_number][0] = f[0];
		lastPSconstants[const_number][1] = f[1];
		lastPSconstants[const_number][2] = f[2];
		lastPSconstants[const_number][3] = f[3];
	}
}

void PixelShaderCache::Init()
{
	char pmatrixprog[1024];
	sprintf(pmatrixprog,"uniform sampler samp0 : register(s0);\n"
						"uniform float4 cColMatrix[5] : register(c%d);\n"
						"void main(\n"
						"out float4 ocol0 : COLOR0,\n"
						" in float3 uv0 : TEXCOORD0){\n"
						"float4 texcol = tex2D(samp0,uv0.xy);\n"
						"ocol0 = float4(dot(texcol,cColMatrix[0]),dot(texcol,cColMatrix[1]),dot(texcol,cColMatrix[2]),dot(texcol,cColMatrix[3])) + cColMatrix[4];\n"						
						"}\n",C_COLORMATRIX);
	char pcopyprog[1024];
	sprintf(pcopyprog,"uniform sampler samp0 : register(s0);\n"
						"void main(\n"
						"out float4 ocol0 : COLOR0,\n"
						" in float3 uv0 : TEXCOORD0){\n"
						"ocol0 = tex2D(samp0,uv0.xy);\n"						
						"}\n");
	char pdmatrixprog[1024];
	sprintf(pdmatrixprog,"uniform sampler samp0 : register(s0);\n"
						"uniform float4 cColMatrix[5] : register(c%d);\n"
						"void main(\n"
						"out float4 ocol0 : COLOR0,\n"
						" in float3 uv0 : TEXCOORD0){\n"
						"float4 texcol = tex2D(samp0,uv0.xy);\n"
						"float4 EncodedDepth = frac(texcol.r * float4(1.0f,255.0f,255.0f*255.0f,255.0f*255.0f*255.0f));\n"
						"EncodedDepth -= EncodedDepth.raag * float4(0.0f,1.0f/255.0f,1.0f/255.0f,0.0f);\n"
						"texcol = float4(EncodedDepth.rgb,1.0f);\n"
						"ocol0 = float4(dot(texcol,cColMatrix[0]),dot(texcol,cColMatrix[1]),dot(texcol,cColMatrix[2]),dot(texcol,cColMatrix[3])) + cColMatrix[4];\n"						
						"}\n",C_COLORMATRIX);

	s_ColorMatrixProgram = D3D::CompilePixelShader(pmatrixprog, (int)strlen(pmatrixprog));
	s_ColorCopyProgram = D3D::CompilePixelShader(pcopyprog, (int)strlen(pcopyprog));
	s_DepthMatrixProgram = D3D::CompilePixelShader(pdmatrixprog, (int)strlen(pdmatrixprog));
	Clear();
}

void PixelShaderCache::Clear()
{
	PSCache::iterator iter = PixelShaders.begin();
	for (; iter != PixelShaders.end(); iter++)
		iter->second.Destroy();
	PixelShaders.clear(); 

	for (int i = 0; i < (C_COLORMATRIX + 16) * 4; i++)
		lastPSconstants[i/4][i%4] = -100000000.0f;
	memset(&last_pixel_shader_uid, 0xFF, sizeof(last_pixel_shader_uid));
}

void PixelShaderCache::Shutdown()
{
	if(s_ColorMatrixProgram)
		s_ColorMatrixProgram->Release();
	s_ColorMatrixProgram = NULL;
	if(s_ColorCopyProgram)
		s_ColorCopyProgram->Release();
	s_ColorCopyProgram=NULL;
	if(s_DepthMatrixProgram)
			s_DepthMatrixProgram->Release();
	s_DepthMatrixProgram = NULL;
	Clear();
}

bool PixelShaderCache::SetShader(bool dstAlpha)
{
	DVSTARTPROFILE();

	PIXELSHADERUID uid;
	GetPixelShaderId(uid, PixelShaderManager::GetTextureMask(), dstAlpha);
	if (uid == last_pixel_shader_uid && PixelShaders[uid].frameCount == frameCount)
	{
		PSCache::const_iterator iter = PixelShaders.find(uid);
		if (iter != PixelShaders.end() && iter->second.shader)
			return true;
		else
			return false;
	}

	memcpy(&last_pixel_shader_uid, &uid, sizeof(PIXELSHADERUID));
	
	PSCache::iterator iter;
	iter = PixelShaders.find(uid);
	if (iter != PixelShaders.end())
	{
		iter->second.frameCount = frameCount;
		const PSCacheEntry &entry = iter->second;
		last_entry = &entry;
		

		DEBUGGER_PAUSE_AT(NEXT_PIXEL_SHADER_CHANGE,true);

		if (entry.shader)
		{
			D3D::dev->SetPixelShader(entry.shader);
			return true;
		}
		else
			return false;
	}

	const char *code = GeneratePixelShader(PixelShaderManager::GetTextureMask(), dstAlpha, (D3D::GetCaps().NumSimultaneousRTs > 1)? 1 : 2);
	LPDIRECT3DPIXELSHADER9 shader = D3D::CompilePixelShader(code, (int)strlen(code));

	// Make an entry in the table
	PSCacheEntry newentry;
	newentry.shader = shader;
	newentry.frameCount = frameCount;
#if defined(_DEBUG) || defined(DEBUGFAST)
	newentry.code = code;
#endif
	PixelShaders[uid] = newentry;
	last_entry = &PixelShaders[uid];

	INCSTAT(stats.numPixelShadersCreated);
	SETSTAT(stats.numPixelShadersAlive, (int)PixelShaders.size());
	if (shader)
	{
		D3D::dev->SetPixelShader(shader);
		return true;
	}
	
	if (g_ActiveConfig.bShowShaderErrors)
	{
		PanicAlert("Failed to compile Pixel Shader:\n\n%s", code);
	}
	return false;
}


void PixelShaderCache::Cleanup()
{
	/*
	PSCache::iterator iter;
	iter = PixelShaders.begin();
	while (iter != PixelShaders.end())
	{
		PSCacheEntry &entry = iter->second;
		if (entry.frameCount < frameCount - 1400)
		{
			entry.Destroy();
			iter = PixelShaders.erase(iter);
		}
		else
		{
			iter++;
		}
	}
	SETSTAT(stats.numPixelShadersAlive, (int)PixelShaders.size());
	*/
}

#if defined(_DEBUG) || defined(DEBUGFAST)
std::string PixelShaderCache::GetCurrentShaderCode()
{
	if (last_entry)
		return last_entry->code;
	else
		return "(no shader)\n";
}
#endif