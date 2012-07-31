#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <QuickLook/QuickLook.h>
#include <zlib.h>

OSStatus GenerateThumbnailForURL(void *thisInterface, QLThumbnailRequestRef thumbnail, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options, CGSize maxSize);
void CancelThumbnailGeneration(void *thisInterface, QLThumbnailRequestRef thumbnail);

#define L_ENDIAN    1
#define B_ENDIAN    0

#ifdef __BIG_ENDIAN__
#  define ENDIAN_ORDER B_ENDIAN
#else
#  define ENDIAN_ORDER L_ENDIAN
#endif

#define SWITCH_INT(a) {                                                       \
		char s_i, *p_i;                                                       \
		p_i = (char *)&(a);                                                   \
		s_i = p_i[0]; p_i[0] = p_i[3]; p_i[3] = s_i;                          \
		s_i = p_i[1]; p_i[1] = p_i[2]; p_i[2] = s_i;                          \
	} (void)0

/* INTEGER CODES */
#ifdef __BIG_ENDIAN__
/* Big Endian */
#  define MAKE_ID(a, b, c, d) ( (int)(a) << 24 | (int)(b) << 16 | (c) << 8 | (d) )
#else
/* Little Endian */
#  define MAKE_ID(a, b, c, d) ( (int)(d) << 24 | (int)(c) << 16 | (b) << 8 | (a) )
#endif

#define TEST MAKE_ID('T', 'E', 'S', 'T') /* used as preview between 'REND' and 'GLOB' */
#define REND MAKE_ID('R', 'E', 'N', 'D')

/* -----------------------------------------------------------------------------
    Generate a thumbnail for file

   This function's job is to create thumbnail for designated file as fast as possible
   ----------------------------------------------------------------------------- */

OSStatus GenerateThumbnailForURL(void *thisInterface, QLThumbnailRequestRef thumbnail, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options, CGSize maxSize)
{
    CFStringRef sfileloc = CFURLGetString(url);
    CFIndex maxStringSize = CFStringGetMaximumSizeForEncoding(CFStringGetLength(sfileloc), kCFStringEncodingUTF8);
    char *cfileloc = malloc(maxStringSize);
    CFURLGetFileSystemRepresentation(url, true, (UInt8 *)cfileloc, maxStringSize);
    gzFile *gzfile = gzopen(cfileloc, "rb");
    free(cfileloc);
    
    char buf[12];
    int bhead[24 / sizeof(int)]; /* max size on 64bit */
    char endian, pointer_size;
    char endian_switch;
    int sizeof_bhead;

    /* read the blend file header */
    if (gzread(gzfile, buf, 12) != 12)
        return noErr;
    if (strncmp(buf, "BLENDER", 7))
        return noErr;

    if (buf[7] == '-')
        pointer_size = 8;
    else if (buf[7] == '_')
        pointer_size = 4;
    else
        return noErr;

    sizeof_bhead = 16 + pointer_size;

    if (buf[8] == 'V')
        endian = B_ENDIAN;  /* big: PPC */
    else if (buf[8] == 'v')
        endian = L_ENDIAN;  /* little: x86 */
    else
        return noErr;

    endian_switch = ((ENDIAN_ORDER != endian)) ? 1 : 0;

    while (gzread(gzfile, bhead, sizeof_bhead) == sizeof_bhead) {
        if (endian_switch)
            SWITCH_INT(bhead[1]);  /* length */

        if (bhead[0] == REND) {
            gzseek(gzfile, bhead[1], SEEK_CUR); /* skip to the next */
        }
        else {
            break;
        }
    }

    /* using 'TEST' since new names segfault when loading in old blenders */
    if (bhead[0] == TEST) {
        int size[2];

        if (gzread(gzfile, size, sizeof(size)) != sizeof(size))
            return noErr;

        if (endian_switch) {
            SWITCH_INT(size[0]);
            SWITCH_INT(size[1]);
        }
        /* length */
        bhead[1] -= sizeof(int) * 2;

        /* inconsistent image size, quit early */
        if (bhead[1] != size[0] * size[1] * sizeof(int))
            return noErr;

        CFMutableDataRef swappedImgData = CFDataCreateMutable(kCFAllocatorDefault, bhead[1]);
        int i;
        size_t bitsPerComponent = 8;
        size_t bitsPerPixel = 32;
        size_t bytesPerRow = (size[0]*bitsPerPixel)/8;
        for (i = 0; i < bhead[1]; i += bytesPerRow) {
            gzread(gzfile, CFDataGetMutableBytePtr(swappedImgData)+(bhead[1]-i-bytesPerRow), (unsigned int)bytesPerRow);
        }
        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
        CGBitmapInfo info = kCGBitmapByteOrder32Big;
        CGFloat *decode = NULL;
        bool shouldInteroplate = 0;
        CGColorRenderingIntent intent = kCGRenderingIntentDefault;
        CGDataProviderRef imgDataProvider = CGDataProviderCreateWithCFData(swappedImgData);
        CFRelease(swappedImgData);
        CGImageRef image = CGImageCreate(size[0], size[1], bitsPerComponent, bitsPerPixel, bytesPerRow, colorSpace, info, imgDataProvider, decode, shouldInteroplate, intent);
        CGColorSpaceRelease(colorSpace);
        CGDataProviderRelease(imgDataProvider);
        QLThumbnailRequestSetImage(thumbnail, image, NULL);
        CGImageRelease(image);
    }
    
    gzclose(gzfile);
    return noErr;
}

void CancelThumbnailGeneration(void *thisInterface, QLThumbnailRequestRef thumbnail)
{
    // Implement only if supported
}
