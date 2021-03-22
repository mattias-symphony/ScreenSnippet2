


static void swap( LONG* a, LONG* b ) {
    LONG t = *a;
    *a = *b;
    *b = t;
}


// Holds current state for a specific display during region selection
struct Display {
    HWND hwnd; // The window covering the display
    MONITORINFOEXA info;
    DEVMODEA mode;
    HDC backbuffer; // Device context for offscreen draw target for the display
    POINT topLeft; // Starting point of drag rect
    POINT bottomRight; // End point of drag rect
    POINT prevTopLeft;  // Stores previous rect coordinates for erasing
    POINT prevBottomRight;  
};


int const MAX_DISPLAYS = 256; // Hard limit of 256 displays...


struct SelectRegionData {
    HPEN pen; // Pen to use for drawing the frame   
    HPEN eraser; // Pen to use to erase the frame (same color as background)
    HBRUSH background; // Brush to use for erasing the insides of the rect (same color as eraser)
    HBRUSH transparent; // Brush to use for filling the inside of the frame (the color used as transparency mask)
    POINT topLeft; // The point on the virtual desktop, in screen coordinate space, of the region origin
    POINT bottomRight;// The point on the virtual desktop, in screen coordinate space, of the current region drag point
    BOOL dragging; // Will be TRUE once the user press the left mouse button and starts dragging
    BOOL done; // Will be TRUE when the user release the left mouse button and region select is completed
    BOOL aborted; // Will be TRUE if the user press Esc to abort region selection
    int displayCount; // Number of displays
    Display displays[ MAX_DISPLAYS ]; // Current state for each display
};


// Map from client coordinates to global, dpi-adjusted coordinates
static void clientToGlobal( struct Display* display, POINT* p ) {
    RECT monRect = display->info.rcMonitor;
    int width = display->mode.dmPelsWidth;
    int height = display->mode.dmPelsHeight;
    double sx = ( monRect.right - monRect.left ) / (double) width;
    double sy = ( monRect.bottom - monRect.top ) / (double) height;
    p->x = display->mode.dmPosition.x + (int)( p->x / sx );
    p->y = display->mode.dmPosition.y + (int)( p->y / sy );
}


// Map from global, dpi-adjusted coordinates to client coordinates 
static void globalToClient( struct Display* display, POINT* p ) {
    RECT monRect = display->info.rcMonitor;
    int width = display->mode.dmPelsWidth;
    int height = display->mode.dmPelsHeight;
    double sx = ( monRect.right - monRect.left ) / (double) width;
    double sy = ( monRect.bottom - monRect.top ) / (double) height;
    p->x = (int)( ( p->x - display->mode.dmPosition.x ) * sx );
    p->y = (int)( ( p->y - display->mode.dmPosition.y ) * sy );
}


// Check for mouse move, mouse button release and ESC keypress at regular intervals
static void CALLBACK timerProc( HWND hwnd, UINT message, UINT_PTR id, DWORD ms ) {
	LOG_INFO( "Enter timerProc: hwnd=%" PRIX64, (uint64_t)(uintptr_t) hwnd );
    struct SelectRegionData* selectRegionData = (struct SelectRegionData*) id;

    // Read current mouse pos in screen coordinates
    POINT p;
    GetCursorPos( &p );
	LOG_INFO( "Cursor pos=%d,%d", p.x, p.y );

    // Map cursor postion to global, dpi-adjusted coordinates (by finding the screen it is on)
    BOOL found = FALSE;
	LOG_INFO( "Find window matching cursor pos" );
    for( int i = 0; i < selectRegionData->displayCount; ++i ) {
        RECT r;
        GetWindowRect( selectRegionData->displays[ i ].hwnd, &r );      
        ++r.right; // Adjust the region to include rightmost and bottommost pixels, as window rect excludes bottom-right 
        ++r.bottom;
		LOG_INFO( "    hwnd=%" PRIX64 " bounds= %d,%d,%d,%d", (uint64_t)(uintptr_t) selectRegionData->displays[ i ].hwnd,
			r.left, r.top, r.right, r.bottom );
        if( PtInRect( &r, p ) ) {
			LOG_INFO( "        Window contains cursor %d,%d", p.x, p.y );
            ScreenToClient( selectRegionData->displays[ i ].hwnd, &p );
			LOG_INFO( "        Cursor transformed to client space %d,%d", p.x, p.y );
            clientToGlobal( &selectRegionData->displays[ i ], &p );
			LOG_INFO( "        Cursor transformed to global space %d,%d", p.x, p.y );
            found = TRUE;
            break;
        } 
    }

    // Abort if user press Esc
    if( (GetAsyncKeyState( VK_ESCAPE ) & 0x8000 ) != 0 ) {
		LOG_INFO( "User aborted by pressing ESC" );
        selectRegionData->aborted = TRUE;
    }

    if( found ) {
        // Update dragging rect
        if( selectRegionData->dragging ) {
            selectRegionData->bottomRight.x = p.x;
            selectRegionData->bottomRight.y = p.y;
            // Invalidate each window to cause redraw of rect
            for( int i = 0; i < selectRegionData->displayCount; ++i ) {
                InvalidateRect( selectRegionData->displays[ i ].hwnd, NULL, FALSE );
				LOG_INFO( "Invalidating window hwnd=%" PRIX64, (uint64_t)(uintptr_t) selectRegionData->displays[ i ].hwnd );
            }
        }
    } else {
		LOG_INFO( "No window found containing cursor pos" );
	}

    // Check if user have let go of mouse button
    DWORD vk_button = GetSystemMetrics( SM_SWAPBUTTON ) ? VK_RBUTTON : VK_LBUTTON;
    if( selectRegionData->dragging && ( GetAsyncKeyState( vk_button ) & 0x8000 ) == 0 ) {
		LOG_INFO( "Mouse button released(%s button)", vk_button == VK_RBUTTON ? "right" : "left" );
        if( ( selectRegionData->bottomRight.x - selectRegionData->topLeft.x ) == 0 ||
         ( selectRegionData->bottomRight.y - selectRegionData->topLeft.y ) == 0 ) {				
			selectRegionData->dragging = FALSE;
			LOG_INFO( "User clicked and released on the same point, so ignore this selection" );
			LOG_INFO( "Dragging stopped" );
        } else {
            // Complete region selection
            selectRegionData->dragging = FALSE;
            selectRegionData->done = TRUE;
			LOG_INFO( "Region selection completed: %d,%d,%d,%d", selectRegionData->topLeft.x, selectRegionData->topLeft.y, selectRegionData->bottomRight.x, selectRegionData->bottomRight.y );
			LOG_INFO( "Dragging stopped" );
        }
    }

    // Set this function to be called again in 16ms ( we update at ~60hz)
    SetTimer( hwnd, (UINT_PTR)selectRegionData, 16, timerProc );
}


// Creates a rectangle path in the client area, given two points in screenspace coordinates
// Only creates a draw path, which caller can draw using StrokeAndFillPath
static void createRectPath( HWND hwnd, HDC dc, POINT a, POINT b ) {
    BeginPath( dc );
    MoveToEx( dc, a.x, a.y, NULL);
    LineTo( dc, b.x, a.y );
    LineTo( dc, b.x, b.y );
    LineTo( dc, a.x, b.y );
    LineTo( dc, a.x, a.y );
    EndPath( dc );
}


static LRESULT CALLBACK selectRegionWndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    struct SelectRegionData* selectRegionData = (struct SelectRegionData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
    switch( message ) {
        case WM_ERASEBKGND: {
			LOG_INFO( "WM_ERASEBKGND hwnd=%" PRIX64, (uint64_t)(uintptr_t) hwnd );
            // Only do the default background clear before the user starts dragging
            // Once dragging starts, we clear by erasing the previous rect
            if( selectRegionData && selectRegionData->dragging ) {
				LOG_INFO( "Erasing blocked" );
                return 1;
            } else {
				LOG_INFO( "Erasing allowed" );
			}
        } break;
        case WM_PAINT: {
			LOG_INFO( "WM_PAINT hwnd=%" PRIX64, (uint64_t)(uintptr_t) hwnd );

            // Find the Display instance for this window
            struct Display* display = NULL;
            int index = 0;
			LOG_INFO( "Finding display" );
            for( int i = 0; i < selectRegionData->displayCount; ++i ) {
                if( selectRegionData->displays[ i ].hwnd == hwnd ) {
                    display = &selectRegionData->displays[ i ];
					LOG_INFO( "Display found (%d): hwnd=%" PRIX64, i, (uint64_t)(uintptr_t) selectRegionData->displays[ i ].hwnd );
                    break;
                }
            }
			if( !display ) {
				LOG_ERROR( "Display not found" );
			}
            // Draw the current dragged rect onto this window - we draw regardless of whether the rect is inside the
            // window or not - we just let windows handle clipping for us
            if( selectRegionData->dragging && display ) {
                // Swap coordinates if necessary, so p1 is always top-left and p2 is always bottom-right, regardless
                // of the drag direction
                POINT cp1 = selectRegionData->topLeft;
                POINT cp2 = selectRegionData->bottomRight;
                if( cp1.x > cp2.x ) {
                    swap( &cp1.x, &cp2.x );
                }
                if( cp1.y > cp2.y ) {
                    swap( &cp1.y, &cp2.y );
                }

                POINT pp1 = display->prevTopLeft;
                POINT pp2 = display->prevBottomRight;
                if( pp1.x > pp2.x ) {
                    swap( &pp1.x, &pp2.x );
                }
                if( pp1.y > pp2.y ) {
                    swap( &pp1.y, &pp2.y );
                }

                // Store coordinates for the next erase operation
                display->prevTopLeft = selectRegionData->topLeft;
                display->prevBottomRight = selectRegionData->bottomRight;

				LOG_INFO( "Prev rect (global coords) %d,%d,%d,%d", pp1.x, pp1.y, pp2.x, pp2.y );
				LOG_INFO( "Curr rect (global coords) %d,%d,%d,%d", cp1.x, cp1.y, cp2.x, cp2.y );

                // Map from global, dpi-adjusted coordinates to client coordinates so we can draw on the current window
                globalToClient( display, &cp1 );
                globalToClient( display, &cp2 );
                globalToClient( display, &pp1 );
                globalToClient( display, &pp2 );

                // Erase previous rect
                createRectPath( hwnd, display->backbuffer, pp1, pp2 );
                SelectObject( display->backbuffer, selectRegionData->eraser );
                SelectObject( display->backbuffer, selectRegionData->background );
                StrokeAndFillPath( display->backbuffer );
            
                // Draw current rect
                createRectPath( hwnd, display->backbuffer, cp1, cp2 );
                SelectObject( display->backbuffer, selectRegionData->pen );
                SelectObject( display->backbuffer, selectRegionData->transparent );
                StrokeAndFillPath( display->backbuffer );

                // Calculate the bounds of the area affected by erase/draw operations
                RECT curr = { cp1.x, cp1.y, cp2.x, cp2.y };
                RECT prev = { pp1.x, pp1.y, pp2.x, pp2.y };
                RECT frame;
                UnionRect( &frame, &curr, &prev );          

				LOG_INFO( "Erasing rect (client coords) %d,%d,%d,%d", prev.left, prev.top, prev.right, prev.bottom );
				LOG_INFO( "Drawing rect (client coords) %d,%d,%d,%d", curr.left, curr.top, curr.right, curr.bottom );

                RECT client;
                GetClientRect( hwnd, &client );

                RECT bounds;
                IntersectRect( &bounds, &frame, &client );
                InflateRect( &bounds, 4, 4 );

				LOG_INFO( "Copying area %d,%d,%d,%d", bounds.left, bounds.top, bounds.right, bounds.bottom );
                // Copy the updated area from backbuffer to the window
                PAINTSTRUCT ps; 
                HDC dc = BeginPaint( hwnd, &ps );
                BitBlt( dc, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, 
                    display->backbuffer, bounds.left, bounds.top, SRCCOPY );
                EndPaint( hwnd, &ps );
            }
        } break;
        case WM_LBUTTONDOWN: {
			LOG_INFO( "WM_LBUTTONDOWN hwnd=%" PRIX64, (uint64_t)(uintptr_t) hwnd );
            // Find the Display instance for this window
            struct Display* display = NULL;
            int index = 0;
			LOG_INFO( "Finding display" );
            for( int i = 0; i < selectRegionData->displayCount; ++i ) {
                if( selectRegionData->displays[ i ].hwnd == hwnd ) {
                    display = &selectRegionData->displays[ i ];
					LOG_INFO( "Display found (%d): hwnd=%" PRIX64, i, (uint64_t)(uintptr_t) selectRegionData->displays[ i ].hwnd );
                    break;
                }
            }
			if( !display ) {
				LOG_ERROR( "Display not found" );
			}
            // Store the starting point of the drag rect, in global, dpi-adjusted coordinates
            POINT p = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
			LOG_INFO( "Mouse coordinates (client coords): %d,%d", p.x, p.y );
            clientToGlobal( display, &p );
			LOG_INFO( "Mouse coordinates (global coords): %d,%d", p.x, p.y );
            selectRegionData->topLeft.x = p.x;
            selectRegionData->topLeft.y = p.y; 
            selectRegionData->bottomRight.x = p.x;
            selectRegionData->bottomRight.y = p.y;
            selectRegionData->dragging = TRUE;
            InvalidateRect( hwnd, NULL, FALSE );
			LOG_INFO( "Dragging started" );
        } break;
    }
    return DefWindowProc( hwnd, message, wparam, lparam);
}


// Holds a list of bounding rects for all displays
struct FindScreensData {
    int count;
    MONITORINFOEXA info[ MAX_DISPLAYS ];
    DEVMODEA mode[ MAX_DISPLAYS ];
};


// Callback used when enumerating displays. Just adds each bounding rect to the list.
static BOOL CALLBACK findScreens( HMONITOR monitor, HDC dc, LPRECT rect, LPARAM lparam ) {  
    struct FindScreensData* findScreensData = (struct FindScreensData*) lparam;
    MONITORINFOEXA info;
    info.cbSize = sizeof( info );
    GetMonitorInfoA( monitor, (MONITORINFO*) &info );
    DEVMODEA mode;
    EnumDisplaySettingsA( info.szDevice, ENUM_CURRENT_SETTINGS, &mode );

    findScreensData->info[ findScreensData->count ] = info;
    findScreensData->mode[ findScreensData->count ] = mode;
	
	LOG_INFO( "Monitor found(%d) monitor=%" PRIX64, findScreensData->count, (uint64_t)(uintptr_t) monitor );
    ++findScreensData->count;

	LOG_INFO( "    info.szDevice=%s", info.szDevice );	
	LOG_INFO( "    info.rcMonitor=%d,%d,%d,%d", info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom );	
	LOG_INFO( "    info.rcWork=%d,%d,%d,%d", info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom );	
	LOG_INFO( "    info.primary=%s", ( info.dwFlags & MONITORINFOF_PRIMARY ) ? "TRUE" : "FALSE" );	

	LOG_INFO( "    mode.dmPelsWidth=%d", mode.dmPelsWidth );	
	LOG_INFO( "    mode.dmPelsHeight=%d", mode.dmPelsHeight );	
	LOG_INFO( "    mode.dmPosition=%d,%d", mode.dmPosition.x, mode.dmPosition.y );	

    if( findScreensData->count >= sizeof( findScreensData->info ) / sizeof( *findScreensData->info ) ) {
		LOG_ERROR( "More than %d displays found - not supported.", MAX_DISPLAYS );
        return FALSE;
    }
    return TRUE;
}


// Let the user select a region of the full virtual desktop. Selction may span multiple displays.
static int selectRegion( RECT* region ) {
    // Enumerate all displays
    struct FindScreensData findScreensData = { 0 };
	LOG_INFO( "Enumerating monitors" );
    EnumDisplayMonitors( NULL, NULL, findScreens, (LPARAM) &findScreensData );
    if( findScreensData.count <= 0 ) {
		LOG_INFO( "No monitors found, selectRegion failed." );
        return EXIT_FAILURE;
    }
	LOG_INFO( "Monitors found: %d", findScreensData.count );

    COLORREF frame = RGB( 255, 255, 255 );
    COLORREF transparent = RGB( 0, 255, 0 );
    COLORREF background = RGB( 0, 0, 0 );

    // Data passed to each selection window to track state
    struct SelectRegionData selectRegionData = { 
        CreatePen( PS_SOLID, 2, frame ),
        CreatePen( PS_SOLID, 2, background ),
        CreateSolidBrush( background ),
        CreateSolidBrush( transparent ),
    };
    
    // Register window class
    WNDCLASSW wc = { 
        CS_OWNDC | CS_HREDRAW | CS_VREDRAW, // style
        (WNDPROC) selectRegionWndProc,      // lpfnWndProc
        0,                                  // cbClsExtra
        0,                                  // cbWndExtra
        GetModuleHandleA( NULL ),           // hInstance
        NULL,                               // hIcon;
        LoadCursor( NULL, IDC_CROSS ),      // hCursor
        selectRegionData.background,        // hbrBackground
        NULL,                               // lpszMenuName
        WINDOW_CLASS_NAME                   // lpszClassName
    };
    RegisterClassW( &wc );


    // Create a window for each display, covering it entirely as a semi-transparent overlay
    int count = findScreensData.count;
    HWND hwnd[ MAX_DISPLAYS ] = { NULL };
    selectRegionData.displayCount = count;
    for( int i = 0; i < count; ++i ) {
        // Store display data
        struct Display* display = &selectRegionData.displays[ i ];
        display->info = findScreensData.info[ i ];
        display->mode = findScreensData.mode[ i ];

        // Create window
        RECT bounds = findScreensData.info[ i ].rcMonitor;
        hwnd[ i ] = display->hwnd = CreateWindowExW( WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, 
            NULL, WS_VISIBLE, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, 
            NULL, NULL, GetModuleHandleA( NULL ), 0 );
		
		LOG_INFO( "Creating window for display(%d) hwnd=%" PRIX64 " bounds=%d,%d,%d,%d", i, (uint64_t)(uintptr_t) hwnd[ i ], bounds.left, bounds.top, bounds.right, bounds.bottom );
        
        // Create off-screen drawing surface for window
        HDC dc = GetDC( display->hwnd );
		LOG_INFO( "    dc=%" PRIX64, (uint64_t)(uintptr_t) dc );
        HBITMAP backbuffer = CreateCompatibleBitmap( dc, bounds.right - bounds.left, bounds.bottom - bounds.top );
		LOG_INFO( "    backbuffer=%" PRIX64, (uint64_t)(uintptr_t) backbuffer );
        display->backbuffer = CreateCompatibleDC( dc );
		LOG_INFO( "    backbufferDc=%" PRIX64, (uint64_t)(uintptr_t) display->backbuffer );
        SelectObject( display->backbuffer, backbuffer );
        ReleaseDC( display->hwnd, dc );

        // Set window transparency
        SetWindowLongPtrA( hwnd[ i ], GWLP_USERDATA, (LONG_PTR)&selectRegionData );
        SetWindowLongA( hwnd[ i ], GWL_STYLE, WS_VISIBLE );
        SetLayeredWindowAttributes( hwnd[ i ], transparent, 100, LWA_ALPHA | LWA_COLORKEY );
        UpdateWindow( hwnd[ i ] );

        // Fix needed for running Win7 with classic theme. If we don't set position here, the whole window will be offset
        SetWindowPos( hwnd[ i ], NULL, bounds.left, bounds.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED );
    }

    // Call timer function directly - it will set up a timer based call to itself as the last thing it does
    timerProc( hwnd[ 0 ], 0, (UINT_PTR)&selectRegionData, 0 );

    // Main loop, keeps running while there are still windows open, and the user have not aborted or completed selection
    int running = count;
	LOG_INFO( "Entering main windows loop for region selection" );
    while( running && !selectRegionData.done && !selectRegionData.aborted )  {
        // Process messages for each window
        for( int i = 0; i < count; ++i ) {
            if( hwnd[ i ] ) {
                MSG msg = { NULL };
                // Normally we would use GetMessage rather than PeekMessage, but it would block the processing of the 
                // other windows, and require us to run each windows processing on a separate thread. PeekMessage allows
                // us to interleave the message loop for all windows, but would max out the thread if we don't add an
                // explicity `Sleep` to the loop.
                while( PeekMessageA( &msg, hwnd[ i ], 0, 0, PM_REMOVE ) ) {
                    if( msg.message == WM_CLOSE ) { // Detect closing of windows
						LOG_INFO( "Destroying window %d (%d left) hwnd=%" PRIX64, i, running, (uint64_t)(uintptr_t) hwnd[i] );
                        DestroyWindow( hwnd[ i ] ); 
                        hwnd[ i ] = NULL;
                        --running;
                    }
                    TranslateMessage( &msg );
                    DispatchMessage( &msg );
                }
            }
        }
        Sleep( 16 ); // Limit the update rate, as we use PeekMessage rather then GetMessage which would be blocking
    }
	LOG_INFO( "Exit main windows loop for region selection" );

    // Cleanup
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
	LOG_INFO( "Cleanup done" );
    
    // If the user aborted, we don't return the region
    if( !selectRegionData.done ) {
		LOG_INFO( "User aborted screen selection" );
        return EXIT_FAILURE;
    }

    RECT selectedRegion = { selectRegionData.topLeft.x, selectRegionData.topLeft.y, 
        selectRegionData.bottomRight.x, selectRegionData.bottomRight.y, };
	LOG_INFO( "Selected region: %d,%d,%d,%d", selectedRegion.left, selectedRegion.top, selectedRegion.right, selectedRegion.bottom );

    // Swap top/bottom and left/right as necessary (user can drag in any direction they want)   
    if( selectedRegion.left > selectedRegion.right ) {
        swap( &selectedRegion.left, &selectedRegion.right );
		LOG_INFO( "Swapped left/right region points" );
    }
    if( selectedRegion.top > selectedRegion.bottom ) {
        swap( &selectedRegion.top, &selectedRegion.bottom );
		LOG_INFO( "Swapped top/bottom region points" );
    }

	LOG_INFO( "Region selection successful: %d,%d,%d,%d", selectedRegion.left, selectedRegion.top, selectedRegion.right, selectedRegion.bottom );
    *region = selectedRegion;
    return EXIT_SUCCESS;
}


