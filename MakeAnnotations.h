
struct Stroke {
	BOOL highlighter;
	int penIndex;
	int capacity;
	int count;
	POINT* points;
};


struct MakeAnnotationsData {
	RECT bounds;
	Gdiplus::Pen** pens;	
	Gdiplus::Pen** highlighters;	
	Gdiplus::Pen* penEraser;
	Gdiplus::Pen* highlightEraser;
	HDC backbuffer;
	HDC snippet;
	BOOL highlighter;
	int penIndex;
	int highlightIndex;
	BOOL eraser;
	BOOL penDown;
	int strokeCount;
	struct Stroke strokes[ 256 ];
	HWND penButton;
	HWND highlightButton;
	HWND eraseButton;
	HWND doneButton;
	HMENU penMenu;
	HMENU highlightMenu;
    BOOL completed;
};


void newStroke( struct MakeAnnotationsData* data, BOOL highlighter, int penIndex ) {
	if( data->strokeCount < sizeof( data->strokes ) / sizeof( *data->strokes ) ) {
		struct Stroke* stroke = &data->strokes[ data->strokeCount ];
		stroke->highlighter = highlighter;
		stroke->penIndex = penIndex;
		stroke->capacity = 256;
		stroke->count = 0;
		stroke->points = (POINT*) malloc( sizeof( POINT ) * stroke->capacity );
		++data->strokeCount;
	}
}


void addStrokePoint( struct MakeAnnotationsData* data, POINT* p, BOOL force ) {
	if( data->strokeCount > 0 ) {
		struct Stroke* stroke = &data->strokes[ data->strokeCount - 1 ];
		if( stroke->count > 0 ) {
			int dx = stroke->points[ stroke->count - 1 ].x - p->x;
			int dy = stroke->points[ stroke->count - 1 ].y - p->y;
			int dist = (int)sqrtf( (float)( dx * dx + dy * dy ) );
			if( dist < ( force ? 5 : ( data->highlighter ? 25 : 15 ) ) ) {
				return;
			}
		}

		if( stroke->count >= stroke->capacity ) {
			stroke->capacity *= 2;
			stroke->points = (POINT*) realloc( stroke->points, sizeof( POINT ) * stroke->capacity );
		}
		stroke->points[ stroke->count++ ] = *p;
	}
}


static LRESULT CALLBACK makeAnnotationsWndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	struct MakeAnnotationsData* data = (struct MakeAnnotationsData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
	switch( message ) {
		case WM_COMMAND: {
			if( HIWORD( wparam ) == BN_CLICKED ) {
				if( (HWND) lparam == data->doneButton ) {
                    data->completed = TRUE;
					RECT bounds = data->bounds;
					BitBlt( data->snippet, bounds.left, bounds.top, bounds.right - bounds.left, 
						bounds.bottom - bounds.top, data->backbuffer, 0, 0, SRCCOPY );
					PostQuitMessage( 0 );
				}
				if( (HWND) lparam == data->penButton ) {
					RECT bounds;
					GetWindowRect( data->penButton, &bounds );
					POINT p = { bounds.left, bounds.bottom };
					DWORD item = TrackPopupMenu( data->penMenu, TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL );				
					if( item > 0 ) {
						data->penIndex = item - 1;
					}
					data->highlighter = FALSE;
					data->eraser = FALSE;
					SetCursor( LoadCursor( NULL, IDC_CROSS ) );
				}
				if( (HWND) lparam == data->highlightButton ) {
					RECT bounds;
					GetWindowRect( data->highlightButton, &bounds );
					POINT p = { bounds.left, bounds.bottom };
					DWORD item = TrackPopupMenu( data->highlightMenu,  TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL );				
					if( item > 0 ) {
						data->highlightIndex = item - 1;
					}
					data->highlighter = TRUE;
					data->eraser = FALSE;
					SetCursor( LoadCursor( NULL, IDC_CROSS ) );
				}
				if( (HWND) lparam == data->eraseButton ) {
					data->penDown = FALSE;
					data->eraser = TRUE;
					SetCursor( LoadCursor( NULL, IDC_NO ) );
				}
			}
		} break;

		case WM_SETCURSOR: {
			if( data->eraser ) {
				SetCursor( LoadCursor( NULL, IDC_NO ) );
			} else {
				SetCursor( LoadCursor( NULL, IDC_CROSS ) );
			}
		} break;
			
		case WM_CLOSE: {
			PostQuitMessage( 0 );
		} break;

		case WM_ERASEBKGND: {
			if( data ) {
				return 1;
			}
		} break;

		case WM_SIZE: {
			HDC dc = GetDC( hwnd );
			RECT r;
			GetClientRect( hwnd, &r  );
			FillRect( dc, &r, GetStockBrush( LTGRAY_BRUSH ) );
			ReleaseDC( hwnd, dc );
		} break;

		case WM_PAINT: {
			RECT bounds = data->bounds;
			HDC backbuffer = data->backbuffer;
			BitBlt( backbuffer, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, 
				data->snippet, 0, 0, SRCCOPY );

			Gdiplus::Graphics graphics( backbuffer );
			graphics.SetSmoothingMode( Gdiplus::SmoothingModeHighQuality );

			for( int i = 0; i < data->strokeCount; ++i ) {
				struct Stroke* stroke = &data->strokes[ i ];
				Gdiplus::Pen* pen = stroke->highlighter ? 
					data->highlighters[ stroke->penIndex ] : 
					data->pens[ stroke->penIndex ];
				if( stroke->count > 1 ) {			
					Gdiplus::Point* points = new Gdiplus::Point[ stroke->count ];
					for( int j = 0; j < stroke->count; ++j ) {
						points[ j ].X = stroke->points[ j ].x;
						points[ j ].Y = stroke->points[ j ].y;
					}
					graphics.DrawCurve( pen, points, stroke->count );
				}
				if( i == data->strokeCount -1 && !stroke->highlighter && data->penDown && stroke->count > 0 ) {
					POINT mouse;
					GetCursorPos( &mouse );
					ScreenToClient( hwnd, &mouse );
					POINT p = stroke->points[ stroke->count - 1 ];
					graphics.DrawLine( pen, p.x, p.y, mouse.x, mouse.y - 50 );
				}
			}

			PAINTSTRUCT ps; 
			HDC dc = BeginPaint( hwnd, &ps );
			BitBlt( dc, bounds.left, bounds.top + 50, bounds.right - bounds.left, bounds.bottom - bounds.top, 
				backbuffer, 0, 0, SRCCOPY );
			EndPaint( hwnd, &ps );
		} break;

		case WM_LBUTTONDOWN: {
			if( !data->eraser ) {
				data->penDown = TRUE;
				newStroke( data, data->highlighter, 
					data->highlighter ? data->highlightIndex : data->penIndex );
				POINT p = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) - 50 };
				addStrokePoint( data, &p, TRUE );
				InvalidateRect( hwnd, NULL, FALSE );
			    break; // If we are in 'eraser' mode, fall through into the "RBUTTONDOWN" eraser code below
			}
		} // Intentionally no `break;` statement here

		case WM_RBUTTONDOWN: {
			Gdiplus::Point p = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) - 50 };
			HDC backbuffer = data->backbuffer;
			Gdiplus::Graphics graphics( backbuffer );
			graphics.SetSmoothingMode( Gdiplus::SmoothingModeHighQuality );
			if( data->strokeCount > 0 ) {
				for( int i = data->strokeCount - 1; i >= 0 ; --i ) {
					struct Stroke* stroke = &data->strokes[ i ];
					if( stroke->count > 1 ) {			
						Gdiplus::Point* points = new Gdiplus::Point[ stroke->count ];
						for( int j = 0; j < stroke->count; ++j ) {
							points[ j ].X = stroke->points[ j ].x;
							points[ j ].Y = stroke->points[ j ].y;
						}
						Gdiplus::Pen* pen = stroke->highlighter ? 
							data->highlightEraser : 
							data->penEraser;
						Gdiplus::GraphicsPath path;
						path.AddCurve( points, stroke->count );
						if( path.IsOutlineVisible( p, pen, &graphics ) ) {
							stroke->count = 0;
							InvalidateRect( hwnd, NULL, TRUE );
							//break; // Only break if we want to limit erasing to one shape at a time
						}
					}
				}
			}
		} break;

		case WM_LBUTTONUP: {
			if( data->penDown ) {
				data->penDown = FALSE;
				POINT p = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) - 50 };
				addStrokePoint( data, &p, TRUE );
				InvalidateRect( hwnd, NULL, FALSE );
			}
		} break;

		case WM_MOUSEMOVE: {
			if( data->penDown ) {
				POINT p = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) - 50 };
				addStrokePoint( data, &p, FALSE );
				InvalidateRect( hwnd, NULL, FALSE );
			}
		} break;
	}

    return DefWindowProc( hwnd, message, wparam, lparam);
}


HBITMAP WINAPI penIcon( Gdiplus::Pen* pen ) { 
	int const width = 120;
	int const height = 20; 
	HWND hwndDesktop = GetDesktopWindow(); 
    HDC hdcDesktop = GetDC( hwndDesktop ); 
    HDC hdcMem = CreateCompatibleDC( hdcDesktop ); 
    COLORREF clrMenu = GetSysColor( COLOR_MENU ); 
 
    HBRUSH hbrOld = (HBRUSH) SelectObject( hdcMem, CreateSolidBrush( clrMenu ) ); 


    HBITMAP paHbm = CreateCompatibleBitmap( hdcDesktop, width, height ); 
    HBITMAP hbmOld = (HBITMAP) SelectObject( hdcMem, paHbm ); 
 
	PatBlt( hdcMem, 0, 0, width, height, PATCOPY ); 
	Gdiplus::Graphics graphics( hdcMem );
	graphics.SetSmoothingMode( Gdiplus::SmoothingModeHighQuality );
	graphics.DrawLine( pen, Gdiplus::Point( 20, height / 2 ), Gdiplus::Point( width, height / 2 ) );

    SelectObject( hdcMem, hbmOld ); 
 
    DeleteObject( SelectObject( hdcMem, hbrOld ) ); 
    DeleteDC( hdcMem ); 
    ReleaseDC( hwndDesktop, hdcDesktop ); 
	return paHbm;
}


int makeAnnotations( HBITMAP snippet, RECT bounds, int lang ) {
	WNDCLASSW wc = { 
		CS_OWNDC | CS_HREDRAW | CS_VREDRAW,		// style
		(WNDPROC) makeAnnotationsWndProc, 		// lpfnWndProc
		0, 										// cbClsExtra
		0, 										// cbWndExtra
		GetModuleHandleA( NULL ), 				// hInstance
		NULL, 									// hIcon;
		NULL, 									// hCursor
		(HBRUSH) GetStockBrush( LTGRAY_BRUSH ),	// hbrBackground
		NULL, 									// lpszMenuName
		WINDOW_CLASS_NAME 						// lpszClassName
	};
    RegisterClassW( &wc );

	HWND hwnd = CreateWindowExW( WS_EX_TOPMOST, wc.lpszClassName, 
		localization[ lang ].title, WS_VISIBLE | WS_OVERLAPPEDWINDOW, 
		CW_USEDEFAULT, CW_USEDEFAULT, max( 600, bounds.right - bounds.left ) + 50 , bounds.bottom - bounds.top + 150,
		NULL, NULL, GetModuleHandleW( NULL ), 0 );

	COLORREF background = RGB( 0, 0, 0 );

	Gdiplus::Pen* pens[] = {
		new Gdiplus::Pen( Gdiplus::Color( 255, 0, 0, 0 ), 5 ),
		new Gdiplus::Pen( Gdiplus::Color( 255, 0, 0, 255 ), 5 ),
		new Gdiplus::Pen( Gdiplus::Color( 255, 255, 0, 0 ), 5 ),
		new Gdiplus::Pen( Gdiplus::Color( 255, 0, 128, 0 ), 5 ),
	};

	Gdiplus::Pen* highlights[] = {
		new Gdiplus::Pen( Gdiplus::Color( 128, 255, 255, 64 ), 28 ),
		new Gdiplus::Pen( Gdiplus::Color( 128, 150, 100, 150), 28 )
	};

	Gdiplus::Pen* penEraser = new Gdiplus::Pen( Gdiplus::Color( 0, 0, 0, 0), 25 );
	Gdiplus::Pen* highlightEraser = new Gdiplus::Pen( Gdiplus::Color( 0, 0, 0, 0), 50 );

	struct MakeAnnotationsData makeAnnotationsData = {		
		bounds,
		pens,
		highlights,
		penEraser,
		highlightEraser,
	};


	HMENU penMenu = CreatePopupMenu();
	MENUITEMINFOA penItems[] =  { 
		{ sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 1, 0, 0, 0, 0, 0, 0, penIcon( pens[ 0 ] ) },
		{ sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 2, 0, 0, 0, 0, 0, 0, penIcon( pens[ 1 ] ) },
		{ sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 3, 0, 0, 0, 0, 0, 0, penIcon( pens[ 2 ] ) },
		{ sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 4, 0, 0, 0, 0, 0, 0, penIcon( pens[ 3 ] ) },
	};
	for( int i = 0; i < sizeof( penItems ) / sizeof( *penItems ); ++i ) {
		InsertMenuItemA( penMenu, i, TRUE, &penItems[ i ] );
	}
	makeAnnotationsData.penMenu = penMenu;

	HMENU highlightMenu = CreatePopupMenu();
	MENUITEMINFOA highlightItems[] =  { 
		{ sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 1, 0, 0, 0, 0, 0, 0, penIcon( highlights[ 0 ] ) },
		{ sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 2, 0, 0, 0, 0, 0, 0, penIcon( highlights[ 1 ] ) },
	};
	for( int i = 0; i < sizeof( highlightItems ) / sizeof( *highlightItems ); ++i ) {
		InsertMenuItemA( highlightMenu, i, TRUE, &highlightItems[ i ] );
	}
	makeAnnotationsData.highlightMenu = highlightMenu;

	makeAnnotationsData.penButton = CreateWindowW( L"BUTTON", localization[ lang ].pen,
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
		7, 10, 113, 30, hwnd, NULL, GetModuleHandleW( NULL ), NULL );

	makeAnnotationsData.highlightButton = CreateWindowW( L"BUTTON", localization[ lang ].highlight,
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
		126, 10, 113, 30, hwnd, NULL, GetModuleHandleW( NULL ), NULL );

	makeAnnotationsData.eraseButton = CreateWindowW( L"BUTTON", localization[ lang ].erase,
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
		244, 10, 113, 30, hwnd, NULL, GetModuleHandleW( NULL ), NULL );

	makeAnnotationsData.doneButton = CreateWindowW( L"BUTTON", localization[ lang ].done,
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
		375, 10, 113, 30, hwnd, NULL, GetModuleHandleW( NULL ), NULL );

	SetWindowLongPtrA( hwnd, GWLP_USERDATA, (LONG_PTR)&makeAnnotationsData );
	UpdateWindow( hwnd );

	HDC dc = GetDC( hwnd );
	HBITMAP backbuffer = CreateCompatibleBitmap( dc, bounds.right - bounds.left, bounds.bottom - bounds.top );
	makeAnnotationsData.backbuffer = CreateCompatibleDC( dc );
	SelectObject( makeAnnotationsData.backbuffer, backbuffer );
	makeAnnotationsData.snippet = CreateCompatibleDC( dc );
	SelectObject( makeAnnotationsData.snippet, snippet );
	ReleaseDC( hwnd, dc );
	InvalidateRect( hwnd, NULL, TRUE );
	

	MSG msg = { NULL };
	while( GetMessage( &msg, NULL, 0, 0 ) ) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	delete penEraser;
	delete highlightEraser;
	for( int i = 0; i < sizeof( pens ) / sizeof( *pens ); ++i ) {
		//delete pens[ i ];
	}

	for( int i = 0; i < sizeof( highlights ) / sizeof( *highlights ); ++i ) {
		delete highlights[ i ];
	}

	DestroyWindow( hwnd );
	DeleteObject( makeAnnotationsData.snippet );
	DeleteObject( makeAnnotationsData.backbuffer );
	DeleteObject( backbuffer );

	UnregisterClassW( wc.lpszClassName, GetModuleHandleW( NULL ) );

    if( !makeAnnotationsData.completed ) {
        return EXIT_FAILURE;
    }
    
	return EXIT_SUCCESS;
}

