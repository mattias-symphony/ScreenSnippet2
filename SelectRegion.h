

struct Window {
	HWND hwnd;
	HDC backbuffer;
	POINT prevTopLeft;	
	POINT prevBottomRight;	
};


struct SelectRegionData {
	HPEN pen;	
	HPEN eraser;	
	HBRUSH background;
	HBRUSH transparent;
	POINT topLeft;	
	POINT bottomRight;	
	BOOL dragging;
	BOOL done;
	BOOL aborted;
	RECT region;
	int windowCount;
	Window windows[ 256 ];
};



void drawRect( HWND hwnd, HDC dc, POINT a, POINT b ) {
	ScreenToClient( hwnd, &a );
	ScreenToClient( hwnd, &b );
	BeginPath( dc );
	MoveToEx( dc, a.x, a.y, NULL);
	LineTo( dc, b.x, a.y );
	LineTo( dc, b.x, b.y );
	LineTo( dc, a.x, b.y );
	LineTo( dc, a.x, a.y );
	EndPath( dc );
}


static void CALLBACK updateMouseTimerProc( HWND hwnd, UINT message, UINT_PTR id, DWORD ms ) {
	POINT p;
	GetCursorPos( &p );

	struct SelectRegionData* selectRegionData = (struct SelectRegionData*) id;

	if( (GetAsyncKeyState( VK_ESCAPE ) & 0x8000 ) != 0 ) {
		selectRegionData->aborted = TRUE;
	}

	if( selectRegionData->dragging ) {
		selectRegionData->bottomRight.x = p.x;
		selectRegionData->bottomRight.y = p.y;
		for( int i = 0; i < selectRegionData->windowCount; ++i ) {
			InvalidateRect( selectRegionData->windows[ i ].hwnd, NULL, FALSE );
		}
	}

	if( selectRegionData->dragging && ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) == 0 ) {
		selectRegionData->dragging = FALSE;
		selectRegionData->done = TRUE;
		POINT topLeft = selectRegionData->topLeft;
		POINT bottomRight = selectRegionData->bottomRight;
		selectRegionData->region.left = topLeft.x;
		selectRegionData->region.top = topLeft.y;
		selectRegionData->region.right = bottomRight.x;
		selectRegionData->region.bottom = bottomRight.y;
		for( int i = 0; i < selectRegionData->windowCount; ++i ) {
			InvalidateRect( selectRegionData->windows[ i ].hwnd, NULL, FALSE );
		}
	}

	SetTimer( hwnd, (UINT_PTR)selectRegionData, 16, updateMouseTimerProc );
}


static LRESULT CALLBACK selectRegionWndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	struct SelectRegionData* selectRegionData = (struct SelectRegionData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
	switch( message ) {
		case WM_ERASEBKGND: {
			if( selectRegionData && selectRegionData->dragging ) {
				return 1;
			}
		} break;
		case WM_PAINT: {
			struct Window* window = NULL;
			int index = 0;
			for( int i = 0; i < selectRegionData->windowCount; ++i ) {
				if( selectRegionData->windows[ i ].hwnd == hwnd ) {
					window = &selectRegionData->windows[ i ];
					break;
				}
			}
			if( window ) {
				// erase previous rect
				drawRect( hwnd, window->backbuffer, window->prevTopLeft, window->prevBottomRight );
				SelectObject( window->backbuffer, selectRegionData->eraser );
				SelectObject( window->backbuffer, selectRegionData->background );
				StrokeAndFillPath( window->backbuffer );
			
				// draw current rect
				drawRect( hwnd, window->backbuffer, selectRegionData->topLeft, selectRegionData->bottomRight );
				SelectObject( window->backbuffer, selectRegionData->pen );
				SelectObject( window->backbuffer, selectRegionData->transparent );
				StrokeAndFillPath( window->backbuffer );

				POINT cp1 = selectRegionData->topLeft;
				POINT cp2 = selectRegionData->bottomRight;
				if( cp1.x > cp2.x ) {
					swap( &cp1.x, &cp2.x );
				}
				if( cp1.y > cp2.y ) {
					swap( &cp1.y, &cp2.y );
				}
				ScreenToClient( hwnd, &cp1 );
				ScreenToClient( hwnd, &cp2 );
				RECT curr = { cp1.x, cp1.y, cp2.x, cp2.y };
				POINT pp1 = window->prevTopLeft;
				POINT pp2 = window->prevBottomRight;
				if( pp1.x > pp2.x ) {
					swap( &pp1.x, &pp2.x );
				}
				if( pp1.y > pp2.y ) {
					swap( &pp1.y, &pp2.y );
				}
				ScreenToClient( hwnd, &pp1 );
				ScreenToClient( hwnd, &pp2 );
				RECT prev = { pp1.x, pp1.y, pp2.x, pp2.y };
				RECT frame;
				UnionRect( &frame, &curr, &prev );			

				RECT client;
				GetClientRect( hwnd, &client );

				RECT bounds;
				IntersectRect( &bounds, &frame, &client );
				InflateRect( &bounds, 4, 4 );
			
				window->prevTopLeft = selectRegionData->topLeft;
				window->prevBottomRight = selectRegionData->bottomRight;

				PAINTSTRUCT ps; 
				HDC dc = BeginPaint( hwnd, &ps );
				BitBlt( dc, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, 
					window->backbuffer, bounds.left, bounds.top, SRCCOPY );
				EndPaint( hwnd, &ps );
			}
		} break;
		case WM_LBUTTONDOWN: {
			POINT p = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
			ClientToScreen( hwnd, &p );
			selectRegionData->topLeft.x = p.x;
			selectRegionData->topLeft.y = p.y; 
			selectRegionData->bottomRight.x = p.x;
			selectRegionData->bottomRight.y = p.y;
			selectRegionData->dragging = TRUE;
			InvalidateRect( hwnd, NULL, FALSE );
		} break;
	}
    return DefWindowProc( hwnd, message, wparam, lparam);
}

struct FindScreensData {
	int count;
	RECT bounds[ 256 ];
};


static BOOL CALLBACK findScreens( HMONITOR monitor, HDC dc, LPRECT rect, LPARAM lparam ) {	
	struct FindScreensData* findScreensData = (struct FindScreensData*) lparam;
	findScreensData->bounds[ findScreensData->count++ ] = *rect;
	return TRUE;
}


int selectRegion( RECT* region ) {
	struct FindScreensData findScreensData = { 0 };
	EnumDisplayMonitors( NULL, NULL, findScreens, (LPARAM) &findScreensData );
	if( findScreensData.count <= 0 ) {
		return EXIT_FAILURE;
	}

	COLORREF frame = RGB( 255, 255, 255 );
	COLORREF transparent = RGB( 0, 255, 0 );
	COLORREF background = RGB( 0, 0, 0 );

	struct SelectRegionData selectRegionData = { 
		CreatePen( PS_SOLID, 2, frame ),
		CreatePen( PS_SOLID, 2, background ),
		CreateSolidBrush( background ),
		CreateSolidBrush( transparent ),
	};


	WNDCLASSW wc = { 
		CS_OWNDC | CS_HREDRAW | CS_VREDRAW, // style
		(WNDPROC) selectRegionWndProc, 		// lpfnWndProc
		0, 									// cbClsExtra
		0, 									// cbWndExtra
		GetModuleHandleA( NULL ), 			// hInstance
		NULL, 								// hIcon;
		LoadCursor( NULL, IDC_CROSS ), 		// hCursor
		selectRegionData.background,  		// hbrBackground
		NULL, 								// lpszMenuName
		WINDOW_CLASS_NAME 					// lpszClassName
	};
    RegisterClassW( &wc );


	int count = findScreensData.count;
	HWND hwnd[ 256 ] = { NULL };
	selectRegionData.windowCount = count;
	for( int i = 0; i < count; ++i ) {
		struct Window* window = &selectRegionData.windows[ i ];
		RECT bounds = findScreensData.bounds[ i ];
		hwnd[ i ] = window->hwnd = CreateWindowExW( WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, 
			NULL, WS_VISIBLE, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, 
			NULL, NULL, GetModuleHandleA( NULL ), 0 );
		

		HDC dc = GetDC( window->hwnd );
		HBITMAP backbuffer = CreateCompatibleBitmap( dc, bounds.right - bounds.left, bounds.bottom - bounds.top );
		window->backbuffer = CreateCompatibleDC( dc );
		SelectObject( window->backbuffer, backbuffer );
		ReleaseDC( window->hwnd, dc );

		SetWindowLongPtrA( hwnd[ i ], GWLP_USERDATA, (LONG_PTR)&selectRegionData );
		SetWindowLongA(hwnd[ i ], GWL_STYLE, WS_VISIBLE );
		SetLayeredWindowAttributes(hwnd[ i ], transparent, 100, LWA_ALPHA | LWA_COLORKEY);
		UpdateWindow( hwnd[ i ] );
	}

	updateMouseTimerProc( hwnd[ 0 ], 0, (UINT_PTR)&selectRegionData, 0 );

	int running = count;
	while( running && !selectRegionData.done && !selectRegionData.aborted )  {
		for( int i = 0; i < count; ++i ) {
			if( hwnd[ i ] ) {
				MSG msg = { NULL };
				while( PeekMessageA( &msg, hwnd[ i ], 0, 0, PM_REMOVE ) ) {
					if( msg.message == WM_CLOSE ) {
						DestroyWindow( hwnd[ i ] );
						hwnd[ i ] = NULL;
						--running;
					}
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
				Sleep( 10 );
			}
		}
	}

	for( int i = 0; i < count; ++i ) {
		if( hwnd[ i ] ) {
			DestroyWindow( hwnd[ i ] );
		}
	}

	DeleteObject( selectRegionData.pen );
	DeleteObject( selectRegionData.eraser );
	DeleteObject( selectRegionData.background );
	DeleteObject( selectRegionData.transparent );
	UnregisterClassW( wc.lpszClassName, GetModuleHandleW( NULL ) );
	
	if( selectRegionData.aborted ) {
		return EXIT_FAILURE;
	}

	*region = selectRegionData.region;
	return EXIT_SUCCESS;
}


