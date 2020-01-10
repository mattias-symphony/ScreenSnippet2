
// A list of line segments making up a single stroke
struct Stroke {
    BOOL highlighter; // A stroke can be done with pen or highlighter
    int penIndex; // The index (color) of the pen or highlighter used
    int capacity; // `points` array is dynamically grown (by doubling). `capacity` holds current max capacity
    int count; // Current number of points
    Gdiplus::Point* points; // The points making up the stroke
};


// State for the annotations window, this is set up and attached to the window in the `makeAnnotations` function
struct MakeAnnotationsData {
    float snippetScale; // Scale of the display the snippet was captured on
    float scale; // Current scale the snippet is displayed at
    RECT bounds; // Bounds of the screen snippet
    Gdiplus::Pen** pens; // Array of pens selectable via the pen menu...
    Gdiplus::Pen** highlighters; // ...and corresponding array for highlighters
    Gdiplus::Pen* penEraser; // Erasing uses a slightly larger pen so user doesn't have to do pixel perfect selection
    Gdiplus::Pen* highlightEraser;
    HDC backbuffer; // Device context for offscreen draw target (for flicker-free drawing)
    HDC snippet; // Device context for the screen snippet bitmap to annotate
    BOOL highlighter; // Will be TRUE when user have selected `highlighter`, FALSE when `pen` is selected
    int penIndex; // Index of the currently selected pen
    int highlightIndex; // Index of the currently selected highlighter
    BOOL eraser; // Will be TRUE when in `erase` mode`
    BOOL penDown; // Will be TRUE, while holding down the left mouse button, with pen or highlighter selected
    int strokeCount; // Number of strokes (a single stroke can have any length)
    struct Stroke strokes[ 256 ]; // Hardcoded limit of 256 strokes. Could be made dynamic if necessary.    
    HWND penButton; // Handles to the buttons
    HWND highlightButton;
    HWND eraseButton;
    HWND doneButton;
    HMENU penMenu; // Handles to the dropdown menus to select pens and highlighters
    HMENU highlightMenu;
    BOOL completed; // Will be set to true when/if user press `Done` button
    HCURSOR arrowCursor;
    HCURSOR penCursor;
    HCURSOR eraserCursor;
    int buttonHeight; // Last calculated scaled height of the buttons
};


// Creates a new stroke, and allocates memory for its points. If maximum strokes have been reached, it will do nothing
void newStroke( struct MakeAnnotationsData* data, BOOL highlighter, int penIndex ) {
    if( data->strokeCount < sizeof( data->strokes ) / sizeof( *data->strokes ) ) {
        struct Stroke* stroke = &data->strokes[ data->strokeCount ];
        stroke->highlighter = highlighter;
        stroke->penIndex = penIndex;
        stroke->capacity = 256;
        stroke->count = 0;
        stroke->points = (Gdiplus::Point*) malloc( sizeof( Gdiplus::Point ) * stroke->capacity );
        ++data->strokeCount;
    }
}


// Adds a point/segment to a stroke, but only if the distance is far enough from the previous added point
// The distance needed is determined by whether a pen or highlighter is used, and whether `force` is TRUE
void addStrokePoint( struct MakeAnnotationsData* data, POINT* p, BOOL force ) {
    // Filter out points which are too close to the previous point. We do this to better leverage the curve
    // renderer and get smoother, more natural looking strokes even though using a mouse to draw
    if( data->strokeCount > 0 ) {
        struct Stroke* stroke = &data->strokes[ data->strokeCount - 1 ];
        if( stroke->count > 0 ) {
            int dx = stroke->points[ stroke->count - 1 ].X - p->x;
            int dy = stroke->points[ stroke->count - 1 ].Y - p->y;
            int dist = (int)sqrtf( (float)( dx * dx + dy * dy ) );
            int const thresholdForce = 5;
            int const thresholdPen = 15;
            int const thresholdHighlighter = 25;
            if( dist < ( force ? thresholdForce : ( data->highlighter ? thresholdHighlighter : thresholdPen ) ) ) {
                return;
            }
        }

        // Resize array if needed
        if( stroke->count >= stroke->capacity ) {
            stroke->capacity *= 2;
            stroke->points = (Gdiplus::Point*) realloc( stroke->points, sizeof( Gdiplus::Point) * stroke->capacity );
        }

        // Add the point
        stroke->points[ stroke->count++ ] = Gdiplus::Point( p->x, p->y );
    }
}


float getDisplayScaling( HWND hwnd ) {
    HMONITOR monitor = MonitorFromWindow( hwnd, MONITOR_DEFAULTTONEAREST );
    if( !monitor || !GetDpiForMonitorPtr ) {
        return 0.0f;
    }

    UINT dpiX;
    UINT dpiY;
    GetDpiForMonitorPtr( monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY );
    if( dpiX != dpiY ) {
        return 0.0f;
    }

    float const windowsUnscaledDpi = 96.0f;
    return dpiX / windowsUnscaledDpi;
}


void resizeWindow( HWND hwnd, float prevScale, float newScale ) {
    RECT rect;
    GetWindowRect( hwnd, &rect );
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    w = (int)( w / prevScale );
    h = (int)( h / prevScale );
    w = (int)( w * newScale );
    h = (int)( h * newScale );
    SetWindowPos( hwnd, 0, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOREPOSITION );
}


int resizeButton( HWND button, float prevScale, float newScale ) {
    RECT rect;
    GetWindowRect( button, &rect );
    MapWindowPoints( HWND_DESKTOP, GetParent( button ), (LPPOINT) &rect, 2 );
    int x = rect.left;
    int y = rect.top;
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    x = (int)( x / prevScale );
    y = (int)( y / prevScale );
    w = (int)( w / prevScale );
    h = (int)( h / prevScale );
    x = (int)( x * newScale );
    y = (int)( y * newScale );
    w = (int)( w * newScale );
    h = (int)( h * newScale );  
    SetWindowPos( button, 0, x, y, w, h, SWP_NOZORDER | SWP_NOREPOSITION );

    NONCLIENTMETRICSA metrics = {};
    metrics.cbSize = sizeof( metrics );
    SystemParametersInfoA( SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0 );
    metrics.lfCaptionFont.lfHeight = (LONG)( h * 0.8f );
    HFONT font = CreateFontIndirectA( &metrics.lfCaptionFont );
    SendMessage( button, WM_SETFONT, (LPARAM) font, TRUE );
    return h;
}


static LRESULT CALLBACK makeAnnotationsWndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    struct MakeAnnotationsData* data = (struct MakeAnnotationsData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
    int const spaceForButtons = data ? (int)( data->buttonHeight * 1.7f ) : 50; // Reserve some pixels at the top for the buttons

    switch( message ) {
        case WM_NCCREATE: {
            if( EnableNonClientDpiScalingPtr ) {
                EnableNonClientDpiScalingPtr( hwnd ); // Makes titlebar and buttons auto-scale with DPI
            }
        } break;


        case WM_MOVE: {
            if( data ) {
                // Detect if scaling have changed (if window was moved to a different display)
                float scale = getDisplayScaling( hwnd );
                if( scale == 0.0f || data->snippetScale == 0.0f || scale <= data->snippetScale ) {
                    scale = 1.0f;
                } else {
                    scale = 1.0f + ( scale - data->snippetScale );
                }

                // Rescale window if necessary
                if( data->scale != scale ) {
                    resizeWindow( hwnd, data->scale, scale );
                    resizeButton( data->penButton, data->scale, scale );
                    resizeButton( data->highlightButton, data->scale, scale );
                    resizeButton( data->eraseButton, data->scale, scale );
                    data->buttonHeight = resizeButton( data->doneButton, data->scale, scale );

                    data->scale = scale;

                    // Clear background
                    HDC dc = GetDC( hwnd );
                    RECT r;
                    GetClientRect( hwnd, &r  );
                    FillRect( dc, &r, GetStockBrush( LTGRAY_BRUSH ) );
                    ReleaseDC( hwnd, dc );
                }
            }
        } break;

        case WM_COMMAND: {
            // Handle button clicks
            if( HIWORD( wparam ) == BN_CLICKED ) {
                if( (HWND) lparam == data->doneButton ) {
                    data->completed = TRUE;
                    // As the backbuffer already contains the complete annotated image, copy it over 
                    // the snippet bitmap
                    RECT bounds = data->bounds;
                    BitBlt( data->snippet, bounds.left, bounds.top, bounds.right - bounds.left, 
                        bounds.bottom - bounds.top, data->backbuffer, 0, 0, SRCCOPY );
                    PostQuitMessage( 0 ); // Exit the annotation part of the program
                }
                // Show the pen selection submenu and let the user select an item
                if( (HWND) lparam == data->penButton ) {
                    RECT bounds;
                    GetWindowRect( data->penButton, &bounds );
                    POINT p = { bounds.left, bounds.bottom };
                    DWORD item = TrackPopupMenu( data->penMenu, TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL );               
                    if( item > 0 ) {
                        data->penIndex = item - 1;
                    }
                    // Switch to `pen` mode whether the user selected a menu item or not (use last selected pen index)
                    data->highlighter = FALSE;
                    data->eraser = FALSE;
                }
                // Show the highlighter selection submenu and let the user select an item
                if( (HWND) lparam == data->highlightButton ) {
                    RECT bounds;
                    GetWindowRect( data->highlightButton, &bounds );
                    POINT p = { bounds.left, bounds.bottom };
                    DWORD item = TrackPopupMenu( data->highlightMenu,  TPM_RETURNCMD, p.x, p.y, 0, hwnd, NULL );                
                    if( item > 0 ) {
                        data->highlightIndex = item - 1;
                    }
                    // Switch to `highlighter` mode whether the user selected a menu item or not (use last index)
                    data->highlighter = TRUE;
                    data->eraser = FALSE;
                }
                // Enter `erase` mode
                if( (HWND) lparam == data->eraseButton ) {
                    data->penDown = FALSE;
                    data->eraser = TRUE;
                }
            }
        } break;

        // As windows will change the cursor for various reasons, we must restore it to the one we want when required
        case WM_SETCURSOR: {
            POINT pos;
            GetCursorPos( &pos );
            if( ScreenToClient( hwnd, &pos ) ) {
                if( pos.y < spaceForButtons ) {
                    SetCursor( data->arrowCursor );
                } else if( data->eraser ) {
                    SetCursor( data->eraserCursor );
                } else {
                    SetCursor( data->penCursor );
                }
            }
        } break;
            
        // Exit the annotation mode if the window is closed
        case WM_CLOSE: {
            PostQuitMessage( 0 );
        } break;

        // Don't let the system erase the background - we handle this by painting the entire surface on WM_PAINT
        case WM_ERASEBKGND: {
            // Only disable the erase after the window have been created and had its user data set, so we get the
            // initial clearing of the background
            if( data ) {
                HDC dc = GetDC( hwnd );
                RECT r;
                GetClientRect( hwnd, &r  );
                r.bottom = r.top + spaceForButtons; 
                FillRect( dc, &r, GetStockBrush( LTGRAY_BRUSH ) );
                ReleaseDC( hwnd, dc );
                return 1;
            }
        } break;

        // When the window is resized, we need to manually clear any areas that have been made visible, as we have
        // disabled WM_ERASEBKGND
        case WM_SIZE: {
            HDC dc = GetDC( hwnd );
            RECT r;
            GetClientRect( hwnd, &r  );
            FillRect( dc, &r, GetStockBrush( LTGRAY_BRUSH ) );
            ReleaseDC( hwnd, dc );
        } break;

        // Redraw the window - mostly happens in response to us calling `InvalidateRect`
        case WM_PAINT: {
            // All drawing happens on the off-screen backbuffer surface, to eliminate flickering

            // Draw the snippet as a background - the lines will be drawn on top. 
            RECT bounds = data->bounds;
            HDC backbuffer = data->backbuffer;

            BitBlt( backbuffer, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, 
                data->snippet, 0, 0, SRCCOPY );

            // Set up the GDI+ rendering. GDI+ is tjhe only way to get semitransparent and antialiased rendering
            Gdiplus::Graphics graphics( backbuffer );
            graphics.SetSmoothingMode( Gdiplus::SmoothingModeHighQuality );

            // Draw all the strokes
            for( int i = 0; i < data->strokeCount; ++i ) {
                struct Stroke* stroke = &data->strokes[ i ];
                // Select the right pen or highlighter
                Gdiplus::Pen* pen = stroke->highlighter ? 
                    data->highlighters[ stroke->penIndex ] :  data->pens[ stroke->penIndex ];
                // Only draw strokes with at least one segment (two points or more)
                if( stroke->count > 1 ) {           
                    graphics.DrawCurve( pen, stroke->points, stroke->count );
                }
            }

            // To make the pen feel a bit more snappy, draw a straight line from the end of the current stroke
            // to the position of the mouse cursor. This line is just temporary and will be replaced by a point
            // in the stroke point list when the mouse button is released
            if( data->penDown && data->strokeCount > 0 ) {
                struct Stroke* stroke = &data->strokes[ data->strokeCount - 1 ];
                // Select the right pen or highlighter
                Gdiplus::Pen* pen = stroke->highlighter ? 
                    data->highlighters[ stroke->penIndex ] :  data->pens[ stroke->penIndex ];
                if( stroke->count > 0 && !stroke->highlighter ) {
                    POINT mouse;
                    GetCursorPos( &mouse );
                    ScreenToClient( hwnd, &mouse );
                    Gdiplus::Point p = stroke->points[ stroke->count - 1 ];
                    graphics.DrawLine( pen, p.X, p.Y, (INT)( mouse.x / data->scale ), (INT)( ( mouse.y - spaceForButtons ) / data->scale ) );
                }
            }

            // Copy the backbuffer to the window so it can be seen
            PAINTSTRUCT ps; 
            HDC dc = BeginPaint( hwnd, &ps );

            if( data->scale == 1.0f ) {
                BitBlt( dc, 0, spaceForButtons, bounds.right - bounds.left, bounds.bottom - bounds.top, 
                    backbuffer, 0, 0, SRCCOPY );
            } else {
                int w = (int)( ( bounds.right - bounds.left ) * data->scale );
                int h = (int)( ( bounds.bottom - bounds.top ) * data->scale );
                SetStretchBltMode( backbuffer, COLORONCOLOR );
                StretchBlt( dc, 0, spaceForButtons, w, h, 
                    backbuffer, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SRCCOPY );
            }

            
            EndPaint( hwnd, &ps );
        } break;

        case WM_LBUTTONDOWN: {
            // Start a new stroke
            if( !data->eraser ) {
                data->penDown = TRUE;
                newStroke( data, data->highlighter, 
                    data->highlighter ? data->highlightIndex : data->penIndex );
                POINT p = { (int)( GET_X_LPARAM( lparam ) / data->scale ), (int)( ( GET_Y_LPARAM( lparam ) - spaceForButtons ) / data->scale ) };
                addStrokePoint( data, &p, TRUE );
                InvalidateRect( hwnd, NULL, FALSE );
                break; // If we are in 'eraser' mode, fall through into the "RBUTTONDOWN" eraser code below
            }
        } // Intentionally no `break;` statement here

        case WM_RBUTTONDOWN: {
            // Remove strokes when the user press the right button or if `erase` mode is enabled and pressing left button
            Gdiplus::Point p = { (int)( GET_X_LPARAM( lparam ) / data->scale ), (int)( ( GET_Y_LPARAM( lparam ) - spaceForButtons ) / data->scale ) };
            HDC backbuffer = data->backbuffer;
            Gdiplus::Graphics graphics( backbuffer );
            graphics.SetSmoothingMode( Gdiplus::SmoothingModeHighQuality );
            if( data->strokeCount > 0 ) {
                // Check all strokes in reverse order (not strictly necessary now as we don't break after deleting a
                // stroke, but will be helpful if we would do that, as strokes drawn on top have higher priority)
                for( int i = data->strokeCount - 1; i >= 0 ; --i ) {
                    struct Stroke* stroke = &data->strokes[ i ];
                    if( stroke->count > 1 ) {           
                        Gdiplus::Pen* pen = stroke->highlighter ? data->highlightEraser : data->penEraser;
                        // Check if the cursor is on this stroke by building a path from it and calling `IsOutlineVisible`
                        Gdiplus::GraphicsPath path;
                        path.AddCurve( stroke->points, stroke->count );
                        if( path.IsOutlineVisible( p, pen, &graphics ) ) {
                            stroke->count = 0;
                            InvalidateRect( hwnd, NULL, TRUE );
                        }
                    }
                }
            }
        } break;

        // When releasing the mouse button, stop drawing
        case WM_LBUTTONUP: {
            if( data->penDown ) {
                data->penDown = FALSE;
                POINT p = { (int)( GET_X_LPARAM( lparam ) / data->scale ), (int)( ( GET_Y_LPARAM( lparam ) - spaceForButtons ) / data->scale ) };
                addStrokePoint( data, &p, TRUE );
                InvalidateRect( hwnd, NULL, FALSE );
            }
        } break;

        // When the mouse moves and the left button is being held, add a point to the current stroke
        case WM_MOUSEMOVE: {
            if( data->penDown ) {
                POINT p = { (int)( GET_X_LPARAM( lparam ) / data->scale ), (int)( ( GET_Y_LPARAM( lparam ) - spaceForButtons ) / data->scale ) };
                addStrokePoint( data, &p, FALSE );
                InvalidateRect( hwnd, NULL, FALSE );
            }
        } break;
    }

    return DefWindowProc( hwnd, message, wparam, lparam);
}


// Create an icon representing a pen, for use in the dropdown menus to select pen or highlighter
HBITMAP WINAPI penIcon( Gdiplus::Pen* pen ) { 
    int const width = 120;
    int const height = 20; 
    int const margin = 20;
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
    graphics.DrawLine( pen, Gdiplus::Point( margin, height / 2 ), Gdiplus::Point( width, height / 2 ) );

    SelectObject( hdcMem, hbmOld ); 
 
    DeleteObject( SelectObject( hdcMem, hbrOld ) ); 
    DeleteDC( hdcMem ); 
    ReleaseDC( hwndDesktop, hdcDesktop ); 
    return paHbm;
}


int makeAnnotations( HMONITOR monitor, HBITMAP snippet, RECT bounds, float snippetScale, int lang ) {
    HICON icon = LoadIcon( GetModuleHandleA( NULL ), MAKEINTRESOURCE( IDR_ICON ) );
    
    // Register window class
    WNDCLASSW wc = { 
        CS_OWNDC | CS_HREDRAW | CS_VREDRAW,     // style
        (WNDPROC) makeAnnotationsWndProc,       // lpfnWndProc
        0,                                      // cbClsExtra
        0,                                      // cbWndExtra
        GetModuleHandleA( NULL ),               // hInstance
        icon,                                   // hIcon
        NULL,                                   // hCursor
        (HBRUSH) GetStockBrush( LTGRAY_BRUSH ), // hbrBackground
        NULL,                                   // lpszMenuName
        WINDOW_CLASS_NAME                       // lpszClassName
    };
    RegisterClassW( &wc );
    

    // Calculate window size from snippet bounds. Add some extra space and a min size to fit buttons
    int width = max(600, bounds.right - bounds.left) + 50;
    int height = bounds.bottom - bounds.top + 150;

    // Determine position on requested monitor
    MONITORINFO info;
    info.cbSize = sizeof( info );
    GetMonitorInfo( monitor, &info );
    int x = info.rcWork.left + max( 0, ( ( info.rcWork.right - info.rcWork.left ) - width ) / 2 );
    int y = info.rcWork.top + max( 0, ( ( info.rcWork.bottom - info.rcWork.top ) - height ) / 2 );
    
    // Create window
    HWND hwnd = CreateWindowExW( WS_EX_TOPMOST, wc.lpszClassName, 
        localization[ lang ].title, WS_OVERLAPPEDWINDOW, x, y, width, height,
        NULL, NULL, GetModuleHandleW( NULL ), 0 );

    // GDI+ instances of all available pens
    int const penSize = 5;
    Gdiplus::Pen* pens[] = {
        new Gdiplus::Pen( Gdiplus::Color( 255, 0, 0, 0 ), penSize ), // Black pen
        new Gdiplus::Pen( Gdiplus::Color( 255, 0, 0, 255 ), penSize ), // Blue pen
        new Gdiplus::Pen( Gdiplus::Color( 255, 255, 0, 0 ), penSize ), // Red pen
        new Gdiplus::Pen( Gdiplus::Color( 255, 0, 128, 0 ), penSize ), // Green pen
    };

    // GDI+ instances of all available highlighters
    int const highlighterSize = 28;
    Gdiplus::Pen* highlights[] = {
        new Gdiplus::Pen( Gdiplus::Color( 128, 255, 255, 64 ), highlighterSize ), // Yellow highlighter
        new Gdiplus::Pen( Gdiplus::Color( 128, 150, 100, 150), highlighterSize ), // Purple highlighter
    };

    // GDI+ instances of erasers for pens/ highlighters
    Gdiplus::Pen* penEraser = new Gdiplus::Pen( Gdiplus::Color( 0, 0, 0, 0), 25 );
    Gdiplus::Pen* highlightEraser = new Gdiplus::Pen( Gdiplus::Color( 0, 0, 0, 0), 50 );

    // Runtime state for annotations window
    struct MakeAnnotationsData makeAnnotationsData = {      
        snippetScale,
        1.0f,
        bounds,
        pens,
        highlights,
        penEraser,
        highlightEraser,
    };

    makeAnnotationsData.arrowCursor = LoadCursor( NULL, IDC_ARROW );
    makeAnnotationsData.penCursor = (HCURSOR) LoadCursorA( GetModuleHandleA( NULL ), MAKEINTRESOURCEA( IDR_PEN ) );
    makeAnnotationsData.eraserCursor = (HCURSOR) LoadCursorA( GetModuleHandleA( NULL ), MAKEINTRESOURCEA( IDR_ERASER ) );

    // Create the `pen` menu and add all items to it
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

    // Create the `highlight` menu and add all items to it
    HMENU highlightMenu = CreatePopupMenu();
    MENUITEMINFOA highlightItems[] =  { 
        { sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 1, 0, 0, 0, 0, 0, 0, penIcon( highlights[ 0 ] ) },
        { sizeof( *penItems ), MIIM_ID | MIIM_BITMAP | MIIM_DATA, 0, 0, 2, 0, 0, 0, 0, 0, 0, penIcon( highlights[ 1 ] ) },
    };
    for( int i = 0; i < sizeof( highlightItems ) / sizeof( *highlightItems ); ++i ) {
        InsertMenuItemA( highlightMenu, i, TRUE, &highlightItems[ i ] );
    }
    makeAnnotationsData.highlightMenu = highlightMenu;

    // Create buttons
    makeAnnotationsData.penButton = CreateWindowW( L"BUTTON", localization[ lang ].pen,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        5, 7, 75, 20, hwnd, NULL, GetModuleHandleW( NULL ), NULL );
    
    makeAnnotationsData.highlightButton = CreateWindowW( L"BUTTON", localization[ lang ].highlight,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        5 + 80 * 1, 7, 75, 20, hwnd, NULL, GetModuleHandleW( NULL ), NULL );
    
    makeAnnotationsData.eraseButton = CreateWindowW( L"BUTTON", localization[ lang ].erase,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        5 + 80 * 2, 7, 75, 20, hwnd, NULL, GetModuleHandleW( NULL ), NULL );
    
    makeAnnotationsData.doneButton = CreateWindowW( L"BUTTON", localization[ lang ].done,
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        5 + 10 + 80 * 3, 7, 75, 20, hwnd, NULL, GetModuleHandleW( NULL ), NULL );

    float scale = getDisplayScaling( hwnd );
    if( scale == 0.0f ) {
        scale = 1.0f;
    }
    resizeButton( makeAnnotationsData.penButton, 1.0f, scale );
    resizeButton( makeAnnotationsData.highlightButton, 1.0f, scale );
    resizeButton( makeAnnotationsData.eraseButton, 1.0f, scale );
    makeAnnotationsData.buttonHeight = resizeButton( makeAnnotationsData.doneButton, 1.0f, scale );

    // Attach state data to window instance
    SetWindowLongPtrA( hwnd, GWLP_USERDATA, (LONG_PTR)&makeAnnotationsData );
    ShowWindow( hwnd, SW_SHOW );
    UpdateWindow( hwnd );

    // Create off-screen drawing surface for window
    HDC dc = GetDC( hwnd );
    HBITMAP backbuffer = CreateCompatibleBitmap( dc, bounds.right - bounds.left, bounds.bottom - bounds.top );
    makeAnnotationsData.backbuffer = CreateCompatibleDC( dc );
    SelectObject( makeAnnotationsData.backbuffer, backbuffer );

    // Create device context for screen snippet
    makeAnnotationsData.snippet = CreateCompatibleDC( dc );
    SelectObject( makeAnnotationsData.snippet, snippet );
    ReleaseDC( hwnd, dc );
    InvalidateRect( hwnd, NULL, TRUE );
    
    // Standard windows message pump
    MSG msg = { NULL };
    while( GetMessage( &msg, NULL, 0, 0 ) ) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // CLeanup
    delete penEraser;
    delete highlightEraser;
    for( int i = 0; i < sizeof( pens ) / sizeof( *pens ); ++i ) {
        delete pens[ i ];
    }

    for( int i = 0; i < sizeof( highlights ) / sizeof( *highlights ); ++i ) {
        delete highlights[ i ];
    }

    DestroyWindow( hwnd );
    DeleteObject( makeAnnotationsData.snippet );
    DeleteObject( makeAnnotationsData.backbuffer );
    DeleteObject( backbuffer );

    UnregisterClassW( wc.lpszClassName, GetModuleHandleW( NULL ) );

    // If annotation was aborted, we report back failure
    if( !makeAnnotationsData.completed ) {
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

