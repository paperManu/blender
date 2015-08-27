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

/** \file KX_BlenderCanvas.h
 *  \ingroup blroutines
 */

#ifndef __KX_BLENDERKANVAS_H__
#define __KX_BLENDERKANVAS_H__

#ifdef WIN32
#include <windows.h>
#endif

#include <vector>

#include "RAS_ICanvas.h"
#include "RAS_Rect.h"

#ifdef WITH_CXX_GUARDEDALLOC
#include "MEM_guardedalloc.h"
#endif

#ifdef WITH_SHMDATA
#include <shmdata/any-data-writer.h>
#endif

struct ARegion;
struct wmWindow;
struct wmWindowManager;

/**
 * 2D Blender device context abstraction. 
 * The connection from 3d rendercontext to 2d Blender surface embedding.
 */

class KX_BlenderCanvas : public RAS_ICanvas
{
private:
	/**
	 * Rect that defines the area used for rendering,
	 * relative to the context */
	RAS_Rect m_displayarea;
	int m_viewport[4];

public:
	/* Construct a new canvas.
	 * 
	 * \param area The Blender ARegion to run the game within.
	 */
	KX_BlenderCanvas(struct wmWindowManager *wm, struct wmWindow* win, RAS_Rect &rect, struct ARegion* ar);
	~KX_BlenderCanvas();

		void 
	Init(
	);
	
		void 
	SwapBuffers(
	);

		void
	SetSwapInterval(
		int interval
	);

		bool
	GetSwapInterval(
		int &intervalOut
	);

	void GetDisplayDimensions(int &width, int &height);

		void 
	ResizeWindow(
		int width,
		int height
	);

		void
	SetFullScreen(
		bool enable
	);

		bool
	GetFullScreen();

		void
	BeginFrame(
	);

		void 
	EndFrame(
	);

		void 
	ClearColor(
		float r,
		float g,
		float b,
		float a
	);

		void 
	ClearBuffer(
		int type
	);

		int 
	GetWidth(
	) const;

		int 
	GetHeight(
	) const;

		int
	GetMouseX(int x
	);

		int
	GetMouseY(int y
	);

		float
	GetMouseNormalizedX(int x
	);

		float
	GetMouseNormalizedY(int y
	);

	const
		RAS_Rect &
	GetDisplayArea(
	) const {
		return m_displayarea;
	};

		void
	SetDisplayArea(RAS_Rect *rect
	) {
		m_displayarea= *rect;
	};

  virtual
	  void
	SetRenderingResolution(
	  int w, int h
	) {
	  m_use_fbo = true;
	  m_fbo_rect.SetRight(w + m_fbo_rect.GetLeft());
	  m_fbo_rect.SetTop(h + m_fbo_rect.GetBottom());
	}

		RAS_Rect &
	GetWindowArea(
	);

		void
	SetViewPort(
		int x1, int y1,
		int x2, int y2
	);

		void
	UpdateViewPort(
		int x1, int y1,
		int x2, int y2
	);

		const int*
	GetViewPort();

		void 
	SetMouseState(
		RAS_MouseState mousestate
	);

		void 
	SetMousePosition(
		int x,
		int y
	);

		void 
	MakeScreenShot(
		const char* filename
	);

#ifdef WITH_SHMDATA
		  void
	 SetSharedMemoryPath(
		  const char* filename
	 );
#endif
	
		bool 
	BeginDraw(
	);

		void 
	EndDraw(
	);

private:
	/** Blender area the game engine is running within */
	struct wmWindowManager *m_wm;
	struct wmWindow* m_win;
	RAS_Rect	m_frame_rect;
	RAS_Rect 	m_area_rect;
	int			m_area_left;
	int			m_area_top;
	int			m_frame;

	 /** Framebuffer object related stuff */
	 bool m_use_fbo, m_fbo_ready;
	 GLuint m_fbo;
	 GLuint m_fbo_depth;
	 GLuint m_fbo_color;
	 RAS_Rect m_fbo_rect;
   std::vector<GLenum> m_draw_buffer_stack;

#ifdef WITH_SHMDATA
	 /** PBO / Copy to shmdata related */
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

#ifdef WITH_CXX_GUARDEDALLOC
	MEM_CXX_CLASS_ALLOC_FUNCS("GE:KX_BlenderCanvas")
#endif
};

#endif  /* __KX_BLENDERKANVAS_H__ */
