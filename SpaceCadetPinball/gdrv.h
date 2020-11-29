#pragma once

enum class BitmapType : char
{
	None = 0,
	RawBitmap = 1,
	DibBitmap=2,
};


#pragma pack(push, 1)
struct __declspec(align(1)) gdrv_bitmap8
{
	BITMAPINFO* Dib;
	char* BmpBufPtr2;
	char* BmpBufPtr1;
	int Width;
	int Height;
	int Stride;
	BitmapType BitmapType;
	int Color6;
	int XPosition;
	int YPosition;
};
#pragma pack(pop)


struct LOGPALETTEx256
{
	WORD palVersion;
	WORD palNumEntries;
	PALETTEENTRY palPalEntry[256];

	LOGPALETTEx256() : palVersion(0x300), palNumEntries(256), palPalEntry{}
	{
	}
};

static_assert(sizeof(gdrv_bitmap8) == 37, "Wrong size of gdrv_bitmap8");

class gdrv
{
public:
	static HPALETTE palette_handle;
	static HINSTANCE hinst;
	static HWND hwnd;
	static LOGPALETTEx256 current_palette;
	static int sequence_handle;
	static HDC sequence_hdc;
	static int use_wing;

	static int init(HINSTANCE hInst, HWND hWnd);
	static int uninit();
	static void get_focus();
	static BITMAPINFO* DibCreate(__int16 bpp, int width, int height);
	static void DibSetUsage(BITMAPINFO* dib, HPALETTE hpal, int someFlag);
	static int create_bitmap_dib(gdrv_bitmap8* bmp, int width, int height);
	static int create_bitmap(gdrv_bitmap8* bmp, int width, int height);
	static int create_raw_bitmap(gdrv_bitmap8* bmp, int width, int height, int flag);
	static int destroy_bitmap(gdrv_bitmap8* bmp);
	static int display_palette(PALETTEENTRY* plt);
	static UINT start_blit_sequence();
	static void blit_sequence(gdrv_bitmap8* bmp, int xSrc, int ySrcOff, int xDest, int yDest, int DestWidth,
	                          int DestHeight);
	static void end_blit_sequence();
	static void blit(gdrv_bitmap8* bmp, int xSrc, int ySrcOff, int xDest, int yDest, int DestWidth, int DestHeight);
	static void blat(gdrv_bitmap8* bmp, int xDest, int yDest);
	static void fill_bitmap(gdrv_bitmap8* bmp, int width, int height, int xOff, int yOff, char fillChar);
	static void copy_bitmap(gdrv_bitmap8* dstBmp, int width, int height, int xOff, int yOff, gdrv_bitmap8* srcBmp,
	                        int srcXOff, int srcYOff);
	static void copy_bitmap_w_transparency(gdrv_bitmap8* dstBmp, int width, int height, int xOff, int yOff,
	                                       gdrv_bitmap8* srcBmp, int srcXOff, int srcYOff);
private:
};