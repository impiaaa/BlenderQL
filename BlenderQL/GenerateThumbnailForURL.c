#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <QuickLook/QuickLook.h>
#include <zlib.h>

OSStatus GenerateThumbnailForURL(void *thisInterface, QLThumbnailRequestRef thumbnail, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options, CGSize maxSize);
void CancelThumbnailGeneration(void *thisInterface, QLThumbnailRequestRef thumbnail);

#ifdef __LITTLE_ENDIAN__
#define big16ToNative(x) (((x & 0x00FF) << 8) | ((x & 0xFF00) >> 8))
#define little16ToNative(x) x
#define big32ToNative(x) __builtin_bswap32(x)
#define little32ToNative(x) x
#else
#define big16ToNative(x) x
#define little16ToNative(x) (((x & 0x00FF) << 8) | ((x & 0xFF00) >> 8))
#define big32ToNative(x) x
#define little32ToNative(x) __builtin_bswap32(x)
#endif

#define CHUNK 16384

typedef struct _Bheader {
    char pointersize;
    char endianness;
} Bheader;

typedef struct _Cheader {
UInt32 code;
UInt32 length;
} Cheader;

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
    gzFile *file = gzopen(cfileloc, "rb");
    
    char identifier[8];
    gzread(file, identifier, 7);
    identifier[7] = 0;
    if (strcmp(identifier, "BLENDER") != 0) {
        // Not a Blender file
        gzclose(file);
        return noErr;
    }
    
    Bheader head;
    gzread(file, &head, sizeof(Bheader));
    
    char versionnumber[3];
    gzread(file, versionnumber, 3);
    if (atoi(versionnumber) < 250) {
        // Old Blender files have no thumbnails
        gzclose(file);
        return noErr;
    }
    
    Cheader bhead;
    memset(&bhead, 0, sizeof(Cheader));
    while (gzread(file, &bhead, sizeof(Cheader)) == sizeof(Cheader)) {
        if (head.endianness == 'V') {
            bhead.length = big32ToNative(bhead.length);
        }
        else if (head.endianness == 'v') {
            bhead.length = little32ToNative(bhead.length);
        }
        bhead.code = big32ToNative(bhead.code);
        
        if (bhead.code == 'REND') {
            if (gzdirect(file) == 0) {
                bhead.length += 0x10;
            }
            else {
                bhead.length += 0x0C;
            }
            gzseek(file, bhead.length, SEEK_CUR);
        }
        else {
            break;
        }
    }
    
    if (bhead.code != 'TEST') {
        gzclose(file);
        return noErr;
    }
    
    if (gzdirect(file) == 0) {
        gzseek(file, 0x10, SEEK_CUR);
    }
    else {
        gzseek(file, 0x0C, SEEK_CUR);
    }
    
    UInt32 width;
    UInt32 height;
    gzread(file, &width, sizeof(UInt32));
    gzread(file, &height, sizeof(UInt32));
    
    bhead.length -= 8;
    if (bhead.length != width*height*4) {
        gzclose(file);
        return noErr;
    }
    
    CFMutableDataRef swappedImgData = CFDataCreateMutable(kCFAllocatorDefault, bhead.length);
    int i;
    size_t bitsPerComponent = 8;
    size_t bitsPerPixel = 32;
    size_t bytesPerRow = (width*bitsPerPixel)/8;
    for (i = 0; i < bhead.length; i += bytesPerRow) {
        gzread(file, CFDataGetMutableBytePtr(swappedImgData)+(bhead.length-i-bytesPerRow), bytesPerRow);
    }
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    CGBitmapInfo info = kCGBitmapByteOrder32Big;
    CGFloat *decode = NULL;
    bool shouldInteroplate = 0;
    CGColorRenderingIntent intent = kCGRenderingIntentDefault;
    CGDataProviderRef imgDataProvider = CGDataProviderCreateWithCFData(swappedImgData);
    CFRelease(swappedImgData);
    CGImageRef image = CGImageCreate(width, height, bitsPerComponent, bitsPerPixel, bytesPerRow, colorSpace, info, imgDataProvider, decode, shouldInteroplate, intent);
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(imgDataProvider);
    QLThumbnailRequestSetImage(thumbnail, image, NULL);
    CGImageRelease(image);
    gzclose(file);
    return noErr;
}

void CancelThumbnailGeneration(void *thisInterface, QLThumbnailRequestRef thumbnail)
{
    // Implement only if supported
}
