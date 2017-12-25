//  draws bitmap fonts

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "web_console.h"
#include "esp_heap_caps.h"
#include "i2s_parallel.h"
#include "rgb_led_panel.h"
#include "frame_buffer.h"
#include "bmp.h"

static const char *T = "BMP";

FILE *g_bmpFile = NULL;
bitmapFileHeader_t g_bmpFileHeader;
bitmapInfoHeader_t g_bmpInfoHeader;
fontInfo_t         g_fontInfo;

void loadFntFile( char *fileName, font_t *fDat ){
    
}

// Loads a <prefix>.bmp and <prefix>.fnt file
void initFont( char *filePrefix ){
    char tempFileName[32];
    if ( g_bmpFile != NULL ){
        fclose( g_bmpFile );
        g_bmpFile = NULL;
    }
    sprintf( tempFileName, "/SD/%s.fnt", filePrefix );
    loadFntFile( tempFileName, &g_fontInfo );
    sprintf( tempFileName, "/SD/%s.bmp", filePrefix );
    g_bmpFile = loadBitmapFile( tempFileName, &g_bmpFileHeader, &g_bmpInfoHeader );
    if( g_bmpFile == NULL ){
        ESP_LOGE(T, "Could not open %s", tempFileName);
        return;
    }
}

// Returns a filepointer seeked to the beginngin of the bitmap data
FILE *loadBitmapFile( char *filename, bitmapFileHeader_t *bitmapFileHeader, bitmapInfoHeader_t *bitmapInfoHeader ) {
    FILE *filePtr; //our file pointer

    //open filename in read binary mode
    filePtr = fopen(filename,"rb");
    if (filePtr == NULL)
        return NULL;

    //read the bitmap file header
    fread( bitmapFileHeader, sizeof(bitmapFileHeader_t),1,filePtr);

    //verify that this is a bmp file by check bitmap id
    if (bitmapFileHeader->bfType !=0x4D42) {
        fclose(filePtr);
        return NULL;
    }

    //read the bitmap info header
    fread(bitmapInfoHeader, sizeof(bitmapInfoHeader_t),1,filePtr);

    //move file point to the beggining of bitmap data
    fseek(filePtr, bitmapFileHeader->bfOffBits, SEEK_SET);
    return filePtr;
}


#define IMAGE_WIDTH  256    //[pixels]
#define IMAGE_HEIGHT 256    //[pixels]
#define BITS_PIXEL    24    //[bits]
#define ROW_SIZE    ( (BITS_PIXEL * IMAGE_WIDTH + 31)/32 * 4 )  //[bytes]


void copyBmpToFbRect( FILE *bmpF, int xBmp, int yBmp, int wBmp, int hBmp, int xFb, int yFb, uint8_t layerFb, uint8_t rFb, uint8_t gFb, uint8_t bFb ){
	uint8_t rowBuffer[ROW_SIZE];
    if ( bmpF == NULL )
        return;
	int startPos = ftell( bmpF );
	//move vertically to the right row
    fseek( bmpF, ROW_SIZE*(IMAGE_HEIGHT-yBmp-hBmp), SEEK_CUR );
    for ( int rowId=0; rowId<hBmp; rowId++ ){
    	//read the whole row
    	fread( rowBuffer, ROW_SIZE, 1, bmpF );
    	//copy the relevant pixels
        uint8_t *p = &rowBuffer[ xBmp*(BITS_PIXEL/8) ];
		for ( int colId=0; colId<wBmp; colId++ ){
			setPixel( layerFb, colId+xFb, hBmp-rowId-1+yFb, rFb, gFb, bFb, 255-(*p) );
            p += BITS_PIXEL/8;
		}
    }
    fseek( bmpF, startPos, SEEK_SET );
}