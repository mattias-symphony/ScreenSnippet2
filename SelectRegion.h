


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
    struct SelectRegionData* selectRegionData = (struct SelectRegionData*) id;

    // Read current mouse pos in screen coordinates
    POINT p;
    GetCursorPos( &p );

    // Map cursor postion to global, dpi-adjusted coordinates (by finding the screen it is on)
    BOOL found = FALSE;
    for( int i = 0; i < selectRegionData->displayCount; ++i ) {
        RECT r;
        GetWindowRect( selectRegionData->displays[ i ].hwnd, &r );      
        ++r.right; // Adjust the region to include rightmost and bottommost pixels, as window rect excludes bottom-right 
        ++r.bottom;
        if( PtInRect( &r, p ) ) {
            ScreenToClient( selectRegionData->displays[ i ].hwnd, &p );
            clientToGlobal( &selectRegionData->displays[ i ], &p );
            found = TRUE;
            break;
        } 
    }

    // Abort if user press Esc
    if( (GetAsyncKeyState( VK_ESCAPE ) & 0x8000 ) != 0 ) {
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
            }
        }
    }

    // Check if user have let go of mouse button
    DWORD vk_button = GetSystemMetrics( SM_SWAPBUTTON ) ? VK_RBUTTON : VK_LBUTTON;
    if( selectRegionData->dragging && ( GetAsyncKeyState( vk_button ) & 0x8000 ) == 0 ) {
        if( ( selectRegionData->bottomRight.x - selectRegionData->topLeft.x ) == 0 ||
            ( selectRegionData->bottomRight.y - selectRegionData->topLeft.y ) == 0 ) {
        selectRegionData->dragging = FALSE;
        } else {
            // Complete region selection
            selectRegionData->dragging = FALSE;
            selectRegionData->done = TRUE;
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
            // Only do the default background clear before the user starts dragging
            // Once dragging starts, we clear by erasing the previous rect
            if( selectRegionData && selectRegionData->dragging ) {
                return 1;
            }
        } break;
        case WM_PAINT: {
            // Find the Display instance for this window
            struct Display* display = NULL;
            int index = 0;
            for( int i = 0; i < selectRegionData->displayCount; ++i ) {
                if( selectRegionData->displays[ i ].hwnd == hwnd ) {
                    display = &selectRegionData->displays[ i ];
                    break;
                }
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

                RECT client;
                GetClientRect( hwnd, &client );

                RECT bounds;
                IntersectRect( &bounds, &frame, &client );
                InflateRect( &bounds, 4, 4 );

                // Copy the updated area from backbuffer to the window
                PAINTSTRUCT ps; 
                HDC dc = BeginPaint( hwnd, &ps );
                BitBlt( dc, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, 
                    display->backbuffer, bounds.left, bounds.top, SRCCOPY );
                EndPaint( hwnd, &ps );
            }
        } break;
        case WM_LBUTTONDOWN: {
            // Find the Display instance for this window
            struct Display* display = NULL;
            int index = 0;
            for( int i = 0; i < selectRegionData->displayCount; ++i ) {
                if( selectRegionData->displays[ i ].hwnd == hwnd ) {
                    display = &selectRegionData->displays[ i ];
                    break;
                }
            }
            // Store the starting point of the drag rect, in global, dpi-adjusted coordinates
            POINT p = { GET_X_LPARAM( lparam ), GET_Y_LPARAM( lparam ) };
            clientToGlobal( display, &p );
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
    ++findScreensData->count;
    if( findScreensData->count >= sizeof( findScreensData->info ) / sizeof( *findScreensData->info ) ) {
        return FALSE;
    }
    return TRUE;
}


// Let the user select a region of the full virtual desktop. Selction may span multiple displays.
static int selectRegion( RECT* region ) {
    // Enumerate all displays
    struct FindScreensData findScreensData = { 0 };
    EnumDisplayMonitors( NULL, NULL, findScreens, (LPARAM) &findScreensData );
    if( findScreensData.count <= 0 ) {
        return EXIT_FAILURE;
    }

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
        
        // Create off-screen drawing surface for window
        HDC dc = GetDC( display->hwnd );
        HBITMAP backbuffer = CreateCompatibleBitmap( dc, bounds.right - bounds.left, bounds.bottom - bounds.top );
        display->backbuffer = CreateCompatibleDC( dc );
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
    
    // If the user aborted, we don't return the region
    if( !selectRegionData.done ) {
        return EXIT_FAILURE;
    }

    RECT selectedRegion = { selectRegionData.topLeft.x, selectRegionData.topLeft.y, 
        selectRegionData.bottomRight.x, selectRegionData.bottomRight.y, };

    // Swap top/bottom and left/right as necessary (user can drag in any direction they want)   
    if( selectedRegion.left > selectedRegion.right ) {
        swap( &selectedRegion.left, &selectedRegion.right );
    }
    if( selectedRegion.top > selectedRegion.bottom ) {
        swap( &selectedRegion.top, &selectedRegion.bottom );
    }

    *region = selectedRegion;
    return EXIT_SUCCESS;
}


