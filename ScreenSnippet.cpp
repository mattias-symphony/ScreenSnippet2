#ifndef UNICODE
    #define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "gdiplus.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "ole32.lib" )

#define WINDOW_CLASS_NAME L"SymphonyScreenSnippetTool"

FILE* logFile = NULL;

int time_offset() {
    time_t gmt, rawtime = time(NULL);
    struct tm *ptm;
    ptm = gmtime(&rawtime);
    gmt = mktime(ptm);
    return (int)difftime(rawtime, gmt);
}


void internalLog( char const* function, char const* level, char const* format, ... ) {
	if( !logFile ) {
		return;
	}
	time_t rawtime;
	struct tm *info;
	time( &rawtime );
	info = localtime( &rawtime );
	int offset = time_offset();
	int offs_s = offset % 60;
	offset -= offs_s;
	int offs_m = ( offset % (60 * 60) ) / 60;
	offset -= offs_m * 60;
	int offs_h = offset / ( 60 * 60 );
	fprintf( logFile, "%d-%02d-%02d %02d:%02d:%02d:025 %+02d:%02d | %4X | %s | %s: ", info->tm_year + 1900, info->tm_mon + 1, 
		info->tm_mday, info->tm_hour, info->tm_min, info->tm_sec, offs_h, offs_m, GetCurrentProcessId(), level, function );
	va_list args;
	va_start( args, format );
	vfprintf( logFile, format, args );
	va_end( args );
	fprintf( logFile, "\n" );
	fflush( logFile );
}


#define LOG_INFO( format, ... ) internalLog( __func__, "info", format, __VA_ARGS__ )
#define LOG_ERROR( format, ... ) internalLog( __func__, "error", format, __VA_ARGS__ )


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
		LOG_ERROR( "Could not find monitor or GetDpiForMonitorPtr function" );
        return 0.0f;
    }

    UINT dpiX;
    UINT dpiY;
    GetDpiForMonitorPtr( data.monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY );
	LOG_INFO( "DPI=%d,%d", dpiX, dpiY );
    if( dpiX != dpiY ) {
		LOG_ERROR( "DPI differs on horizontal/vertical axis" );
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
		LOG_INFO( "Found existing instance to close. hwnd=%" PRIX64, (uint64_t)(uintptr_t) hwnd );
        PostMessageA( hwnd, WM_CLOSE, 0, 0 );
    }

    return TRUE;
}

int main( int argc, char* argv[] ) {
	PWSTR logPathW = NULL;
	HRESULT res = SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, NULL, &logPathW ); 
	if( SUCCEEDED( res ) ) {
		int count = WideCharToMultiByte( CP_ACP, 0, logPathW, -1, 0, 0, NULL, NULL );
		if( count > 0 ) {
			char* logPathA = new char[ count + strlen( "\\Symphony\\ScreenSnippet.log" ) ];
			WideCharToMultiByte( CP_ACP, 0, logPathW, count, logPathA, count, NULL, NULL );
			strcat( logPathA, "\\Symphony" );
			CreateDirectoryA( logPathA, NULL );
			strcat( logPathA, "\\ScreenSnippet.log" );
			logFile = fopen( logPathA, "a" );
			delete[] logPathA;
		}		
	}
	CoTaskMemFree( logPathW );

	LOG_INFO( "----------------- NEW INSTANCE -----------------" );
	LOG_INFO( "argc=%d", argc );
	for( int i = 0; i < argc; ++i ) {
		LOG_INFO( "argv[%d]=%s", i, argv[ i ] );
	}
	
    // Dynamic binding of functions not available on win 7
    HMODULE user32lib = LoadLibraryA( "user32.dll" );
    if( user32lib ) {
		LOG_INFO( "user32.dll loaded" );
        EnableNonClientDpiScalingPtr = (BOOL (WINAPI*)(HWND)) GetProcAddress( user32lib, "EnableNonClientDpiScaling" );

        DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiAwarenessContextPtr)( DPI_AWARENESS_CONTEXT ) = 
            (DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT)) 
                GetProcAddress( user32lib, "SetThreadDpiAwarenessContext" );
        
        BOOL (WINAPI *SetProcessDPIAwarePtr)(VOID) = (BOOL (WINAPI*)(VOID))GetProcAddress( user32lib, "SetProcessDPIAware" );

        // Avoid DPI scaling affecting the resolution of the grabbed snippet
        if( !SetThreadDpiAwarenessContextPtr || !SetThreadDpiAwarenessContextPtr( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ) ) {
			LOG_INFO( "Thread DPI awareness not available" );
            if( SetProcessDPIAwarePtr ) {
                SetProcessDPIAwarePtr();
				LOG_INFO( "SetProcessDPIAware called" );
            }
        }
    }

    HMODULE shcorelib = LoadLibraryA( "Shcore.dll" );
    if( shcorelib ) {
		LOG_INFO( "shcore.dll loaded" );
        GetDpiForMonitorPtr = 
            (HRESULT (STDAPICALLTYPE*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT* )) 
                GetProcAddress( shcorelib, "GetDpiForMonitor" );
    }

    
    HWND foregroundWindow = GetForegroundWindow();
	LOG_INFO( "foregroundWindow=%" PRIX64, (uint64_t)(uintptr_t) foregroundWindow );

    HMONITOR monitor = MonitorFromWindow( foregroundWindow, MONITOR_DEFAULTTOPRIMARY );
	LOG_INFO( "monitor=%" PRIX64, (uint64_t)(uintptr_t) monitor );

    // Cancel screen snippet in progress
	LOG_INFO( "Checking for existing instances to close" );
    EnumWindows( closeExistingInstance, 0 );

    // If no command line parameters, this was a request to cancel in-progress snippet tool
    if( argc < 2 ) {
        if( foregroundWindow ) {
			LOG_INFO( "Reactivating foregroundWindow=%" PRIX64, (uint64_t)(uintptr_t) foregroundWindow );
            SetForegroundWindow( foregroundWindow );
        }

		LOG_INFO( "EXIT after request to cancel in-progress snippet tools" );

		if( logFile ) {
			fclose( logFile );
		}

        return EXIT_SUCCESS;
    }

    bool annotate = true;
    // Check for --no-annotate switch
    if( argc > 1 && strcmp( argv[ 1 ], "--no-annotate" ) == 0 ) {
		LOG_INFO( "--no-annotate specified, turning off annotations" );
        annotate = false;
        // Skip the --no-annotate argument in the remaining code
        argv++; 
        argc--;
    }

    // Find language matching command line arg
    int lang = 0; // default to 'en-US'
    if( argc == 3 ) {
        char const* lang_str = argv[ 2 ];
		LOG_INFO( "Finding language matching %s", lang_str );
        for( int i = 0; i < sizeof( localization ) / sizeof( *localization ); ++i ) {
            if( _stricmp( localization[ i ].language, lang_str ) == 0 ) {
				LOG_INFO( "Language found(%d): %s", i, localization[ i ].language );
                lang = i;
                break;
            }
        }
    }
	LOG_INFO( "Language used(%d): %s", lang, localization[ lang ].language );

    // Start GDI+ (used for semi-transparent drawing and anti-aliased curve drawing)
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup( &gdiplusToken, &gdiplusStartupInput, NULL );

    // Let the user select a region on the screen
	LOG_INFO( "Selecting region" );
    RECT region;
    if( selectRegion( &region ) == EXIT_SUCCESS ) { 
		LOG_INFO( "Region selected: %d, %d, %d, %d", region.left, region.top, region.right, region.bottom );
        POINT topLeft = { region.left, region.top };
        POINT bottomRight = { region.right, region.bottom };

        // Enforce a minumum size (to avoid ending up with an image you can't even see in the annotation window)
        if( bottomRight.x - topLeft.x < 32 ) {
            bottomRight.x = topLeft.x + 32;
			LOG_INFO( "Expanding to enforce minimum width" );
        }
        if( bottomRight.y - topLeft.y < 32 ) {
            bottomRight.y = topLeft.y + 32;
			LOG_INFO( "Expanding to enforce minimum height" );
        }
		LOG_INFO( "Final region: %d, %d, %d, %d", topLeft.x, topLeft.y, bottomRight.x, bottomRight.y );
        
        // Grab a bitmap of the selected region
		LOG_INFO( "Grabbing snippet" );
        HBITMAP snippet = grabSnippet( topLeft, bottomRight );
		LOG_INFO( "Snippet grabbed=%" PRIX64, (uint64_t)(uintptr_t) snippet );
        float snippetScale = getSnippetScaling( topLeft, bottomRight );
		LOG_INFO( "Finding snippet scale: %f", snippetScale );

        // Let the user annotate the screen snippet with drawings
        int result = EXIT_SUCCESS;
        if( annotate ) {
			LOG_INFO( "Annotating snippet" );
            RECT bounds = { 0, 0, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y };
            result = makeAnnotations( monitor, snippet, bounds, snippetScale, lang );
			LOG_INFO( "Annotation done" );
        } else {
			LOG_INFO( "Skip annotation" );
		}
		
		if( result == EXIT_SUCCESS ) {
			LOG_INFO( "Saving bitmap" );
			// Save bitmap
			Gdiplus::Bitmap bmp( snippet, (HPALETTE)0 );
			CLSID pngClsid;
			if( GetEncoderClsid( L"image/png", &pngClsid ) >= 0 ) {
				size_t len = strlen( argv[ 1 ] );
				wchar_t* filename = filename = new wchar_t[ len + 1 ];
				mbstowcs_s( 0, filename, len + 1, argv[ 1 ], len );
				bmp.Save( filename ? filename : L"test_image.png", &pngClsid, NULL );
				LOG_INFO( "Bitmap saved: %s", argv[ 1 ] );
				delete[] filename;
			}
		} else {
			LOG_INFO( "Not saving bitmap because annotation step aborted" );
		}

        DeleteObject( snippet );
	} else {
		LOG_INFO( "Region selection aborted" );
	}

    Gdiplus::GdiplusShutdown( gdiplusToken );
    if( foregroundWindow ) {
		LOG_INFO( "Reactivating foregroundWindow=%" PRIX64, (uint64_t)(uintptr_t) foregroundWindow );
        SetForegroundWindow( foregroundWindow );
    }

    if( user32lib ) {
        FreeLibrary( user32lib );
    }

    if( shcorelib ) {
        FreeLibrary( shcorelib );
    }

	LOG_INFO( "EXIT_SUCCESS" );

	if( logFile ) {
		fclose( logFile );
	}

    return EXIT_SUCCESS;
}


// pass-through so the program will build with either /SUBSYSTEM:WINDOWS or /SUBSYSTEM:CONSOLE
extern "C" int __stdcall WinMain( struct HINSTANCE__*, struct HINSTANCE__*, char*, int ) { 
    return main( __argc, __argv ); 
}

