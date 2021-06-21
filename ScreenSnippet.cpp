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
#include <d3d9.h>
//#include <d3dx9.h>

#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "gdiplus.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "d3d9.lib" )

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


// Grab a section of the screen using Windows GDI
static HBITMAP grabSnippetGDI( POINT topLeft, POINT bottomRight ) {
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


// Grab a section of the screen using DirectX 9
static HBITMAP grabSnippetDX9( POINT topLeft, POINT bottomRight ) {
    IDirect3DDevice9*   device = NULL;
    IDirect3DSurface9*  surface = NULL;

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if( !d3d ) {
        return NULL;
    }

    D3DDISPLAYMODE  ddm;
    if( FAILED( d3d->GetAdapterDisplayMode( D3DADAPTER_DEFAULT, &ddm ) ) ) {
        d3d->Release();
        return NULL;
    }

    D3DPRESENT_PARAMETERS   d3dpp;
    memset( &d3dpp, 0, sizeof( D3DPRESENT_PARAMETERS ) );
    d3dpp.Windowed = TRUE;
    d3dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    d3dpp.BackBufferFormat = ddm.Format;
    d3dpp.BackBufferHeight = ddm.Height;
    d3dpp.BackBufferWidth = ddm.Width;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetDesktopWindow();
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

    if( FAILED( d3d->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &device ) ) ) {
        d3d->Release();
        return NULL;
    }

    if( FAILED( device->CreateOffscreenPlainSurface( ddm.Width, ddm.Height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &surface, NULL ) ) ) {
        device->Release();
        d3d->Release();
        return NULL;
    }
    
    device->GetFrontBufferData( 0, surface );           
    D3DLOCKED_RECT rect;
    RECT region = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };   
    if( FAILED( surface->LockRect( &rect, &region, D3DLOCK_NO_DIRTY_UPDATE | D3DLOCK_NOSYSLOCK | D3DLOCK_READONLY ) ) ) {
        surface->Release();
        device->Release();
        d3d->Release();
        return NULL;
    }
    
    uint32_t* buffer = (uint32_t*) malloc( sizeof( uint32_t ) * ( bottomRight.x - topLeft.x ) * ( bottomRight.y - topLeft.y ) );
    for( int i = 0; i <  bottomRight.y - topLeft.y; ++i ) {
         memcpy( buffer + i * ( bottomRight.x - topLeft.x ), ( (uint8_t*)rect.pBits ) + i * rect.Pitch, sizeof( uint32_t ) * ( bottomRight.x - topLeft.x ) );
    }


    // TEMP - REMOVE 
    for( int i = 0; i <  bottomRight.y - topLeft.y; ++i ) {
         buffer[ i * ( bottomRight.x - topLeft.x ) ] = 0xffff00ff;
         buffer[ ( i + 1 ) * ( bottomRight.x - topLeft.x ) - 1 ] = 0xffff00ff;
    }
    for( int i = 0; i <  bottomRight.x - topLeft.x; ++i ) {
         buffer[ i  ] = 0xffff00ff;
         buffer[ i + ( bottomRight.x - topLeft.x ) * ( bottomRight.y - topLeft.y - 1 ) ] = 0xffff00ff;
    }
    // END TEMP
    

    surface->UnlockRect();
    surface->Release();
    device->Release();
    d3d->Release();

    HDC screen = GetDC( NULL );
    HDC dc = CreateCompatibleDC( screen );
    HBITMAP snippet = CreateCompatibleBitmap( screen, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y );

    BITMAPINFO info;
    info.bmiHeader.biSize          = sizeof(BITMAPINFO) - sizeof(RGBQUAD);
    info.bmiHeader.biWidth         = bottomRight.x - topLeft.x;
    info.bmiHeader.biHeight        = 0 - (int)( bottomRight.y - topLeft.y );
    info.bmiHeader.biPlanes        = 1;
    info.bmiHeader.biBitCount      = 32;
    info.bmiHeader.biCompression   = BI_RGB;
    info.bmiHeader.biSizeImage     = 0;
    info.bmiHeader.biXPelsPerMeter = 0;
    info.bmiHeader.biYPelsPerMeter = 0;
    info.bmiHeader.biClrUsed       = 0;
    info.bmiHeader.biClrImportant  = 0;
    SetDIBits( dc, snippet, 0, bottomRight.y - topLeft.y, buffer, &info, DIB_RGB_COLORS );
    DeleteDC( dc );
    ReleaseDC( NULL, screen );
    free( buffer );

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


int wmain( int argc, wchar_t* argv[] ) {

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
    if( argc > 1 && wcscmp( argv[ 1 ], L"--no-annotate" ) == 0 ) {
        annotate = false;
        // Skip the --no-annotate argument in the remaining code
        argv++; 
        argc--;
    }

    // Find language matching command line arg
    int lang = 0; // default to 'en-US'
    if( argc == 3 ) {
        wchar_t const* lang_str = argv[ 2 ];
        for( int i = 0; i < sizeof( localization ) / sizeof( *localization ); ++i ) {
            if( wcsicmp( localization[ i ].language, lang_str ) == 0 ) {
                lang = i;
                break;
            }
        }
    }

    // Start GDI+ (used for semi-transparent drawing and anti-aliased curve drawing)
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup( &gdiplusToken, &gdiplusStartupInput, NULL );


    HBITMAP snippet = NULL;
    float snippetScale = 1.0f;

    // Try to make use of built-in windows SnippingTool first
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
        snippet = grabSnippetDX9( topLeft, bottomRight );
        if( !snippet ) {
            snippet = grabSnippetGDI( topLeft, bottomRight );
        }
        snippetScale = getSnippetScaling( topLeft, bottomRight );
    }
    
    if( snippet ) {
        // Let the user annotate the screen snippet with drawings
        int result = EXIT_SUCCESS;
        if( annotate ) {
            result = makeAnnotations( monitor, snippet, snippetScale, lang );
        }
        
        if( result == EXIT_SUCCESS ) {
            // Save bitmap
            Gdiplus::Bitmap bmp( snippet, (HPALETTE)0 );
            CLSID pngClsid;
            if( GetEncoderClsid( L"image/png", &pngClsid ) >= 0 ) {
                wchar_t* filename = argv[ 1 ];
                bmp.Save( filename ? filename : L"test_image.png", &pngClsid, NULL );
            }
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
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW( GetCommandLine(), &argc );
    int result = wmain( argc, argv ); 
    LocalFree( argv );
    return result;
}

