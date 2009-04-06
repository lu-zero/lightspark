/**************************************************************************
    Lighspark, a free flash player implementation

    Copyright (C) 2009  Alessandro Pignotti (a.pignotti@sssup.it)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include <iostream>
#include <string.h>
#include <pthread.h>
#include <SDL/SDL.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <algorithm>

#include "swf.h"
#include "logger.h"
#include "actions.h"
#include "streams.h"

using namespace std;

pthread_t ParseThread::t;

pthread_t InputThread::t;
std::list < IActiveObject* > InputThread::listeners;
sem_t InputThread::sem_listeners;

pthread_t RenderThread::t;
sem_t RenderThread::mutex;
sem_t RenderThread::render;
sem_t RenderThread::end_render;
Frame* RenderThread::cur_frame=NULL;
list<DisplayListTag*> null_list;
Frame RenderThread::bak_frame(null_list);
int RenderThread::bak(0);
GLXFBConfig RenderThread::mFBConfig;
GLXContext RenderThread::mContext;

extern SystemState sys;

int thread_debug(char* msg);
SWF_HEADER::SWF_HEADER(istream& in)
{
	//Valid only on little endian platforms
	in >> Signature[0] >> Signature[1] >> Signature[2];

	in >> Version >> FileLength;
	if(Signature[0]=='F' && Signature[1]=='W' && Signature[2]=='S')
	{
		LOG(NO_INFO, "Uncompressed SWF file: Version " << (int)Version << " Length " << FileLength);
	}
	else if(Signature[0]=='C' && Signature[1]=='W' && Signature[2]=='S')
	{
		LOG(NO_INFO, "Compressed SWF file: Version " << (int)Version << " Length " << FileLength);
		sync_stream* ss=dynamic_cast<sync_stream*>(in.rdbuf());
		ss->setCompressed();
	}
	in >> FrameSize >> FrameRate >> FrameCount;
}

MovieClip::MovieClip()
{
	sem_init(&sem_frames,0,1);
}

bool list_orderer(const DisplayListTag* a, int d)
{
	return a->getDepth()<d;
}

void MovieClip::addToDisplayList(DisplayListTag* t)
{
	list<DisplayListTag*>::iterator it=lower_bound(displayList.begin(),displayList.end(),t->getDepth(),list_orderer);
	displayList.insert(it,t);
}

SystemState::SystemState():currentState(&clip.state),parsingDisplayList(&clip.displayList)
{
	sem_init(&sem_dict,0,1);
	sem_init(&new_frame,0,0);
	sem_init(&sem_run,0,0);

	sem_init(&mutex,0,1);
}

void* ParseThread::worker(void* in_ptr)
{
	try
	{
		istream& f=*(istream*)in_ptr;
		SWF_HEADER h(f);
		sys.setFrameSize(h.getFrameSize());

		int done=0;

		TagFactory factory(f);
		while(1)
		{
			Tag* tag=factory.readTag();
			switch(tag->getType())
			{
			//	case TAG:
				case END_TAG:
					//sleep(5);
					return 0;
				case RENDER_TAG:
					sys.addToDictionary(dynamic_cast<RenderTag*>(tag));
					break;
				case DISPLAY_LIST_TAG:
					sys.addToDisplayList(dynamic_cast<DisplayListTag*>(tag));
					break;
				case SHOW_TAG:
				{
					sys.commitFrame();
					break;
				}
				case CONTROL_TAG:
					dynamic_cast<ControlTag*>(tag)->execute();
					break;
			}
		}
	}
	catch(const char* s)
	{
		LOG(ERROR,"Exception caught: " << s);
		exit(-1);
	}


}

ParseThread::ParseThread(istream& in)
{
	pthread_create(&t,NULL,worker,&in);
}

void ParseThread::wait()
{
	pthread_join(t,NULL);
}

InputThread::InputThread(ENGINE e, void* param)
{
	LOG(NO_INFO,"Creating input thread");
	sem_init(&sem_listeners,0,1);
	if(e==SDL)
	{
		pthread_create(&t,NULL,sdl_worker,param);
	}
	else
		throw "Engine not supported";
}

void InputThread::wait()
{
	pthread_join(t,NULL);
}

/*void* InputThread::npapi_worker(void* in_ptr)
{
	NPAPI_params* p=(NPAPI_params*)in_ptr;
//	Display* d=XOpenDisplay(NULL);
	XSelectInput(p->display,p->window,PointerMotionMask|ExposureMask);

	XEvent e;
	while(XWindowEvent(p->display,p->window,PointerMotionMask|ExposureMask, &e))
	{
		exit(-1);
	}
}*/

void* InputThread::sdl_worker(void* in_ptr)
{
	SDL_Event event;
	while(SDL_WaitEvent(&event))
	{
		switch(event.type)
		{
			case SDL_KEYDOWN:
			{
				switch(event.key.keysym.sym)
				{
					case SDLK_q:
						exit(0);
						break;
/*					case SDLK_n:
						list<IActiveObject*>::const_iterator it=listeners.begin();
						int c=0;
						for(it;it!=listeners.end();it++)
						{
							if(c==2)
								(*it)->MouseEvent(0,0);
							c++;
						}
						sem_post(&sys.sem_run);
						break;*/
				}
				break;
			}
			case SDL_MOUSEMOTION:
			{
				//printf("Oh! mouse\n");
				break;
			}
		}
	}
}

void InputThread::addListener(IActiveObject* ob)
{
	sem_wait(&sem_listeners);

	listeners.push_back(ob);

	sem_post(&sem_listeners);
}

RenderThread::RenderThread(ENGINE e,void* params)
{
	sem_init(&mutex,0,1);
	sem_init(&render,0,0);
	sem_init(&end_render,0,0);
	if(e==SDL)
		pthread_create(&t,NULL,sdl_worker,0);
	else if(e==NPAPI)
		pthread_create(&t,NULL,npapi_worker,params);


}

void* RenderThread::npapi_worker(void* param)
{
	NPAPI_params* p=(NPAPI_params*)param;
	
	Display* d=XOpenDisplay(NULL);

    	int a,b;
    	Bool glx_present=glXQueryVersion(d,&a,&b);
	if(!glx_present)
	{
		printf("glX not present\n");
		return NULL;
	}
	int attrib[10];
	attrib[0]=GLX_BUFFER_SIZE;
	attrib[1]=24;
	attrib[2]=GLX_VISUAL_ID;
	attrib[3]=p->visual;
	attrib[4]=GLX_DEPTH_SIZE;
	attrib[5]=24;
	attrib[6]=GLX_STENCIL_SIZE;
	attrib[7]=8;

	attrib[8]=None;
	GLXFBConfig* fb=glXChooseFBConfig(d, 0, attrib, &a);
//	printf("returned %x pointer and %u elements\n",fb, a);
	if(!fb)
	{
		attrib[0]=None;
		fb=glXChooseFBConfig(d, 0, NULL, &a);
		LOG(ERROR,"Falling back to no depth and no stencil");
	}
	int i;
	for(i=0;i<a;i++)
	{
		int id,v;
		glXGetFBConfigAttrib(d,fb[i],GLX_BUFFER_SIZE,&v);
		glXGetFBConfigAttrib(d,fb[i],GLX_VISUAL_ID,&id);
//		printf("ID 0x%x size %u\n",id,v);
		if(id==p->visual)
		{
//			printf("good id %x\n",id);
			break;
		}
	}
	mFBConfig=fb[i];
	XFree(fb);

	mContext = glXCreateNewContext(d,mFBConfig,GLX_RGBA_TYPE ,NULL,1);
	glXMakeContextCurrent(d, p->window, p->window, mContext);
	if(!glXIsDirect(d,mContext))
		printf("Indirect!!\n");


	glViewport(0,0,p->width,p->height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0,p->width,p->height,0,-100,0);
	glMatrixMode(GL_MODELVIEW);

	try
	{
		while(1)
		{
			sem_wait(&render);
			sem_wait(&mutex);
			if(cur_frame==NULL)
			{
				sem_post(&mutex);
				sem_post(&end_render);
				continue;
			}
			if(!bak)
				bak_frame=*cur_frame;
			RGB bg=sys.getBackground();
			glClearColor(bg.Red/255.0F,bg.Green/255.0F,bg.Blue/255.0F,0);
			glClearDepth(0xffff);
			glClearStencil(5);
			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
			glLoadIdentity();

			float scalex=p->width;
			scalex/=sys.getFrameSize().Xmax;
			float scaley=p->height;
			scaley/=sys.getFrameSize().Ymax;
			glScalef(scalex,scaley,1);

			if(bak)
			{
				bak_frame.Render(0);
				bak=0;
			}
			else
				cur_frame->Render(0);
			glFlush();
			glXSwapBuffers(d,p->window);
			sem_post(&mutex);

			sem_post(&end_render);
		}
	}
	catch(const char* e)
	{
		LOG(ERROR,"Exception caught " << e);
		exit(-1);
	}
	delete p;
}

void* RenderThread::sdl_worker(void*)
{
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );
	SDL_SetVideoMode( 640, 480, 24, SDL_OPENGL );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc(GL_LEQUAL);

//	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	glViewport(0,0,640,480);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0,640,480,0,-100,0);
	glMatrixMode(GL_MODELVIEW);

	try
	{
		while(1)
		{
			sem_wait(&render);
			if(cur_frame==NULL)
			{
				sem_post(&end_render);
				continue;
			}
			RGB bg=sys.getBackground();
			glClearColor(bg.Red/255.0F,bg.Green/255.0F,bg.Blue/255.0F,0);
			glClearDepth(0xffff);
			glClearStencil(5);
			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
			glLoadIdentity();

			glScalef(0.1,0.1,1);

			cur_frame->Render(0);

			SDL_GL_SwapBuffers( );
			sem_post(&end_render);
		}
	}
	catch(const char* e)
	{
		LOG(ERROR, "Exception caught " << e);
		exit(-1);
	}
}

void RenderThread::draw(Frame* f)
{
	//TODO: sync (by copy)
	if(f!=NULL)
	{
		sem_wait(&mutex);
		cur_frame=f;
		sem_post(&mutex);
	}
	else
		bak=1;
	sem_post(&render);
	sem_wait(&end_render);

}

void SystemState::waitToRun()
{
	sem_wait(&mutex);

	if(clip.state.stop_FP && !update_request)
	{
		sem_post(&mutex);
		sem_wait(&sem_run);
	}
	while(1)
	{
		if(clip.state.FP<clip.frames.size())
			break;

		sem_post(&mutex);
		sem_wait(&new_frame);
		sem_wait(&mutex);
	}
	update_request=false;
	clip.state.next_FP=clip.state.FP+1;
	sem_post(&mutex);
}

Frame& SystemState::getFrameAtFP() 
{
	sem_wait(&mutex);
	list<Frame>::iterator it=clip.frames.begin();
	for(int i=0;i<clip.state.FP;i++)
		it++;
	sem_post(&mutex);

	return *it;
}

void SystemState::advanceFP()
{
	sem_wait(&mutex);
	clip.state.FP=clip.state.next_FP; 
	sem_post(&mutex);
}

void SystemState::setFrameSize(const RECT& f)
{
	sem_wait(&mutex);
	frame_size=f; 
	sem_post(&mutex);
}

RECT SystemState::getFrameSize()
{
/*	sem_wait(&mutex);
	frame_size=f; 
	sem_post(&mutex);*/
	return frame_size;
}

void SystemState::addToDictionary(RenderTag* r)
{
	sem_wait(&mutex);
	//sem_wait(&sys.sem_dict);
	dictionary.push_back(r);
	//sem_post(&sys.sem_dict);
	sem_post(&mutex);
}

void SystemState::addToDisplayList(DisplayListTag* t)
{
	sem_wait(&mutex);
	clip.addToDisplayList(t);
	sem_post(&mutex);
}

void SystemState::commitFrame()
{
	sem_wait(&mutex);
	//sem_wait(&clip.sem_frames);
	clip.frames.push_back(Frame(clip.displayList));
	sem_post(&new_frame);
	sem_post(&mutex);
	//sem_post(&clip.sem_frames);
}

RGB SystemState::getBackground()
{
	return Background;
}

void SystemState::setBackground(const RGB& bg)
{
	Background=bg;
}

void SystemState::setUpdateRequest(bool s)
{
	sem_wait(&mutex);
	update_request=s;
	sem_post(&mutex);
}

RenderTag* SystemState::dictionaryLookup(UI16 id)
{
	sem_wait(&mutex);
	//sem_wait(&sem_dict);
	list< RenderTag*>::iterator it = dictionary.begin();
	for(it;it!=dictionary.end();it++)
	{
		if((*it)->getId()==id)
			break;
	}
	if(it==dictionary.end())
		throw "No such ID";
	//sem_post(&sem_dict);
	sem_post(&mutex);
	return *it;
}
