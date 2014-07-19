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

/** \file gameengine/BlenderRoutines/KX_BlenderCanvasFbo.cpp
 *  \ingroup blroutines
 */

#include <GL/glew.h>

#include "MEM_guardedalloc.h"

#include "KX_BlenderCanvasFbo.h"

#include "DNA_image_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_windowmanager_types.h"

#include "BKE_image.h"
#include "BKE_global.h"
#include "BKE_main.h"

#include "BLI_path_util.h"
#include "BLI_string.h"

#include <assert.h>
#include <cstring>


extern "C" {
#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"
#include "WM_api.h"
#include "wm_cursors.h"
#include "wm_window.h"
}

KX_BlenderCanvasFbo::KX_BlenderCanvasFbo(wmWindowManager *wm, wmWindow *win, RAS_Rect &rect, struct ARegion *ar) :
m_wm(wm),
m_win(win),
m_frame_rect(rect)
{
	// initialize area so that it's available for game logic on frame 1 (ImageViewport)
	m_area_rect = rect;
	// area boundaries needed for mouse coordinates in Letterbox framing mode
	m_area_left = ar->winrct.xmin;
	m_area_top = ar->winrct.ymax;

	glGetIntegerv(GL_VIEWPORT, (GLint *)m_viewport);

    InitializeFbo();

#ifdef WITH_SHMDATA
    m_shmdata_writer = NULL;
    sprintf(m_shmdata_filename, "");
#endif
}

KX_BlenderCanvasFbo::~KX_BlenderCanvasFbo()
{
#ifdef WITH_SHMDATA
    if (m_shmdata_writer != NULL)
        shmdata_any_writer_close(m_shmdata_writer);
#endif
}

void KX_BlenderCanvasFbo::InitializeFbo()
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, m_frame_rect.GetRight(), m_frame_rect.GetTop(), 0, GL_DEPTH_STENCIL, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_fbo_depth, 0);

    glGenTextures(1, &m_fbo_color);
    glBindTexture(GL_TEXTURE_2D, m_fbo_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_frame_rect.GetRight(), m_frame_rect.GetTop(), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fbo_color, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("Error initializing FBO\n");
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteTextures(1, &m_fbo_depth);
        glDeleteTextures(1, &m_fbo_color);
    }
    else
        printf("FBO correctly initialized\n");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void KX_BlenderCanvasFbo::Init()
{
	glDepthFunc(GL_LEQUAL);
}


void KX_BlenderCanvasFbo::SwapBuffers()
{
	wm_window_swap_buffers(m_win);

#ifdef WITH_SHMDATA
    FrontBufferToShmdata();
#endif
}

void KX_BlenderCanvasFbo::SetSwapInterval(int interval)
{
	wm_window_set_swap_interval(m_win, interval);
}

int	KX_BlenderCanvasFbo::GetSwapInterval()
{
	return wm_window_get_swap_interval(m_win);
}

void KX_BlenderCanvasFbo::ResizeWindow(int width, int height)
{
	// Not implemented for the embedded player
}

void KX_BlenderCanvasFbo::SetFullScreen(bool enable)
{
	// Not implemented for the embedded player
}

bool KX_BlenderCanvasFbo::GetFullScreen()
{
	// Not implemented for the embedded player
	return false;
}

bool KX_BlenderCanvasFbo::BeginDraw()
{
	// in case of multi-window we need to ensure we are drawing to the correct
	// window always, because it may change in window event handling
	wm_window_make_drawable(m_wm, m_win);

    GLenum fboBuffers[1] = {
        GL_COLOR_ATTACHMENT0
    };

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
    glDrawBuffers(1, fboBuffers);

	return true;
}


void KX_BlenderCanvasFbo::EndDraw()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Now that the rendering has been done to the FBO, we can show it in the window
    glDrawBuffer(GL_BACK);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
    glBlitFramebuffer(m_frame_rect.GetLeft(), m_frame_rect.GetBottom(), m_frame_rect.GetRight(), m_frame_rect.GetTop(),
                      m_frame_rect.GetLeft(), m_frame_rect.GetBottom(), m_frame_rect.GetRight(), m_frame_rect.GetTop(),
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_LINEAR);
}

void KX_BlenderCanvasFbo::BeginFrame()
{
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
}


void KX_BlenderCanvasFbo::EndFrame()
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glDisable(GL_FOG);
}



void KX_BlenderCanvasFbo::ClearColor(float r,float g,float b,float a)
{
	glClearColor(r,g,b,a);
}



void KX_BlenderCanvasFbo::ClearBuffer(int type)
{
	int ogltype = 0;

	if (type & RAS_ICanvas::COLOR_BUFFER )
		ogltype |= GL_COLOR_BUFFER_BIT;

	if (type & RAS_ICanvas::DEPTH_BUFFER )
		ogltype |= GL_DEPTH_BUFFER_BIT;
	glClear(ogltype);
}

int KX_BlenderCanvasFbo::GetWidth(
) const {
	return m_frame_rect.GetWidth();
}

int KX_BlenderCanvasFbo::GetHeight(
) const {
	return m_frame_rect.GetHeight();
}

int KX_BlenderCanvasFbo::GetMouseX(int x)
{
	int left = GetWindowArea().GetLeft();
	return x - (left - m_area_left);
}

int KX_BlenderCanvasFbo::GetMouseY(int y)
{
	int top = GetWindowArea().GetTop();
	return y - (m_area_top - top);
}

float KX_BlenderCanvasFbo::GetMouseNormalizedX(int x)
{
	int can_x = GetMouseX(x);
	return float(can_x)/this->GetWidth();
}

float KX_BlenderCanvasFbo::GetMouseNormalizedY(int y)
{
	int can_y = GetMouseY(y);
	return float(can_y)/this->GetHeight();
}

RAS_Rect &
KX_BlenderCanvasFbo::
GetWindowArea(
) {
	return m_area_rect;
}

	void
KX_BlenderCanvasFbo::
SetViewPort(
	int x1, int y1,
	int x2, int y2
) {
	/* x1 and y1 are the min pixel coordinate (e.g. 0)
	 * x2 and y2 are the max pixel coordinate
	 * the width,height is calculated including both pixels
	 * therefore: max - min + 1
	 */
	int vp_width = (x2 - x1) + 1;
	int vp_height = (y2 - y1) + 1;
	int minx = m_frame_rect.GetLeft();
	int miny = m_frame_rect.GetBottom();

	m_area_rect.SetLeft(minx + x1);
	m_area_rect.SetBottom(miny + y1);
	m_area_rect.SetRight(minx + x2);
	m_area_rect.SetTop(miny + y2);

	m_viewport[0] = minx+x1;
	m_viewport[1] = miny+y1;
	m_viewport[2] = vp_width;
	m_viewport[3] = vp_height;

	glViewport(minx + x1, miny + y1, vp_width, vp_height);
	glScissor(minx + x1, miny + y1, vp_width, vp_height);
}

	void
KX_BlenderCanvasFbo::
UpdateViewPort(
	int x1, int y1,
	int x2, int y2
) {
	m_viewport[0] = x1;
	m_viewport[1] = y1;
	m_viewport[2] = x2;
	m_viewport[3] = y2;
}

	const int*
KX_BlenderCanvasFbo::
GetViewPort() {
#ifdef DEBUG
	// If we're in a debug build, we might as well make sure our values don't differ
	// from what the gpu thinks we have. This could lead to nasty, hard to find bugs.
	int viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	assert(viewport[0] == m_viewport[0]);
	assert(viewport[1] == m_viewport[1]);
	assert(viewport[2] == m_viewport[2]);
	assert(viewport[3] == m_viewport[3]);
#endif

	return m_viewport;
}

void KX_BlenderCanvasFbo::SetMouseState(RAS_MouseState mousestate)
{
	m_mousestate = mousestate;

	switch (mousestate)
	{
	case MOUSE_INVISIBLE:
		{
			WM_cursor_set(m_win, CURSOR_NONE);
			break;
		}
	case MOUSE_WAIT:
		{
			WM_cursor_set(m_win, CURSOR_WAIT);
			break;
		}
	case MOUSE_NORMAL:
		{
			WM_cursor_set(m_win, CURSOR_STD);
			break;
		}
	default:
		{
		}
	}
}



//	(0,0) is top left, (width,height) is bottom right
void KX_BlenderCanvasFbo::SetMousePosition(int x,int y)
{
	int winX = m_frame_rect.GetLeft();
	int winY = m_frame_rect.GetBottom();
	int winH = m_frame_rect.GetHeight();
	
	WM_cursor_warp(m_win, winX + x, winY + (winH-y));
}


/* get shot from frontbuffer sort of a copy from screendump.c */
static unsigned int *screenshot(ScrArea *curarea, int *dumpsx, int *dumpsy)
{
	int x=0, y=0;
	unsigned int *dumprect= NULL;

	x= curarea->totrct.xmin;
	y= curarea->totrct.ymin;
	*dumpsx= curarea->totrct.xmax-x;
	*dumpsy= curarea->totrct.ymax-y;

	if (*dumpsx && *dumpsy) {

		dumprect= (unsigned int *)MEM_mallocN(sizeof(int) * (*dumpsx) * (*dumpsy), "dumprect");
		glReadBuffer(GL_FRONT);
		glReadPixels(x, y, *dumpsx, *dumpsy, GL_RGBA, GL_UNSIGNED_BYTE, dumprect);
		glFinish();
		glReadBuffer(GL_BACK);
	}

	return dumprect;
}

#ifdef WITH_SHMDATA
void KX_BlenderCanvasFbo::FrontBufferToShmdata()
{
    if (strcmp(m_shmdata_filename, "") == 0)
        return;

	ScrArea area_dummy= {0};
	unsigned int *dumprect;
	int dumpsx, dumpsy;

	area_dummy.totrct.xmin = m_frame_rect.GetLeft();
	area_dummy.totrct.xmax = m_frame_rect.GetRight();
	area_dummy.totrct.ymin = m_frame_rect.GetBottom();
	area_dummy.totrct.ymax = m_frame_rect.GetTop();

	dumprect = screenshot(&area_dummy, &dumpsx, &dumpsy);

	if (dumprect) {
        if (m_shmdata_writer != NULL && (dumpsx != m_shmdata_writer_w || dumpsy != m_shmdata_writer_h))
        {
            shmdata_any_writer_close(m_shmdata_writer);
            m_shmdata_writer = NULL;
        }

        if (m_shmdata_writer == NULL)
        {
            m_shmdata_writer = shmdata_any_writer_init();

            char buffer[256] = "";
            sprintf(buffer, "video/x-raw-rgb,bpp=%i,endianness=4321,depth=%i,red_mask=-16777216,green_mask=16711680,blue_mask=65280,width=%i,height=%i,framerate=60/1", 32, 32, dumpsx, dumpsy);
            shmdata_any_writer_set_data_type(m_shmdata_writer, buffer);

            m_shmdata_writer_w = dumpsx;
            m_shmdata_writer_h = dumpsy;

            if (!shmdata_any_writer_set_path(m_shmdata_writer, m_shmdata_filename))
            {
                shmdata_any_writer_close(m_shmdata_writer);
                m_shmdata_writer = NULL;
            }
            else
                shmdata_any_writer_start(m_shmdata_writer);
        }

        if (m_shmdata_writer != NULL)
            shmdata_any_writer_push_data(m_shmdata_writer, (void*)dumprect, dumpsx * dumpsy * 4 * sizeof(char), 0, NULL, NULL);

        MEM_freeN(dumprect);
	}
}
#endif

void KX_BlenderCanvasFbo::MakeScreenShot(const char *filename)
{
	ScrArea area_dummy= {0};
	bScreen *screen = m_win->screen;
	unsigned int *dumprect;
	int dumpsx, dumpsy;

	area_dummy.totrct.xmin = m_frame_rect.GetLeft();
	area_dummy.totrct.xmax = m_frame_rect.GetRight();
	area_dummy.totrct.ymin = m_frame_rect.GetBottom();
	area_dummy.totrct.ymax = m_frame_rect.GetTop();

	dumprect = screenshot(&area_dummy, &dumpsx, &dumpsy);

	if (dumprect) {
		/* initialize image file format data */
		Scene *scene = (screen)? screen->scene: NULL;
		ImageFormatData im_format;

		if (scene)
			im_format = scene->r.im_format;
		else
			BKE_imformat_defaults(&im_format);

		/* create file path */
		char path[FILE_MAX];
		BLI_strncpy(path, filename, sizeof(path));
		BLI_path_abs(path, G.main->name);
		BKE_add_image_extension_from_type(path, im_format.imtype);

		/* create and save imbuf */
		ImBuf *ibuf = IMB_allocImBuf(dumpsx, dumpsy, 24, 0);
		ibuf->rect = dumprect;

		BKE_imbuf_write_as(ibuf, path, &im_format, false);

		ibuf->rect = NULL;
		IMB_freeImBuf(ibuf);
		MEM_freeN(dumprect);
	}
}

#ifdef WITH_SHMDATA
void KX_BlenderCanvasFbo::EnableShmdata(const char *filename)
{
    if (filename != NULL)
        sprintf(m_shmdata_filename, "%s", filename);
}
#endif
