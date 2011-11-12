/**************************************************************************
    Lightspark, a free flash player implementation

    Copyright (C) 2009-2011  Alessandro Pignotti (a.pignotti@sssup.it)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifndef ENGINEUTILS_H
#define ENGINEUTILS_H

#ifndef _WIN32
#	include <X11/Xlib.h>
#endif
#include <gtk/gtk.h>

namespace lightspark
{

typedef void (*ls_callback_t)(void*);

class EngineData
{
public:
#ifdef _WIN32
	EngineData(GdkNativeWindow win, int w, int h) : window(win),width(w),height(h) {}
#else
	EngineData(Display* d, VisualID v, GdkNativeWindow win, int w, int h):
			display(d),visual(v),window(win),container(NULL),width(w),height(h) {}
	Display* display;
	VisualID visual;
#endif
	GdkNativeWindow window;
	GtkWidget* container;
	int width;
	int height;

	virtual ~EngineData() {}
	virtual void setupMainThreadCallback(ls_callback_t func, void* arg) = 0;
	virtual void stopMainDownload() = 0;
	virtual bool isSizable() const = 0;
};

};
#endif
