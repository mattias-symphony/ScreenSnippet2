#ifndef UNICODE
	#define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "gdiplus.lib" )

#define WINDOW_CLASS_NAME L"SymphonyScreenSnippetTool"

#include "SelectRegion.h"
#include "Localization.h"
#include "MakeAnnotations.h"

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


int main( int argc, char* argv[] ) {
	FreeConsole();
	SetProcessDPIAware(); // Avoid DPI scaling affecting the resolution of the grabbed snippet

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

		// Let the user annotate the screen snippet with drawings
		RECT bounds = { 0, 0, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y };
		int result = makeAnnotations( snippet, bounds, lang );
		if( result == EXIT_SUCCESS ) {
			// Save annotated bitmap
			Gdiplus::Bitmap bmp( snippet, (HPALETTE)0 );
			CLSID pngClsid;
			if( GetEncoderClsid( L"image/png", &pngClsid ) >= 0 ) {
				wchar_t* filename = 0;
				if( argc > 1 ) {
					size_t len = strlen( argv[ 1 ] );
					filename = new wchar_t[ len + 1 ];
					mbstowcs_s( 0, filename, len + 1, argv[ 1 ], len );
				}
				bmp.Save( filename ? filename : L"test_image.png", &pngClsid, NULL );
				if( filename ) {
					delete[] filename;
				}
			}
        }

		DeleteObject( snippet );
	}

	Gdiplus::GdiplusShutdown( gdiplusToken );
	return EXIT_SUCCESS;
}


// pass-through so the program will build with either /SUBSYSTEM:WINDOWS or /SUBSYSTEM:CONSOLE
extern "C" int __stdcall WinMain( struct HINSTANCE__*, struct HINSTANCE__*, char*, int ) { 
	return main( __argc, __argv ); 
}

