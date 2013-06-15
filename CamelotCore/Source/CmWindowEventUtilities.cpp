/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2011 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "CmWindowEventUtilities.h"
#include "CmRenderWindow.h"
#include "CmApplication.h"
#include "CmException.h"
#include "CmCursor.h"
#include "CmInput.h"

#if CM_PLATFORM == CM_PLATFORM_LINUX
#include <X11/Xlib.h>
void GLXProc( CamelotFramework::RenderWindow *win, const XEvent &event );
#endif

using namespace CamelotFramework;

WindowEventUtilities::Windows WindowEventUtilities::_msWindows;
boost::signal<void(const Int2&)> WindowEventUtilities::onMouseMoved;
boost::signal<void(CamelotFramework::UINT32)> WindowEventUtilities::onCharInput;

//--------------------------------------------------------------------------------//
void WindowEventUtilities::messagePump()
{
#if CM_PLATFORM == CM_PLATFORM_WIN32
	// Windows Message Loop (NULL means check all HWNDs belonging to this context)
	MSG  msg;
	while( PeekMessage( &msg, NULL, 0U, 0U, PM_REMOVE ) )
	{
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}
#elif CM_PLATFORM == CM_PLATFORM_LINUX
	//GLX Message Pump
	Windows::iterator win = _msWindows.begin();
	Windows::iterator end = _msWindows.end();

	Display* xDisplay = 0; // same for all windows
	
	for (; win != end; win++)
	{
	    XID xid;
	    XEvent event;

	    if (!xDisplay)
		(*win)->getCustomAttribute("XDISPLAY", &xDisplay);

	    (*win)->getCustomAttribute("WINDOW", &xid);

	    while (XCheckWindowEvent (xDisplay, xid, StructureNotifyMask | VisibilityChangeMask | FocusChangeMask, &event))
	    {
		GLXProc(*win, event);
	    }

	    // The ClientMessage event does not appear under any Event Mask
	    while (XCheckTypedWindowEvent (xDisplay, xid, ClientMessage, &event))
	    {
		GLXProc(*win, event);
	    }
	}
#elif CM_PLATFORM == CM_PLATFORM_APPLE && !defined __OBJC__ && !defined __LP64__
	// OSX Message Pump
	EventRef event = NULL;
	EventTargetRef targetWindow;
	targetWindow = GetEventDispatcherTarget();
    
    // If we are unable to get the target then we no longer care about events.
    if( !targetWindow ) return;
    
    // Grab the next event, process it if it is a window event
	if( ReceiveNextEvent( 0, NULL, kEventDurationNoWait, true, &event ) == noErr )
	{
        // Dispatch the event
		SendEventToEventTarget( event, targetWindow );
   		ReleaseEvent( event );
	}
#endif
}
//--------------------------------------------------------------------------------//
void WindowEventUtilities::_addRenderWindow(RenderWindow* window)
{
	_msWindows.push_back(window);
}

//--------------------------------------------------------------------------------//
void WindowEventUtilities::_removeRenderWindow(RenderWindow* window)
{
	Windows::iterator i = std::find(_msWindows.begin(), _msWindows.end(), window);
	if( i != _msWindows.end() )
		_msWindows.erase( i );
}

#if CM_PLATFORM == CM_PLATFORM_WIN32
//--------------------------------------------------------------------------------//
LRESULT CALLBACK WindowEventUtilities::_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_CREATE)
	{	// Store pointer to Win32Window in user data area
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)(((LPCREATESTRUCT)lParam)->lpCreateParams));
		return 0;
	}

	// look up window instance
	// note: it is possible to get a WM_SIZE before WM_CREATE
	RenderWindow* win = (RenderWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (!win)
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	switch( uMsg )
	{
	case WM_ACTIVATE:
	{
        bool active = (LOWORD(wParam) != WA_INACTIVE);
        if( active )
        {
		    win->setActive( true );

			if(!win->hasFocus())
				win->_setHasFocus(true);

			gInput().inputWindowChanged(*win);
        }
        else
        {
            if( win->isDeactivatedOnFocusChange() )
            {
    		    win->setActive( false );
            }

			if(win->hasFocus())
				win->_setHasFocus(false);
        }

		break;
	}
	case WM_SYSKEYDOWN:
		switch( wParam )
		{
		case VK_CONTROL:
		case VK_SHIFT:
		case VK_MENU: //ALT
			//return zero to bypass defProc and signal we processed the message
			return 0;
		}
		break;
	case WM_SYSKEYUP:
		switch( wParam )
		{
		case VK_CONTROL:
		case VK_SHIFT:
		case VK_MENU: //ALT
		case VK_F10:
			//return zero to bypass defProc and signal we processed the message
			return 0;
		}
		break;
	case WM_SYSCHAR:
		// return zero to bypass defProc and signal we processed the message, unless it's an ALT-space
		if (wParam != VK_SPACE)
			return 0;
		break;
	case WM_ENTERSIZEMOVE:
		break;
	case WM_EXITSIZEMOVE:
		break;
	case WM_MOVE:
		win->windowMovedOrResized();
		break;
	case WM_DISPLAYCHANGE:
		win->windowMovedOrResized();
		break;
	case WM_SIZE:
		win->windowMovedOrResized();
		break;
	case WM_SETCURSOR:
		if(Cursor::isHidden())
			Cursor::_win32HideCursor();
		else
			Cursor::_win32ShowCursor();
		return true;
	case WM_GETMINMAXINFO:
		// Prevent the window from going smaller than some minimu size
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 100;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 100;
		break;
	case WM_CLOSE:
	{
		// TODO - Only stop main loop if primary window is closed!!
		gApplication().stopMainLoop();

		return 0;
	}
	case WM_MOUSEMOVE:
		{
			POINT mousePos;
			
			mousePos.x = GET_X_LPARAM(lParam);
			mousePos.y = GET_Y_LPARAM(lParam); 

			ClientToScreen(hWnd, &mousePos);

			if(!onMouseMoved.empty())
				onMouseMoved(Int2(mousePos.x, mousePos.y));

			return true;
		}
	case WM_DEADCHAR:
	case WM_CHAR:
		{
			switch (wParam) 
			{ 
			case VK_BACK:
			case 0x0A:  // linefeed 
			case 0x0D:  // carriage return 
			case VK_ESCAPE:
			case VK_TAB: 
				break; 

			default:    // displayable character 
				{
					UINT8 scanCode = (lParam >> 16) & 0xFF;

					BYTE keyState[256];
					HKL layout = GetKeyboardLayout(0);
					if(GetKeyboardState(keyState) == 0)
						return 0;

					unsigned int vk = MapVirtualKeyEx(scanCode, MAPVK_VSC_TO_VK_EX, layout);
					if(vk == 0)
						return 0;

					bool isDeadKey = (MapVirtualKeyEx(vk, MAPVK_VK_TO_CHAR, layout) & (1 << 31)) != 0;
					if(isDeadKey)
						return 0;

					wchar_t buff[3] = {0};
					int numChars = ToUnicodeEx(vk, scanCode, keyState, buff, 3, 0, layout);

					// TODO - I am ignoring dead keys here - primarily because I haven't found a good way of retrieving non-combined dead key
					// value. ToUnicodeEx and MapVirtualKeyEx only return precombined (i.e. spacing) versions, which can't be combined using other characters.
					// I need non-combined version so I can use it with FoldString to apply to a certain character.

					UINT32 finalChar = 0;
					if(numChars == 1)
						finalChar = buff[0];
					else
						return 0;

					if(!onCharInput.empty())
						onCharInput(finalChar);

					return 0;
				}
			} 

			break;
		}
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}
#elif CM_PLATFORM == CM_PLATFORM_LINUX
//--------------------------------------------------------------------------------//
void GLXProc( RenderWindow *win, const XEvent &event )
{
	switch(event.type)
	{
	case ClientMessage:
	{
		::Atom atom;
		win->getCustomAttribute("ATOM", &atom);
		if(event.xclient.format == 32 && event.xclient.data.l[0] == (long)atom)
		{	
			//Window closed by window manager
		}
		break;
	}
	case DestroyNotify:
	{
		if (!win->isClosed())
		{
			// Window closed without window manager warning.
		}
		break;
	}
	case ConfigureNotify:
	{    
        // This could be slightly more efficient if windowMovedOrResized took arguments:
		unsigned int oldWidth, oldHeight, oldDepth;
		int oldLeft, oldTop;
		win->getMetrics(oldWidth, oldHeight, oldDepth, oldLeft, oldTop);
		win->windowMovedOrResized();

		unsigned int newWidth, newHeight, newDepth;
		int newLeft, newTop;
		win->getMetrics(newWidth, newHeight, newDepth, newLeft, newTop);

		break;
	}
	case FocusIn:     // Gained keyboard focus
	case FocusOut:    // Lost keyboard focus
		break;
	case MapNotify:   //Restored
		win->setActive( true );
		break;
	case UnmapNotify: //Minimised
		win->setActive( false );
		win->setVisible( false );
		break;
	case VisibilityNotify:
		switch(event.xvisibility.state)
		{
		case VisibilityUnobscured:
			win->setActive( true );
			win->setVisible( true );
			break;
		case VisibilityPartiallyObscured:
			win->setActive( true );
			win->setVisible( true );
			break;
		case VisibilityFullyObscured:
			win->setActive( false );
			win->setVisible( false );
			break;
		}
		break;
	default:
		break;
	} //End switch event.type
}
#elif CM_PLATFORM == CM_PLATFORM_APPLE && !defined __OBJC__ && !defined __LP64__
//--------------------------------------------------------------------------------//
OSStatus WindowEventUtilities::_CarbonWindowHandler(EventHandlerCallRef nextHandler, EventRef event, void* wnd)
{
    OSStatus status = noErr;
    
    // Only events from our window should make it here
    // This ensures that our user data is our WindowRef
    RenderWindow* curWindow = (RenderWindow*)wnd;
    if(!curWindow) return eventNotHandledErr;
    
    //Iterator of all listeners registered to this RenderWindow
	WindowEventListeners::iterator index,
        start = _msListeners.lower_bound(curWindow),
        end = _msListeners.upper_bound(curWindow);
    
    // We only get called if a window event happens
    UInt32 eventKind = GetEventKind( event );

    switch( eventKind )
    {
        case kEventWindowActivated:
            curWindow->setActive( true );
            for( ; start != end; ++start )
                (start->second)->windowFocusChange(curWindow);
            break;
        case kEventWindowDeactivated:
            if( curWindow->isDeactivatedOnFocusChange() )
            {
                curWindow->setActive( false );
            }

            for( ; start != end; ++start )
                (start->second)->windowFocusChange(curWindow);

            break;
        case kEventWindowShown:
        case kEventWindowExpanded:
            curWindow->setActive( true );
            curWindow->setVisible( true );
            for( ; start != end; ++start )
                (start->second)->windowFocusChange(curWindow);
            break;
        case kEventWindowHidden:
        case kEventWindowCollapsed:
            curWindow->setActive( false );
            curWindow->setVisible( false );
            for( ; start != end; ++start )
                (start->second)->windowFocusChange(curWindow);
            break;            
        case kEventWindowDragCompleted:
            curWindow->windowMovedOrResized();
            for( ; start != end; ++start )
				(start->second)->windowMoved(curWindow);
            break;
        case kEventWindowBoundsChanged:
            curWindow->windowMovedOrResized();
            for( ; start != end; ++start )
				(start->second)->windowResized(curWindow);
            break;
		case kEventWindowClose:
		{
			bool close = true;
			for( ; start != end; ++start )
			{
				if (!(start->second)->windowClosing(curWindow))
					close = false;
			}
			if (close)
				// This will cause event handling to continue on to the standard handler, which calls
				// DisposeWindow(), which leads to the 'kEventWindowClosed' event
				status = eventNotHandledErr;
			break;
		}
        case kEventWindowClosed:

            for( ; start != end; ++start )
				(start->second)->windowClosed(curWindow);
            break;
        default:
            status = eventNotHandledErr;
            break;
    }
    
    return status;
}
#endif
