#ifndef UNICODE
    #define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shellscalingapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "gdiplus.lib" )

#define WINDOW_CLASS_NAME L"SymphonyScreenSnippetTool"

BOOL (WINAPI *EnableNonClientDpiScalingPtr)( HWND ) = NULL; // Dynamic bound function which does not exist on win7
HRESULT (STDAPICALLTYPE* GetDpiForMonitorPtr)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT* ) = NULL;

#include "resources.h"
#include "SelectRegion.h"
#include "Localization.h"
#include "MakeAnnotations.h"

struct SnippetScalingData {
    POINT topLeft;
    POINT bottomRight;
    HMONITOR monitor;
};


BOOL CALLBACK monproc( HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM lparam ) {
    SnippetScalingData* data = (SnippetScalingData*) lparam;

    if( PtInRect( rect, data->topLeft ) && PtInRect( rect, data->bottomRight ) ) {
        data->monitor = monitor;
        return FALSE;
    }

    return TRUE;
}


float getSnippetScaling( POINT topLeft, POINT bottomRight ) {
    SnippetScalingData data = { topLeft, bottomRight, NULL };

    EnumDisplayMonitors( NULL, NULL, monproc, (LPARAM)&data );
    if( !data.monitor || !GetDpiForMonitorPtr ) {
        return 0.0f;
    }

    UINT dpiX;
    UINT dpiY;
    GetDpiForMonitorPtr( data.monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY );
    if( dpiX != dpiY ) {
        return 0.0f;
    }

    float const windowsUnscaledDpi = 96.0f;
    return dpiX / windowsUnscaledDpi;
}


// Grab a section of the screen
static HBITMAP grabSnippet( POINT topLeft, POINT bottomRight ) {
    HDC screen = GetDC( NULL );
    HDC dc = CreateCompatibleDC( screen );
    HBITMAP snippet = CreateCompatibleBitmap( screen, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y );
    HGDIOBJ oldObject = SelectObject( dc, snippet );
    
    BitBlt( dc, 0, 0, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y, screen, topLeft.x, topLeft.y, SRCCOPY );

    SelectObject( dc, oldObject );
    DeleteDC( dc );
    ReleaseDC( NULL, screen );

    return snippet;
}



// Utility function used to get an encoder for PNG image format
static int GetEncoderClsid( const WCHAR* format, CLSID* pClsid ) {
    UINT num = 0; // number of image encoders
    UINT size = 0; // size of the image encoder array in bytes

    Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

    Gdiplus::GetImageEncodersSize( &num, &size );
    if( size == 0 ) {
        return -1;
    }

    pImageCodecInfo = (Gdiplus::ImageCodecInfo*) malloc( size );
    if( pImageCodecInfo == NULL ) {
        return -1;
    }

    GetImageEncoders( num, size, pImageCodecInfo );

    for( UINT j = 0; j < num; ++j ) {
        if( wcscmp( pImageCodecInfo[ j ].MimeType, format ) == 0 ) {
            *pClsid = pImageCodecInfo[ j ].Clsid;
            free( pImageCodecInfo );
            return j;
        }
    }

    free( pImageCodecInfo );
    return -1;
}


// Callback for closing existing instances of the snippet tool
static BOOL CALLBACK closeExistingInstance( HWND hwnd, LPARAM lparam ) {    
    wchar_t className[ 256 ] = L"";
    GetClassNameW( hwnd, className, sizeof( className ) );

    if( wcscmp( className, WINDOW_CLASS_NAME ) == 0 ) {
        PostMessageA( hwnd, WM_CLOSE, 0, 0 );
    }

    return TRUE;
}

int main( int argc, char* argv[] ) {

    // Dynamic binding of functions not available on win 7
    HMODULE user32lib = LoadLibraryA( "user32.dll" );
    if( user32lib ) {
        EnableNonClientDpiScalingPtr = (BOOL (WINAPI*)(HWND)) GetProcAddress( user32lib, "EnableNonClientDpiScaling" );

        DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiAwarenessContextPtr)( DPI_AWARENESS_CONTEXT ) = 
            (DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT)) 
                GetProcAddress( user32lib, "SetThreadDpiAwarenessContext" );
        
        BOOL (WINAPI *SetProcessDPIAwarePtr)(VOID) = (BOOL (WINAPI*)(VOID))GetProcAddress( user32lib, "SetProcessDPIAware" );

        // Avoid DPI scaling affecting the resolution of the grabbed snippet
        if( !SetThreadDpiAwarenessContextPtr || !SetThreadDpiAwarenessContextPtr( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ) ) {
            if( SetProcessDPIAwarePtr ) {
                SetProcessDPIAwarePtr();
            }
        }
    }

    HMODULE shcorelib = LoadLibraryA( "Shcore.dll" );
    if( shcorelib ) {
        GetDpiForMonitorPtr = 
            (HRESULT (STDAPICALLTYPE*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT* )) 
                GetProcAddress( shcorelib, "GetDpiForMonitor" );
    }

    
    HWND foregroundWindow = GetForegroundWindow();

    HMONITOR monitor = MonitorFromWindow( foregroundWindow, MONITOR_DEFAULTTOPRIMARY );

    // Cancel screen snippet in progress
    EnumWindows( closeExistingInstance, 0 );

    // If no command line parameters, this was a request to cancel in-progress snippet tool
    if( argc < 2 ) {
        if( foregroundWindow ) {
            SetForegroundWindow( foregroundWindow );
        }
        return EXIT_SUCCESS;
    }

    bool annotate = true;
    // Check for --no-annotate switch
    if( argc > 1 && strcmp( argv[ 1 ], "--no-annotate" ) == 0 ) {
        annotate = false;
        // Skip the --no-annotate argument in the remaining code
        argv++; 
        argc--;
    }

    // Find language matching command line arg
    int lang = 0; // default to 'en-US'
    if( argc == 3 ) {
        char const* lang_str = argv[ 2 ];
        for( int i = 0; i < sizeof( localization ) / sizeof( *localization ); ++i ) {
            if( _stricmp( localization[ i ].language, lang_str ) == 0 ) {
                lang = i;
                break;
            }
        }
    }

    // Start GDI+ (used for semi-transparent drawing and anti-aliased curve drawing)
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup( &gdiplusToken, &gdiplusStartupInput, NULL );

    // Let the user select a region on the screen
    RECT region;
    if( selectRegion( &region ) == EXIT_SUCCESS ) { 
        POINT topLeft = { region.left, region.top };
        POINT bottomRight = { region.right, region.bottom };

        // Enforce a minumum size (to avoid ending up with an image you can't even see in the annotation window)
        if( bottomRight.x - topLeft.x < 32 ) {
            bottomRight.x = topLeft.x + 32;
        }
        if( bottomRight.y - topLeft.y < 32 ) {
            bottomRight.y = topLeft.y + 32;
        }
        
        // Grab a bitmap of the selected region
        HBITMAP snippet = grabSnippet( topLeft, bottomRight );
        float snippetScale = getSnippetScaling( topLeft, bottomRight );

        // Let the user annotate the screen snippet with drawings
        int result = EXIT_SUCCESS;
        if( annotate ) {
            RECT bounds = { 0, 0, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y };
            result = makeAnnotations( monitor, snippet, bounds, snippetScale, lang );
        }
        // Save bitmap
        Gdiplus::Bitmap bmp( snippet, (HPALETTE)0 );
        CLSID pngClsid;
        if( GetEncoderClsid( L"image/png", &pngClsid ) >= 0 ) {
            size_t len = strlen( argv[ 1 ] );
            wchar_t* filename = filename = new wchar_t[ len + 1 ];
            mbstowcs_s( 0, filename, len + 1, argv[ 1 ], len );
            bmp.Save( filename ? filename : L"test_image.png", &pngClsid, NULL );
            delete[] filename;
        }

        DeleteObject( snippet );
    }

    Gdiplus::GdiplusShutdown( gdiplusToken );
    if( foregroundWindow ) {
        SetForegroundWindow( foregroundWindow );
    }

    if( user32lib ) {
        FreeLibrary( user32lib );
    }

    if( shcorelib ) {
        FreeLibrary( shcorelib );
    }

    return EXIT_SUCCESS;
}


// pass-through so the program will build with either /SUBSYSTEM:WINDOWS or /SUBSYSTEM:CONSOLE
extern "C" int __stdcall WinMain( struct HINSTANCE__*, struct HINSTANCE__*, char*, int ) { 
    return main( __argc, __argv ); 
}

