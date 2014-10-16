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

/** \file GPG_Canvas.h
 *  \ingroup player
 */

#ifndef __GPG_CANVAS_H__
#define __GPG_CANVAS_H__

#ifdef WIN32
#pragma warning (disable:4786)
#endif  /* WIN32 */

#include "GPC_Canvas.h"

#include "GHOST_IWindow.h"

#ifdef WITH_SHMDATA
#include <shmdata/any-data-writer.h>
#endif


class GPG_Canvas : public GPC_Canvas
{
protected:
	/** GHOST window. */
	GHOST_IWindow* m_window;

	bool m_use_fbo, m_fbo_ready;
	GLuint m_fbo;
	GLuint m_fbo_depth;
	GLuint m_fbo_color;
	RAS_Rect m_fbo_rect;
	std::vector<GLenum> m_draw_buffer_stack;

#ifdef WITH_SHMDATA
	shmdata_any_writer_t *m_shmdata_writer;
	char m_shmdata_filename[256];
	int m_shmdata_writer_w, m_shmdata_writer_h;
	bool m_copy_to_shmdata;

	GLuint m_pbos[2];
	int m_pbo_index;

	void SetDrawBuffer();
	void bufferToShmdata(unsigned int *buffer);
#endif

	void InitializeFbo();

public:
	GPG_Canvas(GHOST_IWindow* window);
	virtual ~GPG_Canvas(void);

	virtual void Init(void);
	virtual void SetMousePosition(int x, int y);
	virtual void SetMouseState(RAS_MouseState mousestate);
	virtual void SwapBuffers();
	virtual void SetSwapInterval(int interval);
	virtual bool GetSwapInterval(int& intervalOut);

	virtual int GetWidth() const { return m_fbo_rect.GetWidth(); }
	virtual int GetHeight() const { return m_fbo_rect.GetHeight(); }

	virtual int GetMouseX(int x) { return x; }
	virtual int GetMouseY(int y) { return y; }
	virtual float GetMouseNormalizedX(int x);
	virtual float GetMouseNormalizedY(int y);

	virtual void ResizeWindow(int width, int height);
	virtual void SetFullScreen(bool enable);
	virtual bool GetFullScreen();
	virtual void SetRenderingResolution(int w, int h) {
		m_use_fbo = true;
		m_fbo_rect.SetRight(w + m_fbo_rect.GetLeft());
		m_fbo_rect.SetTop(h + m_fbo_rect.GetBottom());
	}

	bool BeginDraw();
	void EndDraw() {};

#ifdef WITH_SHMDATA
	void SetSharedMemoryPath(const char* filename);
#endif
};

#endif  /* __GPG_CANVAS_H__ */
