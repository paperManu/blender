/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/GamePlayer/ghost/GPG_Canvas.cpp
 *  \ingroup player
 */


#include "GPG_Canvas.h"
#include <assert.h>
#include "GHOST_ISystem.h"

GPG_Canvas::GPG_Canvas(GHOST_IWindow* window)
: GPC_Canvas(0, 0), m_window(window), m_use_fbo(false), m_fbo_ready(false)
{
	if (m_window)
	{
		GHOST_Rect bnds;
		m_window->getClientBounds(bnds);
		this->Resize(bnds.getWidth(), bnds.getHeight());
		m_fbo_rect = m_displayarea;
	}

#ifdef WITH_SHMDATA
	m_shmdata_writer = NULL;
	sprintf(m_shmdata_filename, "");
#endif
}


GPG_Canvas::~GPG_Canvas(void)
{
	if (m_use_fbo)
	{
		glDeleteFramebuffers(1, &m_fbo);
		glDeleteTextures(1, &m_fbo_depth);
		glDeleteTextures(1, &m_fbo_color);

#ifdef WITH_SHMDATA
		if (m_shmdata_writer != NULL)
			shmdata_any_writer_close(m_shmdata_writer);
#endif

		glDeleteBuffers(2, m_pbos);
	}
}

void GPG_Canvas::InitializeFbo()
{
	 glGenFramebuffers(1, &m_fbo);
	 glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

	 glGenTextures(1, &m_fbo_depth);
	 glActiveTexture(GL_TEXTURE0);
	 glBindTexture(GL_TEXTURE_2D, m_fbo_depth);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	 glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	 glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, m_fbo_rect.GetRight() + m_fbo_rect.GetLeft(), m_fbo_rect.GetTop() + m_fbo_rect.GetBottom(), 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	 glBindTexture(GL_TEXTURE_2D, 0);
	 glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_fbo_depth, 0);

	 glGenTextures(1, &m_fbo_color);
	 glBindTexture(GL_TEXTURE_2D, m_fbo_color);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	 glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	 glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_fbo_rect.GetRight() + m_fbo_rect.GetBottom(), m_fbo_rect.GetTop() + m_fbo_rect.GetBottom(), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	 glBindTexture(GL_TEXTURE_2D, 0);
	 glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_color, 0);

	 GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	 if (status != GL_FRAMEBUFFER_COMPLETE)
	 {
		  printf("Error initializing offscreen rendering\n");
		  glDeleteFramebuffers(1, &m_fbo);
		  glDeleteTextures(1, &m_fbo_depth);
		  glDeleteTextures(1, &m_fbo_color);
	 }
	 else
		  printf("Offscreen rendering correctly initialized\n");

	 glBindFramebuffer(GL_FRAMEBUFFER, 0);

#ifdef WITH_SHMDATA
	 glGenBuffers(2, m_pbos);
	 for (int i = 0; i < 2; ++i) {
		  glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[i]);
		  glBufferData(GL_PIXEL_PACK_BUFFER, m_fbo_rect.GetWidth() * m_fbo_rect.GetHeight() * 3, 0, GL_STREAM_READ);
	 }
	 glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	 m_pbo_index = 0;
#endif
}

void GPG_Canvas::Init()
{
	if (m_window)
	{
		m_window->setDrawingContextType(GHOST_kDrawingContextTypeOpenGL);
		assert(m_window->getDrawingContextType() == GHOST_kDrawingContextTypeOpenGL);
	}
}

void GPG_Canvas::SetMousePosition(int x, int y)
{
	GHOST_ISystem* system = GHOST_ISystem::getSystem();
	if (system && m_window)
	{
		GHOST_TInt32 gx = (GHOST_TInt32)x;
		GHOST_TInt32 gy = (GHOST_TInt32)y;
		GHOST_TInt32 cx;
		GHOST_TInt32 cy;
		m_window->clientToScreen(gx, gy, cx, cy);
		system->setCursorPosition(cx, cy);
	}
}


void GPG_Canvas::SetMouseState(RAS_MouseState mousestate)
{
	m_mousestate = mousestate;

	if (m_window)
	{
		switch (mousestate)
		{
		case MOUSE_INVISIBLE:
			m_window->setCursorVisibility(false);
			break;
		case MOUSE_WAIT:
			m_window->setCursorShape(GHOST_kStandardCursorWait);
			m_window->setCursorVisibility(true);
			break;
		case MOUSE_NORMAL:
			m_window->setCursorShape(GHOST_kStandardCursorDefault);
			m_window->setCursorVisibility(true);
			break;
		}
	}
}


void GPG_Canvas::SwapBuffers()
{
	if (m_window)
	{
		if (m_use_fbo)
		{
	  	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	
			// Reactivate the back buffer
	  	glDrawBuffer(GL_BACK);
	  	// Now that the rendering has been done to the FBO, we can show it in the window
	  	glBlitFramebuffer(m_fbo_rect.GetLeft(), m_fbo_rect.GetBottom(), m_fbo_rect.GetRight(), m_fbo_rect.GetTop(),
	 											m_displayarea.GetLeft(), m_displayarea.GetBottom(), 640, 480,
	 											GL_COLOR_BUFFER_BIT, GL_LINEAR);

#ifdef WITH_SHMDATA
			// And we can copy it into one of the pbos
			glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[m_pbo_index]);
			GLubyte* gpuPixels = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
			bufferToShmdata((unsigned int*)gpuPixels);
			
			m_pbo_index = (m_pbo_index + 1) % 2;
			glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[m_pbo_index]);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glReadPixels(m_fbo_rect.GetLeft(), m_fbo_rect.GetBottom(), m_fbo_rect.GetWidth(), m_fbo_rect.GetHeight(), GL_RGB, GL_UNSIGNED_BYTE, 0);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#endif
	
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}

		m_window->swapBuffers();
	}
}

void GPG_Canvas::SetSwapInterval(int interval)
{
	if (m_window)
		m_window->setSwapInterval(interval);
}

bool GPG_Canvas::GetSwapInterval(int& intervalOut)
{
	if (m_window)
		return (bool)m_window->getSwapInterval(intervalOut);

	return false;
}

void GPG_Canvas::ResizeWindow(int width, int height)
{
	if (m_window->getState() == GHOST_kWindowStateFullScreen)
	{
		GHOST_ISystem* system = GHOST_ISystem::getSystem();
		GHOST_DisplaySetting setting;
		setting.xPixels = width;
		setting.yPixels = height;
		//XXX allow these to be changed or kept from previous state
		setting.bpp = 32;
		setting.frequency = 60;

		system->updateFullScreen(setting, &m_window);
	}

	m_window->setClientSize(width, height);

	Resize(width, height);
}

void GPG_Canvas::SetFullScreen(bool enable)
{
	if (enable)
		m_window->setState(GHOST_kWindowStateFullScreen);
	else
		m_window->setState(GHOST_kWindowStateNormal);
}

bool GPG_Canvas::GetFullScreen()
{
	return m_window->getState() == GHOST_kWindowStateFullScreen;
}

float GPG_Canvas::GetMouseNormalizedX(int x)
{
	return float(x)/this->GetWidth();
}

float GPG_Canvas::GetMouseNormalizedY(int y)
{
	return float(y)/this->GetHeight();
}

bool GPG_Canvas::BeginDraw()
{
	if (m_use_fbo)
	{
		if (!m_fbo_ready)
		{
			InitializeFbo();
			m_fbo_ready = true;
		}
		SetDrawBuffer();
		return true;
	}
	else
		return true;
}

#ifdef WITH_SHMDATA
void GPG_Canvas::SetSharedMemoryPath(const char *filename)
{
	 if (filename != NULL && strcmp(filename, "") != 0)
		  sprintf(m_shmdata_filename, "%s", filename);
	 else
		  sprintf(m_shmdata_filename, "%s", "/tmp/bge");
}
#endif

#ifdef WITH_SHMDATA
void GPG_Canvas::SetDrawBuffer()
{
	if (m_use_fbo)
	{
		GLenum fboBuffers[1] = {
			GL_COLOR_ATTACHMENT0
		};
	
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
		glDrawBuffers(1, fboBuffers);
	}
}
#endif


#ifdef WITH_SHMDATA
void GPG_Canvas::bufferToShmdata(unsigned int *buffer)
{
	 if (strcmp(m_shmdata_filename, "") == 0)
		  return;

	if (buffer) {
		  if (m_shmdata_writer != NULL && (m_fbo_rect.GetWidth() != m_shmdata_writer_w || m_fbo_rect.GetHeight() != m_shmdata_writer_h))
		  {
				shmdata_any_writer_close(m_shmdata_writer);
				m_shmdata_writer = NULL;
		  }

		  if (m_shmdata_writer == NULL)
		  {
				m_shmdata_writer = shmdata_any_writer_init();

				char buffer[256] = "";
				sprintf(buffer, "video/x-raw-rgb,bpp=%i,endianness=4321,depth=%i,red_mask=16711680,green_mask=65280,blue_mask=255,width=%i,height=%i,framerate=60/1", 24, 24, m_fbo_rect.GetWidth(), m_fbo_rect.GetHeight());
				shmdata_any_writer_set_data_type(m_shmdata_writer, buffer);

				m_shmdata_writer_w = m_fbo_rect.GetWidth();
				m_shmdata_writer_h = m_fbo_rect.GetHeight();

				if (!shmdata_any_writer_set_path(m_shmdata_writer, m_shmdata_filename))
				{
					 shmdata_any_writer_close(m_shmdata_writer);
					 m_shmdata_writer = NULL;
				}
				else
					 shmdata_any_writer_start(m_shmdata_writer);
		  }

		  if (m_shmdata_writer != NULL)
				shmdata_any_writer_push_data(m_shmdata_writer, (void*)buffer, m_fbo_rect.GetWidth() * m_fbo_rect.GetHeight() * 3 * sizeof(char), 0, NULL, NULL);
	}
}
#endif
