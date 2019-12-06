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

void swap( LONG* a, LONG* b );

#include "SelectRegion.h"
#include "Localization.h"
#include "MakeAnnotations.h"


void swap( LONG* a, LONG* b ) {
	LONG t = *a;
	*a = *b;
	*b = t;
}


HBITMAP grabSnippet( POINT a, POINT b ) {
    HDC hScreen = GetDC( NULL );
    HDC hDC = CreateCompatibleDC( hScreen );
    HBITMAP hBitmap = CreateCompatibleBitmap( hScreen, abs( b.x - a.x ), abs( b.y - a.y ) );
    HGDIOBJ old_obj = SelectObject( hDC, hBitmap );
    BOOL bRet = BitBlt( hDC, 0, 0, abs( b.x - a.x ), abs( b.y - a.y ), hScreen, a.x, a.y, SRCCOPY );

    SelectObject( hDC, old_obj );
    DeleteDC( hDC );
    ReleaseDC( NULL, hScreen );

	return hBitmap;
}


namespace Gdiplus {

	int GetEncoderClsid( const WCHAR* format, CLSID* pClsid ) {
		UINT num = 0; // number of image encoders
		UINT size = 0; // size of the image encoder array in bytes

		ImageCodecInfo* pImageCodecInfo = NULL;

		GetImageEncodersSize( &num, &size );
		if( size == 0 ) {
			return -1;
		}

		pImageCodecInfo = (ImageCodecInfo*) malloc( size );
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

} /* namespace Gdiplus */


int main( int argc, char* argv[] ) {
	FreeConsole();
	SetProcessDPIAware();

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

	RECT region;
	if( selectRegion( &region ) == EXIT_SUCCESS ) {
		// Swap top/bottom and left/right as necessary		
		POINT topLeft = { region.left, region.top };
		POINT bottomRight = { region.right, region.bottom };
		if( topLeft.x > bottomRight.x ) {
			swap( &topLeft.x, &bottomRight.x );
		}
		if( topLeft.y > bottomRight.y ) {
			swap( &topLeft.y, &bottomRight.y );
		}

		// Enforce a minumum size (to avoid ending up with an image you can't even see in the annotation window)
		if( bottomRight.x - topLeft.x < 32 ) {
			bottomRight.x = topLeft.x + 32;
		}
		if( bottomRight.y - topLeft.y < 32 ) {
			bottomRight.y = topLeft.y + 32;
		}
		
		HBITMAP snippet = grabSnippet( topLeft, bottomRight );
		RECT bounds = { 0, 0, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y };
		int result = makeAnnotations( snippet, bounds, lang );
		if( result == EXIT_SUCCESS ) {
			// Save annotated bitmap
			Gdiplus::Bitmap bmp( snippet, (HPALETTE)0 );
			CLSID pngClsid;
			Gdiplus::GetEncoderClsid( L"image/png", &pngClsid );
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

		DeleteObject( snippet );
	}

	Gdiplus::GdiplusShutdown( gdiplusToken );
	return EXIT_SUCCESS;
}

// pass-through so the program will build with either /SUBSYSTEM:WINDOWS or /SUBSYSTEM:CONSOLE
extern "C" int __stdcall WinMain( struct HINSTANCE__*, struct HINSTANCE__*, char*, int ) { 
	return main( __argc, __argv ); 
}

