#include "Common.h"
#include "MemoryImage.h"
#include "SexyAppBase.h"
#include "Graphics.h"
#include "DDInterface.h"
#include "D3DInterface.h"
#include "NativeDisplay.h"
#include "IMG_savepng.h"
#include "GLState.h"

#if 0
#include "PerfTimer.h"
#include "Debug.h"
#endif

#include "Quantize.h"
#include "SWTri.h"

#include <math.h>
#include <assert.h>
#include <string.h>
#include <SDL.h>

// Enable this define USE_GL_RGBA to use GL_RGBA format for the image textures.
#define USE_GL_RGBA 1

using namespace Sexy;

MemoryImage::MemoryImage()
{
    TLOG(mLogFacil, 1, "new MemoryImage()");
    Init();
}

// FIXME. Why do we need this constructor?
MemoryImage::MemoryImage(SexyAppBase* theApp)
{
    TLOG(mLogFacil, 1, "new MemoryImage(theApp)");
    mApp = theApp;
    Init();
}

MemoryImage::MemoryImage(const MemoryImage& theMemoryImage) :
    Image(theMemoryImage),

//    uint32_t*               mBits;

//    uint32_t*               mColorTable;
//    uchar*                  mColorIndices;

    mForcedMode(false),
    mIsVolatile(theMemoryImage.mIsVolatile),
    mBitsChanged(theMemoryImage.mBitsChanged),
    mWantPal(theMemoryImage.mWantPal),
    mOptimizeSoftwareDrawing(false),

    mPurgeBits(theMemoryImage.mPurgeBits),
    mBitsChangedCount(theMemoryImage.mBitsChangedCount)

//    uint32_t*               mNativeAlphaData;
//    uchar*                  mRLAlphaData;
//    uchar*                  mRLAdditiveData;
{
    mLogFacil = NULL;
#ifdef DEBUG
    mLogFacil = LoggerFacil::find("image");
    TLOG(mLogFacil, 1, "new MemoryImage (copy)");
#endif

    MemoryImage* aNonConstMemoryImage = (MemoryImage*) &theMemoryImage;

    bool deleteBits = false;
    if ((theMemoryImage.mBits == NULL) && (theMemoryImage.mColorTable == NULL))
    {
        // An ugly hack. We're forcing that mBits exists in the (const) source. Further down it is deleted again.
        // Must be a DDImage with only a DDSurface
        aNonConstMemoryImage->GetBits();
        deleteBits = true;
    }

    if (theMemoryImage.mBits != NULL)
    {
        mBits = new uint32_t[mWidth*mHeight + 1];
        mBits[mWidth*mHeight] = MEMORYCHECK_ID;
        memcpy(mBits, theMemoryImage.mBits, (mWidth*mHeight + 1)*sizeof(uint32_t));
    }
    else
        mBits = NULL;

    if (deleteBits)
    {
        // Remove the temporary source bits. See above.
        delete [] aNonConstMemoryImage->mBits;
        aNonConstMemoryImage->mBits = NULL;
    }

    if (theMemoryImage.mColorTable != NULL)
    {
        mColorTable = new uint32_t[256];
        memcpy(mColorTable, theMemoryImage.mColorTable, 256*sizeof(uint32_t));
    }
    else
        mColorTable = NULL;

    if (theMemoryImage.mColorIndices != NULL)
    {
        mColorIndices = new uchar[mWidth*mHeight];
        memcpy(mColorIndices, theMemoryImage.mColorIndices, mWidth*mHeight*sizeof(uchar));
    }
    else
        mColorIndices = NULL;

    if (theMemoryImage.mNativeAlphaData != NULL)
    {
        if (theMemoryImage.mColorTable == NULL)
        {
            mNativeAlphaData = new uint32_t[mWidth*mHeight];
            memcpy(mNativeAlphaData, theMemoryImage.mNativeAlphaData, mWidth*mHeight*sizeof(uint32_t));
        }
        else
        {
            mNativeAlphaData = new uint32_t[256];
            memcpy(mNativeAlphaData, theMemoryImage.mNativeAlphaData, 256*sizeof(uint32_t));
        }
    }
    else
        mNativeAlphaData = NULL;

    if (theMemoryImage.mRLAlphaData != NULL)
    {
        mRLAlphaData = new uchar[mWidth*mHeight];
        memcpy(mRLAlphaData, theMemoryImage.mRLAlphaData, mWidth*mHeight);
    }
    else
        mRLAlphaData = NULL;

    if (theMemoryImage.mRLAdditiveData != NULL)
    {
        mRLAdditiveData = new uchar[mWidth*mHeight];
        memcpy(mRLAdditiveData, theMemoryImage.mRLAdditiveData, mWidth*mHeight);
    }
    else
        mRLAdditiveData = NULL;

    mApp->AddImage(this);

    // TODO. Determine mOptimizeSoftwareDrawing from masks.
    // The masks are probably: R=0xff0000, G=0x00ff00, B=0x0000ff
}

MemoryImage::~MemoryImage()
{
    delete [] mBits;
    delete [] mNativeAlphaData;
    delete [] mRLAlphaData;
    delete [] mRLAdditiveData;
    delete [] mColorIndices;
    delete [] mColorTable;
}

void MemoryImage::Init()
{
    mBits = NULL;
    mColorTable = NULL;
    mColorIndices = NULL;

    mNativeAlphaData = NULL;
    mRLAlphaData = NULL;
    mRLAdditiveData = NULL;
    mBitsChanged = false;
    mForcedMode = false;
    mIsVolatile = false;

    mBitsChangedCount = 0;

    mPurgeBits = false;
    mWantPal = false;

    mApp->AddImage(this);

    // TODO. Determine mOptimizeSoftwareDrawing from masks.
    // The masks are probably (see below): R=0xff0000, G=0x00ff00, B=0x0000ff
    mOptimizeSoftwareDrawing = false;
}

void MemoryImage::BitsChanged()
{
    mBitsChanged = true;
    mBitsChangedCount++;

    delete [] mNativeAlphaData;
    mNativeAlphaData = NULL;

    delete [] mRLAlphaData;
    mRLAlphaData = NULL;

    delete [] mRLAdditiveData;
    mRLAdditiveData = NULL;

    // Verify secret value at end to protect against overwrite
    if (mBits != NULL)
    {
        assert(mBits[mWidth*mHeight] == MEMORYCHECK_ID);
    }
}

void MemoryImage::NormalDrawLine(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor)
{
    double aMinX = std::min(theStartX, theEndX);
    double aMinY = std::min(theStartY, theEndY);
    double aMaxX = std::max(theStartX, theEndX);
    double aMaxY =std::max(theStartY, theEndY);

    // Masks can become class member of MemoryImage
    uint32_t aRMask = 0xFF0000;
    uint32_t aGMask = 0x00FF00;
    uint32_t aBMask = 0x0000FF;
    uint32_t aRRoundAdd = aRMask >> 1;
    uint32_t aGRoundAdd = aGMask >> 1;
    uint32_t aBRoundAdd = aBMask >> 1;

    uint32_t *aSurface = GetBits();

        //FIXME
    if (true)//(mLockedSurfaceDesc.ddpfPixelFormat.dwRGBBitCount == 32)
    {
        if (theColor.mAlpha == 255)
        {
            uint32_t aColor = 0xFF000000 |
                ((((theColor.mRed * aRMask) + aRRoundAdd) >> 8) & aRMask) |
                ((((theColor.mGreen * aGMask) + aGRoundAdd) >> 8) & aGMask) |
                ((((theColor.mBlue * aBMask) + aBRoundAdd) >> 8) & aBMask);

            double dv = theEndY - theStartY;
            double dh = theEndX - theStartX;
            int minG, maxG, G, DeltaG1, DeltaG2;
            double swap;
            int inc = 1;
            int aCurX;
            int aCurY;
            int aRowWidth = mWidth;
            int aRowAdd = aRowWidth;;

            if (abs((int)dv) < abs((int)dh))
            {
                // Mostly horizontal
                if (dh < 0)
                {
                    dh = -dh;
                    dv = -dv;
                    swap = theEndY;
                    theEndY = theStartY;
                    theStartY = swap;
                    swap = theEndX;
                    theEndX = theStartX;
                    theStartX = swap;
                }
                if (dv < 0)
                {
                    dv = -dv;
                    inc = -1;
                    aRowAdd = -aRowAdd;
                }

                uint32_t* aDestPixels = ((uint32_t*) aSurface) + ((int) theStartY * aRowWidth) + (int) theStartX;
                *aDestPixels = aColor;
                aDestPixels++;

                aCurY = (int)theStartY;
                aCurX = (int)theStartX + 1;

                G = 2 * (int)(dv - dh);
                DeltaG1 = 2 * (int)(dv - dh);
                DeltaG2 = 2 * (int)dv;

                G += DeltaG2 * (int)(((int)theStartY - (int) theStartY));

                while (aCurX <= theEndX)
                {
                    if (G > 0)
                    {
                        G += DeltaG1;
                        aCurY += inc;
                        aDestPixels += aRowAdd;

                        if (aCurX<aMinX || aCurY<aMinY || aCurX>aMaxX || aCurY>aMaxY)
                            break;
                    }
                    else
                        G += DeltaG2;

                    *aDestPixels = aColor;

                    aCurX++;
                    aDestPixels++;
                }
            }
            else
            {
                // Mostly vertical
                if ( dv < 0 )
                {
                    dh = -dh;
                    dv = -dv;
                    swap = theEndY;
                    theEndY = theStartY;
                    theStartY = swap;
                    swap = theEndX;
                    theEndX = theStartX;
                    theStartX = swap;
                }

                if (dh < 0)
                {
                    dh = -dh;
                    inc = -1;
                }

                uint32_t* aDestPixels = ((uint32_t*) aSurface) + ((int) theStartY * aRowWidth) + (int) theStartX;
                *aDestPixels = aColor;
                aDestPixels += aRowAdd;

                aCurX = (int)theStartX;
                aCurY = (int)theStartY + 1;

                G = 2 * (int)(dh - dv);
                minG = maxG = G;
                DeltaG1 = 2 * (int)( dh - dv );
                DeltaG2 = 2 * (int)dh;

                G += DeltaG2 * (int)(((int)theStartX - (int) theStartX));

                while (aCurY <= theEndY)
                {
                    if ( G > 0 )
                    {
                        G += DeltaG1;
                        aCurX += inc;
                        aDestPixels += inc;

                        if (aCurX<aMinX || aCurY<aMinY || aCurX>aMaxX || aCurY>aMaxY)
                            break;
                    }
                    else
                        G += DeltaG2;

                    *aDestPixels = aColor;

                    aCurY++;
                    aDestPixels += aRowAdd;
                }
            }
        }
        else
        {
            uint32_t src = 0xFF000000 |
                ((((((theColor.mRed * theColor.mAlpha + 0x80) >> 8) * aRMask) + aRRoundAdd) >> 8) & aRMask) |
                ((((((theColor.mGreen * theColor.mAlpha + 0x80) >> 8) * aGMask) + aGRoundAdd) >> 8) & aGMask) |
                ((((((theColor.mBlue * theColor.mAlpha + 0x80) >> 8) * aBMask) + aBRoundAdd) >> 8) & aBMask);
            int oma = 256 - theColor.mAlpha;

            double dv = theEndY - theStartY;
            double dh = theEndX - theStartX;
            int minG, maxG, G, DeltaG1, DeltaG2;
            double swap;
            int inc = 1;
            int aCurX;
            int aCurY;
            int aRowWidth = mWidth;
            int aRowAdd = aRowWidth;

            if (abs((int)dv) < abs((int)dh))
            {
                // Mostly horizontal
                if (dh < 0)
                {
                    dh = -dh;
                    dv = -dv;
                    swap = theEndY;
                    theEndY = theStartY;
                    theStartY = swap;
                    swap = theEndX;
                    theEndX = theStartX;
                    theStartX = swap;
                }
                if (dv < 0)
                {
                    dv = -dv;
                    inc = -1;
                    aRowAdd = -aRowAdd;
                }

                uint32_t* aDestPixels = ((uint32_t*) aSurface) + ((int) theStartY * aRowWidth) + (int) theStartX;
                uint32_t dest = *aDestPixels;
                *(aDestPixels++) = src +
                    (((((dest & aRMask) * oma) + aRRoundAdd) >> 8) & aRMask) +
                    (((((dest & aGMask) * oma) + aGRoundAdd) >> 8) & aGMask) +
                    (((((dest & aBMask) * oma) + aBRoundAdd) >> 8) & aBMask);

                aCurY = (int)theStartY;
                aCurX = (int)theStartX + 1;

                G = 2 * (int)(dv - dh);
                DeltaG1 = 2 * (int)(dv - dh);
                DeltaG2 = 2 * (int)dv;

                G += DeltaG2 * (int)(((int)theStartX - (int) theStartX));

                while (aCurX <= theEndX)
                {
                    if (G > 0)
                    {
                        G += DeltaG1;
                        aCurY += inc;
                        aDestPixels += aRowAdd;

                        if (aCurX<aMinX || aCurY<aMinY || aCurX>aMaxX || aCurY>aMaxY)
                            break;
                    }
                    else
                        G += DeltaG2;

                    dest = *aDestPixels;
                    *(aDestPixels++) = src +
                        (((((dest & aRMask) * oma) + aRRoundAdd) >> 8) & aRMask) +
                        (((((dest & aGMask) * oma) + aGRoundAdd) >> 8) & aGMask) +
                        (((((dest & aBMask) * oma) + aBRoundAdd) >> 8) & aBMask);

                    aCurX++;
                }
            }
            else
            {
                // Mostly vertical
                if ( dv < 0 )
                {
                    dh = -dh;
                    dv = -dv;
                    swap = theEndY;
                    theEndY = theStartY;
                    theStartY = swap;
                    swap = theEndX;
                    theEndX = theStartX;
                    theStartX = swap;
                }

                if (dh < 0)
                {
                    dh = -dh;
                    inc = -1;
                }

                uint32_t* aDestPixels = ((uint32_t*) aSurface) + ((int) theStartY * aRowWidth) + (int) theStartX;
                uint32_t dest = *aDestPixels;
                *aDestPixels = src +
                    (((((dest & aRMask) * oma) + aRRoundAdd) >> 8) & aRMask) +
                    (((((dest & aGMask) * oma) + aGRoundAdd) >> 8) & aGMask) +
                    (((((dest & aBMask) * oma) + aBRoundAdd) >> 8) & aBMask);
                aDestPixels += aRowAdd;

                aCurX = (int)theStartX;
                aCurY = (int)theStartY + 1;

                G = 2 * (int)(dh - dv);
                minG = maxG = G;
                DeltaG1 = 2 * (int)( dh - dv );
                DeltaG2 = 2 * (int)dh;

                G += DeltaG2 * (int)(((int)theStartX - (int) theStartX));

                while (aCurY <= theEndY)
                {
                    if ( G > 0 )
                    {
                        G += DeltaG1;
                        aCurX += inc;
                        aDestPixels += inc;

                        if (aCurX<aMinX || aCurY<aMinY || aCurX>aMaxX || aCurY>aMaxY)
                            break;
                    }
                    else
                        G += DeltaG2;

                    dest = *aDestPixels;
                    *aDestPixels = src +
                        (((((dest & aRMask) * oma) + aRRoundAdd) >> 8) & aRMask) +
                        (((((dest & aGMask) * oma) + aGRoundAdd) >> 8) & aGMask) +
                        (((((dest & aBMask) * oma) + aBRoundAdd) >> 8) & aBMask);

                    aCurY++;
                    aDestPixels += aRowAdd;
                }
            }
        }
    }
}

void MemoryImage::AdditiveDrawLine(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor)
{
    double aMinX = std::min(theStartX, theEndX);
    double aMinY = std::min(theStartY, theEndY);
    double aMaxX =std::max(theStartX, theEndX);
    double aMaxY = std::max(theStartY, theEndY);

    // Masks can become class member of MemoryImage
    uint32_t aRMask = 0xFF0000;
    uint32_t aGMask = 0x00FF00;
    uint32_t aBMask = 0x0000FF;
    int aRedShift = 16;
    int aGreenShift = 8;
    int aBlueShift = 0;

    uchar* aMaxTable = mApp->mAdd8BitMaxTable;
    uint32_t *aSurface = GetBits();

    if (true)//(mLockedSurfaceDesc.ddpfPixelFormat.dwRGBBitCount == 32)
    {
        uint32_t rc = ((theColor.mRed * theColor.mAlpha) / 255);
        uint32_t gc = ((theColor.mGreen * theColor.mAlpha) / 255);
        uint32_t bc = ((theColor.mBlue * theColor.mAlpha) / 255);

        double dv = theEndY - theStartY;
        double dh = theEndX - theStartX;
        int minG, maxG, G, DeltaG1, DeltaG2;
        double swap;
        int inc = 1;
        int aCurX;
        int aCurY;
        int aRowWidth = mWidth;
        int aRowAdd = aRowWidth;

        if (abs((int)dv) < abs((int)dh))
        {
            // Mostly horizontal
            if (dh < 0)
            {
                dh = -dh;
                dv = -dv;
                swap = theEndY;
                theEndY = theStartY;
                theStartY = swap;
                swap = theEndX;
                theEndX = theStartX;
                theStartX = swap;
            }

            if (dv < 0)
            {
                dv = -dv;
                inc = -1;
                aRowAdd = -aRowAdd;
            }

            uint32_t* aDestPixels = ((uint32_t*) aSurface) + ((int) theStartY * aRowWidth) + (int) theStartX;
            uint32_t dest = *aDestPixels;

            int r = aMaxTable[((dest & aRMask) >> aRedShift) + rc];
            int g = aMaxTable[((dest & aGMask) >> aGreenShift) + gc];
            int b = aMaxTable[((dest & aBMask) >> aBlueShift) + bc];

            *(aDestPixels++) =
                0xFF000000 |
                (r << aRedShift) |
                (g << aGreenShift) |
                (b << aBlueShift);

            aCurY = (int)theStartY;
            aCurX = (int)theStartX + 1;

            G = 2 * (int)(dv - dh);
            DeltaG1 = 2 * (int)(dv - dh);
            DeltaG2 = 2 * (int)dv;

            while (aCurX <= theEndX)
            {
                if (G > 0)
                {
                    G += DeltaG1;
                    aCurY += inc;
                    aDestPixels += aRowAdd;

                    if (aCurX<aMinX || aCurY<aMinY || aCurX>aMaxX || aCurY>aMaxY)
                        break;
                }
                else
                    G += DeltaG2;

                dest = *aDestPixels;

                r = aMaxTable[((dest & aRMask) >> aRedShift) + rc];
                g = aMaxTable[((dest & aGMask) >> aGreenShift) + gc];
                b = aMaxTable[((dest & aBMask) >> aBlueShift) + bc];

                *(aDestPixels++) =
                    0xFF000000 |
                    (r << aRedShift) |
                    (g << aGreenShift) |
                    (b << aBlueShift);

                aCurX++;
            }
        }
        else
        {
            // Mostly vertical
            if ( dv < 0 )
            {
                dh = -dh;
                dv = -dv;
                swap = theEndY;
                theEndY = theStartY;
                theStartY = swap;
                swap = theEndX;
                theEndX = theStartX;
                theStartX = swap;
            }

            if (dh < 0)
            {
                dh = -dh;
                inc = -1;
            }

            uint32_t* aDestPixels = ((uint32_t*) aSurface) + ((int) theStartY * mWidth) + (int) theStartX;

            uint32_t dest = *aDestPixels;

            int r = aMaxTable[((dest & aRMask) >> aRedShift) + rc];
            int g = aMaxTable[((dest & aGMask) >> aGreenShift) + gc];
            int b = aMaxTable[((dest & aBMask) >> aBlueShift) + bc];

            *aDestPixels =
                0xFF000000 |
                (r << aRedShift) |
                (g << aGreenShift) |
                (b << aBlueShift);

            aDestPixels += aRowAdd;

            aCurX = (int)theStartX;
            aCurY = (int)theStartY + 1;

            G = 2 * (int)(dh - dv);
            minG = maxG = G;
            DeltaG1 = 2 * (int)( dh - dv );
            DeltaG2 = 2 * (int)dh;
            while (aCurY <= theEndY)
            {
                if ( G > 0 )
                {
                    G += DeltaG1;
                    aCurX += inc;
                    aDestPixels += inc;

                    if (aCurX<aMinX || aCurY<aMinY || aCurX>aMaxX || aCurY>aMaxY)
                        break;
                }
                else
                    G += DeltaG2;

                dest = *aDestPixels;

                r = aMaxTable[((dest & aRMask) >> aRedShift) + rc];
                g = aMaxTable[((dest & aGMask) >> aGreenShift) + gc];
                b = aMaxTable[((dest & aBMask) >> aBlueShift) + bc];

                *aDestPixels =
                    0xFF000000 |
                    (r << aRedShift) |
                    (g << aGreenShift) |
                    (b << aBlueShift);

                aCurY++;
                aDestPixels += aRowAdd;
            }
        }
    }
}


void MemoryImage::DrawLine(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor, int theDrawMode)
{
    if (theStartY == theEndY)
    {
        //FIXME is this correct or should it be the min on ints?
        int aStartX = (int)std::min(theStartX, theEndX);
        int aEndX = (int)std::max(theStartX, theEndX);

        FillRect(Rect(aStartX, (int)theStartY, aEndX-aStartX+1, (int)theEndY-(int)theStartY+1), theColor, theDrawMode);
        return;
    }
    else if (theStartX == theEndX)
    {
        //FIXME is this correct or should it be the min on ints?
        int aStartY = (int)std::min(theStartY, theEndY);
        int aEndY = (int)std::max(theStartY, theEndY);

        FillRect(Rect((int)theStartX, aStartY, (int)theEndX-(int)theStartX+1, aEndY-aStartY+1), theColor, theDrawMode);
        return;
    }

    switch (theDrawMode)
    {
    case Graphics::DRAWMODE_NORMAL:
        NormalDrawLine(theStartX, theStartY, theEndX, theEndY, theColor);
        break;
    case Graphics::DRAWMODE_ADDITIVE:
        AdditiveDrawLine(theStartX, theStartY, theEndX, theEndY, theColor);
        break;
    }

    BitsChanged();
}

void MemoryImage::NormalDrawLineAA(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor)
{
    uint32_t* aBits = GetBits();
    uint32_t color = theColor.ToInt();

    int aX0 = (int)theStartX, aX1 = (int)theEndX;
    int aY0 = (int)theStartY, aY1 = (int)theEndY;
    int aXinc = 1;
    if (aY0 > aY1)
    {
        int aTempX = aX0, aTempY = aY0;
        aX0 = aX1; aY0 = aY1;
        aX1 = aTempX; aY1 = aTempY;
        double aTempXd = theStartX, aTempYd = theStartY;
        theStartX = theEndX; theStartY = theEndY;
        theEndX = aTempXd; theEndY = aTempYd;
    }

    int dx = aX1 - aX0;
    int dy = aY1 - aY0;
    double dxd = theEndX - theStartX;
    double dyd = theEndY - theStartY;
    if (dx < 0)
    {
        dx = -dx;
        aXinc = -1;
        dxd = -dxd;
    }

    if (theColor.mAlpha != 255)
    {
        #define PIXEL_TYPE              uint32_t
        #define CALC_WEIGHT_A(w)        (((w) * (theColor.mAlpha+1)) >> 8)
        #define BLEND_PIXEL(p) \
        {\
            int aDestAlpha = dest >> 24;\
            int aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;\
            a = 255 * a / aNewDestAlpha;\
            oma = 256 - a;\
            *(p) = (aNewDestAlpha << 24) |\
                    ((((color & 0xFF0000) * a + (dest & 0xFF0000) * oma) >> 8) & 0xFF0000) |\
                    ((((color & 0x00FF00) * a + (dest & 0x00FF00) * oma) >> 8) & 0x00FF00) |\
                    ((((color & 0x0000FF) * a + (dest & 0x0000FF) * oma) >> 8) & 0x0000FF);\
        }
        const int STRIDE = mWidth;

        #include "GENERIC_DrawLineAA.inc"

        #undef PIXEL_TYPE
        #undef CALC_WEIGHT_A
        #undef BLEND_PIXEL
    }
    else
    {
        #define PIXEL_TYPE              uint32_t
        #define CALC_WEIGHT_A(w)        (w)
        #define BLEND_PIXEL(p) \
        {\
            int aDestAlpha = dest >> 24;\
            int aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;\
            a = 255 * a / aNewDestAlpha;\
            oma = 256 - a;\
            *(p) = (aNewDestAlpha << 24) |\
                    ((((color & 0xFF0000) * a + (dest & 0xFF0000) * oma) >> 8) & 0xFF0000) |\
                    ((((color & 0x00FF00) * a + (dest & 0x00FF00) * oma) >> 8) & 0x00FF00) |\
                    ((((color & 0x0000FF) * a + (dest & 0x0000FF) * oma) >> 8) & 0x0000FF);\
        }
        const int STRIDE = mWidth;

        #include "GENERIC_DrawLineAA.inc"

        #undef PIXEL_TYPE
        #undef CALC_WEIGHT_A
        #undef BLEND_PIXEL
    }


    BitsChanged();
}

void MemoryImage::AdditiveDrawLineAA(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor)
{
}

void MemoryImage::DrawLineAA(double theStartX, double theStartY, double theEndX, double theEndY, const Color& theColor, int theDrawMode)
{
    if (theStartY == theEndY)
    {
        //FIXME is this correct or should it be the min on ints?
        int aStartX = (int)std::min(theStartX, theEndX);
        int aEndX =(int)std:: max(theStartX, theEndX);

        FillRect(Rect(aStartX, (int)theStartY, aEndX-aStartX+1, (int)theEndY-(int)theStartY+1), theColor, theDrawMode);
        return;
    }
    else if (theStartX == theEndX)
    {
        //FIXME is this correct or should it be the min on ints?
        int aStartY = (int)std::min(theStartY, theEndY);
        int aEndY = (int)std::max(theStartY, theEndY);

        FillRect(Rect((int)theStartX, aStartY, (int)theEndX-(int)theStartX+1, aEndY-aStartY+1), theColor, theDrawMode);
        return;
    }

    switch (theDrawMode)
    {
    case Graphics::DRAWMODE_NORMAL:
        NormalDrawLineAA(theStartX, theStartY, theEndX, theEndY, theColor);
        break;
    case Graphics::DRAWMODE_ADDITIVE:
        AdditiveDrawLineAA(theStartX, theStartY, theEndX, theEndY, theColor);
        break;
    }

    BitsChanged();
}


void MemoryImage::CommitBits()
{
    //if (gDebug)
    //  mApp->CopyToClipboard("+MemoryImage::CommitBits");

    if ((mBitsChanged) && (!mForcedMode))
    {
        // Analyze
        if (mBits != NULL)
        {
            mHasTrans = false;
            mHasAlpha = false;

            int aSize = mWidth*mHeight;
            uint32_t* ptr = mBits;

            for (int i = 0; i < aSize; i++)
            {
                uchar anAlpha = *ptr++ >> 24;

                if (anAlpha == 0) {
                    mHasTrans = true;
                    if (mHasAlpha)
                        break;              // No need to look any further
                }
                else if (anAlpha != 255) {
                    mHasAlpha = true;
                    if (mHasTrans)
                        break;              // No need to look any further
                }
            }
        }
        else if (mColorTable != NULL)
        {
            mHasTrans = false;
            mHasAlpha = false;

            int aSize = 256;
            uint32_t* ptr = mColorTable;

            for (int i = 0; i < aSize; i++)
            {
                uchar anAlpha = *ptr++ >> 24;

                if (anAlpha == 0)
                    mHasTrans = true;
                else if (anAlpha != 255)
                    mHasAlpha = true;
            }
        }
        else
        {
            mHasTrans = true;
            mHasAlpha = false;
        }

        mBitsChanged = false;
    }

    //if (gDebug)
    //  mApp->CopyToClipboard("-MemoryImage::CommitBits");
}

void MemoryImage::SetImageMode(bool hasTrans, bool hasAlpha)
{
    mForcedMode = true;
    mHasTrans = hasTrans;
    mHasAlpha = hasAlpha;
}

void MemoryImage::SetVolatile(bool isVolatile)
{
    mIsVolatile = isVolatile;
}

void* MemoryImage::GetNativeAlphaData(NativeDisplay *theDisplay)
{
    if (mNativeAlphaData != NULL)
        return mNativeAlphaData;

    CommitBits();

    const int rRightShift = 16 + (8-theDisplay->mRedBits);
    const int gRightShift = 8 + (8-theDisplay->mGreenBits);
    const int bRightShift = 0 + (8-theDisplay->mBlueBits);

    const int rLeftShift = theDisplay->mRedShift;
    const int gLeftShift = theDisplay->mGreenShift;
    const int bLeftShift = theDisplay->mBlueShift;

    const int rMask = theDisplay->mRedMask;
    const int gMask = theDisplay->mGreenMask;
    const int bMask = theDisplay->mBlueMask;

    if (mColorTable == NULL)
    {
        uint32_t* aSrcPtr = GetBits();

        uint32_t* anAlphaData = new uint32_t[mWidth*mHeight];

        uint32_t* aDestPtr = anAlphaData;
        int aSize = mWidth*mHeight;
        for (int i = 0; i < aSize; i++)
        {
            uint32_t val = *(aSrcPtr++);

            int anAlpha = val >> 24;

            uint32_t r = ((val & 0xFF0000) * (anAlpha+1)) >> 8;
            uint32_t g = ((val & 0x00FF00) * (anAlpha+1)) >> 8;
            uint32_t b = ((val & 0x0000FF) * (anAlpha+1)) >> 8;

            *(aDestPtr++) =
                (((r >> rRightShift) << rLeftShift) & rMask) |
                (((g >> gRightShift) << gLeftShift) & gMask) |
                (((b >> bRightShift) << bLeftShift) & bMask) |
                (anAlpha << 24);
        }

        mNativeAlphaData = anAlphaData;
    }
    else
    {
        uint32_t* aSrcPtr = mColorTable;

        uint32_t* anAlphaData = new uint32_t[256];

        for (int i = 0; i < 256; i++)
        {
            uint32_t val = *(aSrcPtr++);

            int anAlpha = val >> 24;

            uint32_t r = ((val & 0xFF0000) * (anAlpha+1)) >> 8;
            uint32_t g = ((val & 0x00FF00) * (anAlpha+1)) >> 8;
            uint32_t b = ((val & 0x0000FF) * (anAlpha+1)) >> 8;

            anAlphaData[i] =
                (((r >> rRightShift) << rLeftShift) & rMask) |
                (((g >> gRightShift) << gLeftShift) & gMask) |
                (((b >> bRightShift) << bLeftShift) & bMask) |
                (anAlpha << 24);
        }


        mNativeAlphaData = anAlphaData;
    }

    return mNativeAlphaData;
}


uchar* MemoryImage::GetRLAlphaData()
{
    CommitBits();

    if (mRLAlphaData == NULL)
    {
        mRLAlphaData = new uchar[mWidth*mHeight];

        if (mColorTable == NULL)
        {
            uint32_t* aSrcPtr;
            if (mNativeAlphaData != NULL)
                aSrcPtr = (uint32_t*) mNativeAlphaData;
            else
                aSrcPtr = GetBits();

            #define NEXT_SRC_COLOR (*(aSrcPtr++))

            #include "MI_GetRLAlphaData.inc"

            #undef NEXT_SRC_COLOR
        }
        else
        {
            uchar* aSrcPtr = mColorIndices;
            uint32_t* aColorTable = mColorTable;

            #define NEXT_SRC_COLOR (aColorTable[*(aSrcPtr++)])

            #include "MI_GetRLAlphaData.inc"

            #undef NEXT_SRC_COLOR
        }
    }

    return mRLAlphaData;
}

uchar* MemoryImage::GetRLAdditiveData(NativeDisplay *theNative)
{
    if (mRLAdditiveData == NULL)
    {
        if (mColorTable == NULL)
        {
            uint32_t* aBits = (uint32_t*) GetNativeAlphaData(theNative);

            mRLAdditiveData = new uchar[mWidth*mHeight];

            uchar* aWPtr = mRLAdditiveData;
            uint32_t* aRPtr = aBits;

            if (mWidth==1)
            {
                memset(aWPtr,1,mHeight);
            }
            else
            {
                for (int aRow = 0; aRow < mHeight; aRow++)
                {
                    int aRCount = 1;
                    int aRLCount = 1;

                    int aLastAClass = (((*aRPtr++) & 0xFFFFFF) != 0) ? 1 : 0;

                    while (aRCount < mWidth)
                    {
                        aRCount++;

                        int aThisAClass = (((*aRPtr++) & 0xFFFFFF) != 0) ? 1 : 0;

                        if ((aThisAClass != aLastAClass) || (aRCount == mWidth))
                        {
                            if (aThisAClass == aLastAClass)
                                aRLCount++;

                            for (int i = aRLCount; i > 0; i--)
                            {
                                if (i >= 255)
                                    *aWPtr++ = 255;
                                else
                                    *aWPtr++ = i;
                            }

                            if ((aRCount == mWidth) && (aThisAClass != aLastAClass))
                                *aWPtr++ = 1;

                            aLastAClass = aThisAClass;
                            aRLCount = 1;
                        }
                        else
                        {
                            aRLCount++;
                        }
                    }
                }
            }
        }
        else
        {
            uint32_t* aNativeColorTable = (uint32_t*) GetNativeAlphaData(theNative);

            mRLAdditiveData = new uchar[mWidth*mHeight];

            uchar* aWPtr = mRLAdditiveData;
            uchar* aRPtr = mColorIndices;

            if (mWidth==1)
            {
                memset(aWPtr,1,mHeight);
            }
            else
            {
                for (int aRow = 0; aRow < mHeight; aRow++)
                {
                    int aRCount = 1;
                    int aRLCount = 1;

                    int aLastAClass = (((aNativeColorTable[*aRPtr++]) & 0xFFFFFF) != 0) ? 1 : 0;

                    while (aRCount < mWidth)
                    {
                        aRCount++;

                        int aThisAClass = (((aNativeColorTable[*aRPtr++]) & 0xFFFFFF) != 0) ? 1 : 0;

                        if ((aThisAClass != aLastAClass) || (aRCount == mWidth))
                        {
                            if (aThisAClass == aLastAClass)
                                aRLCount++;

                            for (int i = aRLCount; i > 0; i--)
                            {
                                if (i >= 255)
                                    *aWPtr++ = 255;
                                else
                                    *aWPtr++ = i;
                            }

                            if ((aRCount == mWidth) && (aThisAClass != aLastAClass))
                                *aWPtr++ = 1;

                            aLastAClass = aThisAClass;
                            aRLCount = 1;
                        }
                        else
                        {
                            aRLCount++;
                        }
                    }
                }
            }
        }
    }

    return mRLAdditiveData;
}

void MemoryImage::DoPurgeBits()
{
    mPurgeBits = true;
    if (mApp->Is3DAccelerated())
    {
        // Due to potential D3D threading issues we have to defer the texture creation
        //  and therefore the actual purging
        if (!HasTextureData())
            return;
    }
    else
    {
        if ((mBits == NULL) && (mColorIndices == NULL))
            return;
        GetNativeAlphaData(gSexyAppBase->mDDInterface);
    }

    delete [] mBits;
    mBits = NULL;

    if (HasTextureData())
    {
        delete [] mColorIndices;
        mColorIndices = NULL;

        delete [] mColorTable;
        mColorTable = NULL;
    }
}

void MemoryImage::DeleteSWBuffers()
{
    if ((mBits == NULL) && (mColorIndices == NULL))
        GetBits();

    delete [] mNativeAlphaData;
    mNativeAlphaData = NULL;

    delete [] mRLAdditiveData;
    mRLAdditiveData = NULL;

    delete [] mRLAlphaData;
    mRLAlphaData = NULL;
}

void MemoryImage::DeleteNativeData()
{
    if ((mBits == NULL) && (mColorIndices == NULL))
        GetBits(); // We need to keep the bits around

    delete [] mNativeAlphaData;
    mNativeAlphaData = NULL;

    delete [] mRLAdditiveData;
    mRLAdditiveData = NULL;
}

void MemoryImage::ReInit()
{
    // Fix any un-palletizing
    if (mWantPal)
        Palletize();

    if (mPurgeBits)
        DoPurgeBits();
}

void MemoryImage::SetBits(uint32_t* theBits, int theWidth, int theHeight, bool commitBits)
{
    if (theBits != mBits)
    {
        delete [] mColorIndices;
        mColorIndices = NULL;

        delete [] mColorTable;
        mColorTable = NULL;

        if (theWidth != mWidth || theHeight != mHeight)
        {
            delete [] mBits;
            mBits = new uint32_t[theWidth*theHeight + 1];
            mWidth = theWidth;
            mHeight = theHeight;
        }
        memcpy(mBits, theBits, mWidth*mHeight*sizeof(uint32_t));
        mBits[mWidth*mHeight] = MEMORYCHECK_ID;

        BitsChanged();
        if (commitBits)
            CommitBits();
    }
}

void MemoryImage::Create(int theWidth, int theHeight)
{
    delete [] mBits;
    mBits = NULL;

    mWidth = theWidth;
    mHeight = theHeight;

    // All zeros --> trans + alpha
    mHasTrans = true;
    mHasAlpha = true;

    BitsChanged();
}

uint32_t* MemoryImage::GetBits()
{
    if (mBits == NULL)
    {
        int aSize = mWidth*mHeight;

        mBits = new uint32_t[aSize+1];
        mBits[aSize] = MEMORYCHECK_ID;

        if (mColorTable != NULL)
        {
            TLOG(mLogFacil, 1, "copy bits from mColorIndices");
            for (int i = 0; i < aSize; i++)
                mBits[i] = mColorTable[mColorIndices[i]];

            delete [] mColorIndices;
            mColorIndices = NULL;

            delete [] mColorTable;
            mColorTable = NULL;

            delete [] mNativeAlphaData;
            mNativeAlphaData = NULL;
        }
        else if (mNativeAlphaData != NULL)
        {
            TLOG(mLogFacil, 1, "copy bits from NativeDisplay");
            // Copy the bits to the new buffer
            // The colors in the pixel are properly placed to get ARGB
            NativeDisplay* aDisplay = gSexyAppBase->mDDInterface;

            const int rMask = aDisplay->mRedMask;
            const int gMask = aDisplay->mGreenMask;
            const int bMask = aDisplay->mBlueMask;

            const int rLeftShift = aDisplay->mRedShift   + aDisplay->mRedBits;
            const int gLeftShift = aDisplay->mGreenShift + aDisplay->mGreenBits;
            const int bLeftShift = aDisplay->mBlueShift  + aDisplay->mBlueBits;

            uint32_t* aDestPtr = mBits;
            uint32_t* aSrcPtr = mNativeAlphaData;

            int aSize = mWidth*mHeight;
            for (int i = 0; i < aSize; i++)
            {
                uint32_t val = *(aSrcPtr++);

                uint32_t anAlpha = val >> 24;

                uint32_t r = (((((val & rMask) << 8) / (anAlpha+1)) & rMask) << 8) >> rLeftShift;
                uint32_t g = (((((val & gMask) << 8) / (anAlpha+1)) & gMask) << 8) >> gLeftShift;
                uint32_t b = (((((val & bMask) << 8) / (anAlpha+1)) & bMask) << 8) >> bLeftShift;

                // ARGB
                *(aDestPtr++) = (anAlpha << 24) | (r << 16) | (g << 8) | (b);
            }
        }
        else if (!HasTextureData())
        {
            TLOG(mLogFacil, 1, "no texture data");
            memset(mBits, 0, aSize*sizeof(uint32_t));
        }
        else if (!RecoverBits())
        {
            TLOG(mLogFacil, 1, "???? RecoverBits failed");
            memset(mBits, 0, aSize*sizeof(uint32_t));
        }
    }

    return mBits;
}

bool MemoryImage::RecoverBits()
{
    // Please notice that this function was moved here from D3DInterface.
    // Also notice that the commented out code was incomplete to begin with (missing switch ...).
#if 0
    if (!HasTextureData())
        return false;

    TextureData* aData = (TextureData*) mD3DData;
    if (aData->mBitsChangedCount != mBitsChangedCount) // bits have changed since texture was created
        return false;

    for (int aPieceRow = 0; aPieceRow < aData->mTexVecHeight; aPieceRow++) {
        for (int aPieceCol = 0; aPieceCol < aData->mTexVecWidth; aPieceCol++) {
            TextureDataPiece* aPiece = &aData->mTextures[aPieceRow * aData->mTexVecWidth + aPieceCol];

            //DDSURFACEDESC2 aDesc;
            //aDesc.dwSize = sizeof(aDesc);
            int offx = aPieceCol * aData->mTexPieceWidth;
            int offy = aPieceRow * aData->mTexPieceHeight;
            int aWidth = std::min(GetWidth() - offx, aPiece->mWidth);
            int aHeight = std::min(GetHeight() - offy, aPiece->mHeight);

            switch (aData->mPixelFormat) {
            case PixelFormat_A8R8G8B8:
                CopySurface8888ToImage(aPiece->mTexture, aDesc.lPitch, theImage, offx, offy, aWidth, aHeight);
                break;
            case PixelFormat_A4R4G4B4:
                CopyTexture4444ToImage(aDesc.lpSurface, aDesc.lPitch, theImage, offx, offy, aWidth, aHeight);
                break;
            case PixelFormat_R5G6B5:
                CopyTexture565ToImage(aDesc.lpSurface, aDesc.lPitch, theImage, offx, offy, aWidth, aHeight);
                break;
            case PixelFormat_Palette8:
                CopyTexturePalette8ToImage(aDesc.lpSurface, aDesc.lPitch, theImage, offx, offy, aWidth, aHeight, aData->mPalette);
                break;
            }
        }
    }

#endif
    return true;
}

void MemoryImage::FillRect(const Rect& theRect, const Color& theColor, int theDrawMode)
{
    uint32_t src = theColor.ToInt();

    uint32_t* aBits = GetBits();

    int oldAlpha = src >> 24;

    if (oldAlpha == 0xFF)
    {
        for (int aRow = theRect.mY; aRow < theRect.mY+theRect.mHeight; aRow++)
        {
            uint32_t* aDestPixels = &aBits[aRow*mWidth+theRect.mX];

            for (int i = 0; i < theRect.mWidth; i++)
                *aDestPixels++ = src;
        }
    }
    else
    {
        for (int aRow = theRect.mY; aRow < theRect.mY+theRect.mHeight; aRow++)
        {
            uint32_t* aDestPixels = &aBits[aRow*mWidth+theRect.mX];

            for (int i = 0; i < theRect.mWidth; i++)
            {
                uint32_t dest = *aDestPixels;

                int aDestAlpha = dest >> 24;
                int aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * oldAlpha) / 255;

                int newAlpha = 255 * oldAlpha / aNewDestAlpha;

                int oma = 256 - newAlpha;

#ifdef OPTIMIZE_SOFTWARE_DRAWING
                *(aDestPixels++) = (aNewDestAlpha << 24) |
                    ((((dest & 0xFF00FF) * oma + (src & 0xFF00FF) * newAlpha) >> 8) & 0xFF00FF) |
                    ((((dest & 0x00FF00) * oma + (src & 0x00FF00) * newAlpha) >> 8) & 0x00FF00);
#else
                *(aDestPixels++) = (aNewDestAlpha << 24) |
                    ((((dest & 0x0000FF) * oma) >> 8) + (((src & 0x0000FF) * newAlpha) >> 8) & 0x0000FF) |
                    ((((dest & 0x00FF00) * oma) >> 8) + (((src & 0x00FF00) * newAlpha) >> 8) & 0x00FF00) |
                    ((((dest & 0xFF0000) * oma) >> 8) + (((src & 0xFF0000) * newAlpha) >> 8) & 0xFF0000);
#endif
            }
        }
    }

    BitsChanged();
}

void MemoryImage::ClearRect(const Rect& theRect)
{
    uint32_t* aBits = GetBits();

    for (int aRow = theRect.mY; aRow < theRect.mY+theRect.mHeight; aRow++)
    {
        uint32_t* aDestPixels = &aBits[aRow*mWidth+theRect.mX];

        for (int i = 0; i < theRect.mWidth; i++)
            *aDestPixels++ = 0;
    }

    BitsChanged();
}

void MemoryImage::Clear()
{
    uint32_t* ptr = GetBits();
    if (ptr != NULL)
    {
        for (int i = 0; i < mWidth*mHeight; i++)
            *ptr++ = 0;

        BitsChanged();
    }
}

void MemoryImage::AdditiveBlt(Image* theImage, int theX, int theY, const Rect& theSrcRect, const Color& theColor)
{
    theImage->mDrawn = true;

    MemoryImage* aSrcMemoryImage = dynamic_cast<MemoryImage*>(theImage);

    uchar* aMaxTable = mApp->mAdd8BitMaxTable;

    if (aSrcMemoryImage != NULL)
    {
        if (aSrcMemoryImage->mColorTable == NULL)
        {
            uint32_t* aSrcBits = aSrcMemoryImage->GetBits();

            #define NEXT_SRC_COLOR      (*(aSrcPtr++))
            #define SRC_TYPE            uint32_t

            #include "MI_AdditiveBlt.inc"

            #undef NEXT_SRC_COLOR
            #undef SRC_TYPE
        }
        else
        {
            uint32_t* aColorTable = aSrcMemoryImage->mColorTable;
            uchar* aSrcBits = aSrcMemoryImage->mColorIndices;

            #define NEXT_SRC_COLOR      (aColorTable[*(aSrcPtr++)])
            #define SRC_TYPE uchar

            #include "MI_AdditiveBlt.inc"

            #undef NEXT_SRC_COLOR
            #undef SRC_TYPE
        }

        BitsChanged();
    }
}

void MemoryImage::NormalBlt(Image* theImage, int theX, int theY, const Rect& theSrcRect, const Color& theColor)
{
    theImage->mDrawn = true;

    MemoryImage* aSrcMemoryImage = dynamic_cast<MemoryImage*>(theImage);

    if (aSrcMemoryImage != NULL)
    {
        if (aSrcMemoryImage->mColorTable == NULL)
        {
            uint32_t* aSrcPixelsRow = ((uint32_t*) aSrcMemoryImage->GetBits()) + (theSrcRect.mY * theImage->GetWidth()) + theSrcRect.mX;

            #define NEXT_SRC_COLOR      (*(aSrcPtr++))
            #define READ_SRC_COLOR      (*(aSrcPtr))
            #define EACH_ROW            uint32_t* aSrcPtr = aSrcPixelsRow

            #include "MI_NormalBlt.inc"

            #undef NEXT_SRC_COLOR
            #undef READ_SRC_COLOR
            #undef EACH_ROW
        }
        else
        {
            uint32_t* aColorTable = aSrcMemoryImage->mColorTable;
            uchar* aSrcPixelsRow = aSrcMemoryImage->mColorIndices + (theSrcRect.mY * theImage->GetWidth()) + theSrcRect.mX;

            #define NEXT_SRC_COLOR      (aColorTable[*(aSrcPtr++)])
            #define READ_SRC_COLOR      (aColorTable[*(aSrcPtr)])
            #define EACH_ROW            uchar* aSrcPtr = aSrcPixelsRow

            #include "MI_NormalBlt.inc"

            #undef NEXT_SRC_COLOR
            #undef READ_SRC_COLOR
            #undef EACH_ROW
        }

        BitsChanged();
    }
}

void MemoryImage::Blt(Image* theImage, int theX, int theY, const Rect& theSrcRect, const Color& theColor, int theDrawMode)
{
    theImage->mDrawn = true;

    assert((theColor.mRed >= 0) && (theColor.mRed <= 255));
    assert((theColor.mGreen >= 0) && (theColor.mGreen <= 255));
    assert((theColor.mBlue >= 0) && (theColor.mBlue <= 255));
    assert((theColor.mAlpha >= 0) && (theColor.mAlpha <= 255));

    switch (theDrawMode)
    {
    case Graphics::DRAWMODE_NORMAL:
        NormalBlt(theImage, theX, theY, theSrcRect, theColor);
        break;
    case Graphics::DRAWMODE_ADDITIVE:
        AdditiveBlt(theImage, theX, theY, theSrcRect, theColor);
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void MemoryImage::BltClipF(Image* theImage, float theX, float theY, const Rect& theSrcRect, const Rect& theClipRect, const Color& theColor, int theDrawMode)
{
    theImage->mDrawn = true;

    BltRotated(theImage,theX,theY,theSrcRect,theClipRect,theColor,theDrawMode,0,0,0);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool MemoryImage::BltRotatedClipHelper(float &theX, float &theY, const Rect &theSrcRect, const Rect &theClipRect, double theRot, FRect &theDestRect, float theRotCenterX, float theRotCenterY)
{
    // Clipping Code (this used to be in Graphics::DrawImageRotated)
    float aCos = cosf(theRot);
    float aSin = sinf(theRot);

    // Map the four corners and find the bounding rectangle
    float px[4] = { 0, theSrcRect.mWidth, theSrcRect.mWidth, 0 };
    float py[4] = { 0, 0, theSrcRect.mHeight, theSrcRect.mHeight };
    float aMinX = 10000000;
    float aMaxX = -10000000;
    float aMinY = 10000000;
    float aMaxY = -10000000;

    for (int i=0; i<4; i++)
    {
        float ox = px[i] - theRotCenterX;
        float oy = py[i] - theRotCenterY;

        px[i] = (theRotCenterX + ox*aCos + oy*aSin) + theX;
        py[i] = (theRotCenterY + oy*aCos - ox*aSin) + theY;

        if (px[i] < aMinX)
            aMinX = px[i];
        if (px[i] > aMaxX)
            aMaxX = px[i];
        if (py[i] < aMinY)
            aMinY = py[i];
        if (py[i] > aMaxY)
            aMaxY = py[i];
    }



    FRect aClipRect(theClipRect.mX,theClipRect.mY,theClipRect.mWidth,theClipRect.mHeight);

    FRect aDestRect = FRect(aMinX, aMinY, aMaxX-aMinX, aMaxY-aMinY).Intersection(aClipRect);
    if ((aDestRect.mWidth <= 0) || (aDestRect.mHeight <= 0)) // nothing to draw
        return false;

    theDestRect = aDestRect;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool MemoryImage::StretchBltClipHelper(const Rect &theSrcRect, const Rect &theClipRect, const Rect &theDestRect, FRect &theSrcRectOut, Rect &theDestRectOut)
{
    theDestRectOut = Rect(theDestRect.mX , theDestRect.mY, theDestRect.mWidth, theDestRect.mHeight).Intersection(theClipRect);

    double aXFactor = theSrcRect.mWidth / (double) theDestRect.mWidth;
    double aYFactor = theSrcRect.mHeight / (double) theDestRect.mHeight;

    theSrcRectOut = FRect(theSrcRect.mX + (theDestRectOut.mX - theDestRect.mX)*aXFactor,
                   theSrcRect.mY + (theDestRectOut.mY - theDestRect.mY)*aYFactor,
                   theSrcRect.mWidth + (theDestRectOut.mWidth - theDestRect.mWidth)*aXFactor,
                   theSrcRect.mHeight + (theDestRectOut.mHeight - theDestRect.mHeight)*aYFactor);

    return theSrcRectOut.mWidth>0 && theSrcRectOut.mHeight>0;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool MemoryImage::StretchBltMirrorClipHelper(const Rect &theSrcRect, const Rect &theClipRect, const Rect &theDestRect, FRect &theSrcRectOut, Rect &theDestRectOut)
{
    theDestRectOut = Rect(theDestRect.mX, theDestRect.mY, theDestRect.mWidth, theDestRect.mHeight).Intersection(theClipRect);

    double aXFactor = theSrcRect.mWidth / (double) theDestRect.mWidth;
    double aYFactor = theSrcRect.mHeight / (double) theDestRect.mHeight;

    int aTotalClip = theDestRect.mWidth - theDestRectOut.mWidth;
    int aLeftClip = theDestRectOut.mX - theDestRect.mX;
    int aRightClip = aTotalClip-aLeftClip;

    theSrcRectOut = FRect(theSrcRect.mX + (aRightClip)*aXFactor,
                   theSrcRect.mY + (theDestRectOut.mY - theDestRect.mY)*aYFactor,
                   theSrcRect.mWidth + (theDestRectOut.mWidth - theDestRect.mWidth)*aXFactor,
                   theSrcRect.mHeight + (theDestRectOut.mHeight - theDestRect.mHeight)*aYFactor);

    return theSrcRectOut.mWidth>0 && theSrcRectOut.mHeight>0;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void MemoryImage::BltRotated(Image* theImage, float theX, float theY, const Rect &theSrcRect, const Rect& theClipRect, const Color& theColor, int theDrawMode, double theRot, float theRotCenterX, float theRotCenterY)
{
    theImage->mDrawn = true;

    // This BltRotatedClipHelper clipping used to happen in Graphics::DrawImageRotated
    FRect aDestRect;
    if (!BltRotatedClipHelper(theX, theY, theSrcRect, theClipRect, theRot, aDestRect,theRotCenterX,theRotCenterY))
        return;

    MemoryImage* aMemoryImage = dynamic_cast<MemoryImage*>(theImage);
    uchar* aMaxTable = mApp->mAdd8BitMaxTable;

    if (aMemoryImage != NULL)
    {
        if (aMemoryImage->mColorTable == NULL)
        {
            uint32_t* aSrcBits = aMemoryImage->GetBits() + theSrcRect.mX + theSrcRect.mY*theSrcRect.mWidth;

            #define SRC_TYPE uint32_t
            #define READ_COLOR(ptr) (*(ptr))

            if (theDrawMode == Graphics::DRAWMODE_NORMAL)
            {
                #include "MI_BltRotated.inc"
            }
            else
            {
                #include "MI_BltRotated_Additive.inc"
            }

            #undef SRC_TYPE
            #undef READ_COLOR
        }
        else
        {
            uint32_t* aColorTable = aMemoryImage->mColorTable;
            uchar* aSrcBits = aMemoryImage->mColorIndices + theSrcRect.mX + theSrcRect.mY*theSrcRect.mWidth;

            #define SRC_TYPE uchar
            #define READ_COLOR(ptr) (aColorTable[*(ptr)])

            if (theDrawMode == Graphics::DRAWMODE_NORMAL)
            {
                #include "MI_BltRotated.inc"
            }
            else
            {
                #include "MI_BltRotated_Additive.inc"
            }

            #undef SRC_TYPE
            #undef READ_COLOR
        }

        BitsChanged();
    }
}

void MemoryImage::SlowStretchBlt(Image* theImage, const Rect& theDestRect, const FRect& theSrcRect, const Color& theColor, int theDrawMode)
{
    theImage->mDrawn = true;

    // This thing was a pain to write.  I bet i could have gotten something just as good
    // from some Graphics Gems book.

    uint32_t* aDestEnd = GetBits() + (mWidth * mHeight);

    MemoryImage* aSrcMemoryImage = dynamic_cast<MemoryImage*>(theImage);

    if (aSrcMemoryImage != NULL)
    {
        if (aSrcMemoryImage->mColorTable == NULL)
        {
            uint32_t* aSrcBits = aSrcMemoryImage->GetBits();

            #define SRC_TYPE uint32_t
            #define READ_COLOR(ptr) (*(ptr))

            #include "MI_SlowStretchBlt.inc"

            #undef SRC_TYPE
            #undef READ_COLOR
        }
        else
        {
            uint32_t* aColorTable = aSrcMemoryImage->mColorTable;
            uchar* aSrcBits = aSrcMemoryImage->mColorIndices;

            #define SRC_TYPE uchar
            #define READ_COLOR(ptr) (aColorTable[*(ptr)])

            #include "MI_SlowStretchBlt.inc"

            #undef SRC_TYPE
            #undef READ_COLOR
        }

        BitsChanged();
    }
}

//TODO: Make the special version
void MemoryImage::FastStretchBlt(Image* theImage, const Rect& theDestRect, const FRect& theSrcRect, const Color& theColor, int theDrawMode)
{
    theImage->mDrawn = true;

    MemoryImage* aSrcMemoryImage = dynamic_cast<MemoryImage*>(theImage);

    if (aSrcMemoryImage != NULL)
    {
        uint32_t* aDestPixelsRow = ((uint32_t*) GetBits()) + (theDestRect.mY * mWidth) + theDestRect.mX;
        uint32_t* aSrcPixelsRow = (uint32_t*) aSrcMemoryImage->GetBits();;

        double aSrcY = theSrcRect.mY;

        double anAddX = theSrcRect.mWidth / theDestRect.mWidth;
        double anAddY = theSrcRect.mHeight / theDestRect.mHeight;

        if (theColor == Color::White)
        {
            for (int y = 0; y < theDestRect.mHeight; y++)
            {
                double aSrcX = theSrcRect.mX;

                uint32_t* aDestPixels = aDestPixelsRow;

                for (int x = 0; x < theDestRect.mWidth; x++)
                {
                    aSrcX += anAddX;

                    uint32_t* aSrcPixels = aSrcPixelsRow + ((int) aSrcX) + (aSrcMemoryImage->mWidth * ((int) aSrcY));
                    uint32_t src = *aSrcPixels;

                    uint32_t dest = *aDestPixels;

                    int a = src >> 24;

                    if (a != 0)
                    {
                        int aDestAlpha = dest >> 24;
                        int aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;

                        a = 255 * a / aNewDestAlpha;

                        int oma = 256 - a;

                        *(aDestPixels++) = (aNewDestAlpha << 24) |
                            ((((dest & 0x0000FF) * oma) >> 8) + (((src & 0x0000FF) * a) >> 8) & 0x0000FF) |
                            ((((dest & 0x00FF00) * oma) >> 8) + (((src & 0x00FF00) * a) >> 8) & 0x00FF00) |
                            ((((dest & 0xFF0000) * oma) >> 8) + (((src & 0xFF0000) * a) >> 8) & 0xFF0000);
                    }
                    else
                        aDestPixels++;
                }

                aDestPixelsRow += mWidth;
                aSrcY += anAddY;
            }
        }
        else
        {
        }
    }

    BitsChanged();
}

void MemoryImage::StretchBlt(Image* theImage, const Rect& theDestRect, const Rect& theSrcRect, const Rect& theClipRect, const Color& theColor, int theDrawMode, bool fastStretch)
{
    theImage->mDrawn = true;

    Rect aDestRect;
    FRect aSrcRect;

    if (!StretchBltClipHelper(theSrcRect, theClipRect, theDestRect, aSrcRect, aDestRect))
        return;

    if (fastStretch)
        FastStretchBlt(theImage, aDestRect, aSrcRect, theColor, theDrawMode);
    else
        SlowStretchBlt(theImage, aDestRect, aSrcRect, theColor, theDrawMode);
}

void MemoryImage::BltMatrixHelper(Image* theImage, float x, float y, const SexyMatrix3 &theMatrix, const Rect& theClipRect, const Color& theColor, int theDrawMode, const Rect &theSrcRect, void *theSurface, int theBytePitch, int thePixelFormat, bool blend)
{
    MemoryImage *anImage = dynamic_cast<MemoryImage*>(theImage);
    if (anImage==NULL)
        return;

    float w2 = theSrcRect.mWidth/2.0f;
    float h2 = theSrcRect.mHeight/2.0f;

    float u0 = (float)theSrcRect.mX / theImage->GetWidth();
    float u1 = (float)(theSrcRect.mX + theSrcRect.mWidth) / theImage->GetWidth();
    float v0 = (float)theSrcRect.mY / theImage->GetHeight();
    float v1 = (float)(theSrcRect.mY + theSrcRect.mHeight) / theImage->GetHeight();

    SWHelper::XYZStruct aVerts[4] =
    {
        { -w2,  -h2,    u0, v0, 0xFFFFFFFF },
        { w2,   -h2,    u1, v0, 0xFFFFFFFF },
        { -w2,  h2,     u0, v1, 0xFFFFFFFF },
        { w2,   h2,     u1, v1, 0xFFFFFFFF }
    };

    for (int i=0; i<4; i++)
    {
        SexyVector3 v(aVerts[i].mX, aVerts[i].mY, 1);
        v = theMatrix*v;
        aVerts[i].mX = v.x + x - 0.5f;
        aVerts[i].mY = v.y + y - 0.5f;
    }

    SWHelper::SWDrawShape(aVerts, 4, anImage, theColor, theDrawMode, theClipRect, theSurface, theBytePitch, thePixelFormat, blend,false);
}

void MemoryImage::BltMatrix(Image* theImage, float x, float y, const SexyMatrix3 &theMatrix, const Rect& theClipRect, const Color& theColor, int theDrawMode, const Rect &theSrcRect, bool blend)
{
    theImage->mDrawn = true;

    uint32_t *aSurface = GetBits();
    int aPitch = mWidth*4;
    int aFormat = 0x8888;
    if (mForcedMode && !mHasAlpha && !mHasTrans)
        aFormat = 0x888;

    BltMatrixHelper(theImage,x,y,theMatrix,theClipRect,theColor,theDrawMode,theSrcRect,aSurface,aPitch,aFormat,blend);
    BitsChanged();
}

void MemoryImage::BltTrianglesTexHelper(Image *theTexture, const TriVertex theVertices[][3], int theNumTriangles, const Rect &theClipRect, const Color &theColor, int theDrawMode, void *theSurface, int theBytePitch, int thePixelFormat, float tx, float ty, bool blend)
{
    MemoryImage *anImage = dynamic_cast<MemoryImage*>(theTexture);
//  if (anImage==NULL)
//      return;

    for (int i=0; i<theNumTriangles; i++)
    {
        bool vertexColor = false;

        SWHelper::XYZStruct aVerts[3];
        for (int j=0; j<3; j++)
        {
            aVerts[j].mX = theVertices[i][j].x + tx;
            aVerts[j].mY = theVertices[i][j].y + ty;
            aVerts[j].mU = theVertices[i][j].u;
            aVerts[j].mV = theVertices[i][j].v;
            aVerts[j].mDiffuse = theVertices[i][j].color;

            if (aVerts[j].mDiffuse!=0)
                vertexColor = true;
        }

        SWHelper::SWDrawShape(aVerts, 3, anImage, theColor, theDrawMode, theClipRect, theSurface, theBytePitch, thePixelFormat, blend, vertexColor);
    }

}

void MemoryImage::FillScanLinesWithCoverage(Span* theSpans, int theSpanCount, const Color& theColor, int theDrawMode, const unsigned char* theCoverage,
                                            int theCoverX, int theCoverY, int theCoverWidth, int theCoverHeight)
{
    uint32_t* theBits = GetBits();
    uint32_t src = theColor.ToInt();
    for (int i = 0; i < theSpanCount; ++i)
    {
        Span* aSpan = &theSpans[i];
        int x = aSpan->mX - theCoverX;
        int y = aSpan->mY - theCoverY;

        uint32_t* aDestPixels = &theBits[aSpan->mY*mWidth + aSpan->mX];
        const unsigned char* aCoverBits = &theCoverage[y*theCoverWidth+x];
        for (int w = 0; w < aSpan->mWidth; ++w)
        {
            int cover = *aCoverBits++ + 1;
            int a = (cover * theColor.mAlpha) >> 8;
            int oma;
            uint32_t dest = *aDestPixels;

            if (a > 0)
            {
                int aDestAlpha = dest >> 24;
                int aNewDestAlpha = aDestAlpha + ((255 - aDestAlpha) * a) / 255;

                a = 255 * a / aNewDestAlpha;
                oma = 256 - a;
                *(aDestPixels++) = (aNewDestAlpha << 24) |
                    ((((dest & 0x0000FF) * oma + (src & 0x0000FF) * a) >> 8) & 0x0000FF) |
                    ((((dest & 0x00FF00) * oma + (src & 0x00FF00) * a) >> 8) & 0x00FF00) |
                    ((((dest & 0xFF0000) * oma + (src & 0xFF0000) * a) >> 8) & 0xFF0000);
            }
        }
    }
    BitsChanged();
}

void MemoryImage::BltTrianglesTex(Image *theTexture, const TriVertex theVertices[][3], int theNumTriangles, const Rect& theClipRect, const Color &theColor, int theDrawMode, float tx, float ty, bool blend)
{
    theTexture->mDrawn = true;

    uint32_t *aSurface = GetBits();

    int aPitch = mWidth*4;
    int aFormat = 0x8888;
    if (mForcedMode && !mHasAlpha && !mHasTrans)
        aFormat = 0x888;

    BltTrianglesTexHelper(theTexture,theVertices,theNumTriangles,theClipRect,theColor,theDrawMode,aSurface,aPitch,aFormat,tx,ty,blend);
    BitsChanged();
}

bool MemoryImage::Palletize()
{
    CommitBits();

    if (mColorTable != NULL)
        return true;

    GetBits();

    if (mBits == NULL)
        return false;

    mColorIndices = new uchar[mWidth*mHeight];
    mColorTable = new uint32_t[256];

    if (!Quantize8Bit(mBits, mWidth, mHeight, mColorIndices, mColorTable))
    {
        delete [] mColorIndices;
        mColorIndices = NULL;

        delete [] mColorTable;
        mColorTable = NULL;

        mWantPal = false;

        return false;
    }

    delete [] mBits;
    mBits = NULL;

    delete [] mNativeAlphaData;
    mNativeAlphaData = NULL;

    mWantPal = true;

    return true;
}

void MemoryImage::CopyImageToSurface(SDL_Surface* surface, int offx, int offy, int theWidth, int theHeight)
{
    if (surface == NULL)
        return;

    int aWidth = std::min(theWidth, (mWidth - offx));
    int aHeight = std::min(theHeight, (mHeight - offy));

    bool rightPad = aWidth<theWidth;
    bool bottomPad = aHeight<theHeight;

    if (aWidth > 0 && aHeight > 0) {
        CopyImageToSurface8888((Uint32*) surface->pixels, surface->pitch, offx, offy, aWidth, aHeight, rightPad);
    }

    if (bottomPad) {
        uchar *dstrow = ((uchar*) surface->pixels) + surface->pitch*aHeight;
        memcpy(dstrow, dstrow - surface->pitch, surface->pitch);
    }
}

#if defined(USE_GL_RGBA)
static inline Uint32 convert_ARBG_to_ABGR(Uint32 color)
{
    Uint32 r = color & 0xFF0000;
    Uint32 b = color & 0x0000FF;
    return (color & ~0xFF00FF) | (b << 16) | (r >> 16);
}
#endif

MemoryImage* MemoryImage::CreateImageFrom(int offx, int offy, int theWidth, int theHeight) {
    MemoryImage* image = new MemoryImage();
    image->Create(theWidth, theHeight);
    image->Clear();

    if (mColorTable == NULL) {
        uint32_t *srcRow = GetBits() + offy * mWidth + offx;
        uint32_t *dstRow = (uint32_t*) image->GetBits();

        for (int y = 0; y < theHeight; y++) {
            uint32_t *src = srcRow;
            uint32_t *dst = dstRow + y * theWidth;

            for (int x = 0; x < theWidth; x++) {
                *dst++ = *src++;
            }
            srcRow += mWidth;
        }
    }
    else {  //palette

        //FIXME not tested!
        uchar *srcRow = (uchar*) mColorIndices + offy * mWidth + offx;
        uchar *dstRow = (uchar*) image->mColorIndices;
        image->mColorTable = mColorTable;

        for (int y = 0; y < theHeight; y++) {
            uchar *src = srcRow;
            uchar *dst = dstRow + y * theWidth;

            for (int x = 0; x < theWidth; x++)
                *dst++ = *src++;
            srcRow += mWidth;
        }
    }

    return image;
}

void MemoryImage::CopyImageToSurface8888(void *theDest, Uint32 theDestPitch, int offx, int offy, int theWidth, int theHeight, bool rightPad)
{
    // The IF and ELSE part are identical except that one reads the pixels directly
    // and the other uses the colortable.
    if (mColorTable == NULL) {
        uint32_t *srcRow = GetBits() + offy * mWidth + offx;
        char *dstRow = (char*) theDest;

        for (int y = 0; y < theHeight; y++) {
            uint32_t *src = srcRow;
            uint32_t *dst = (uint32_t*) dstRow;

            for (int x = 0; x < theWidth; x++) {
#if defined(USE_GL_RGBA)
                *dst++ = convert_ARBG_to_ABGR(*src++);
#else
                *dst++ = *src++;
#endif
            }

            if (rightPad)
                *dst = *(dst - 1);      // Copy the last pixel. Huh? Only one?

            srcRow += mWidth;
            dstRow += theDestPitch;
        }
    } else // palette
    {
        uchar *srcRow = (uchar*) mColorIndices + offy * mWidth + offx;
        uchar *dstRow = (uchar*) theDest;
        uint32_t *palette = mColorTable;

        for (int y = 0; y < theHeight; y++) {
            uchar *src = srcRow;
            uint32_t *dst = (uint32_t*) dstRow;

            for (int x = 0; x < theWidth; x++)
#if defined(USE_GL_RGBA)
                *dst++ = convert_ARBG_to_ABGR(palette[*src++]);
#else
                *dst++ = palette[*src++];
#endif

            if (rightPad)
                *dst = *(dst - 1);

            srcRow += mWidth;
            dstRow += theDestPitch;
        }
    }
}

void MemoryImage::SaveImage(ITYPE type, const std::string& filename, const std::string& path)  {
    SDL_Surface* surface;

    // TODO. It is awfully ugly that we need USE_GL_RBGA to create a surface, only because CopyImageToSurface
    // has a built in knowledge about it.
#ifdef USE_GL_RGBA
    // Attention. We use the Uint32 different, namely: ABGR
    const Uint32 SDL_amask = 0xFF000000;
    const Uint32 SDL_bmask = 0x00FF0000;
    const Uint32 SDL_gmask = 0x0000FF00;
    const Uint32 SDL_rmask = 0x000000FF;
#else
    // Keep the RGB fields the same as in the rest of TuxCap
    const Uint32 SDL_amask = 0xFF000000;
    const Uint32 SDL_rmask = 0x00FF0000;
    const Uint32 SDL_gmask = 0x0000FF00;
    const Uint32 SDL_bmask = 0x000000FF;
#endif
    surface = SDL_CreateRGBSurface(
                                   SDL_SWSURFACE,
                                   GetWidth(),
                                   GetHeight(),
                                   32,
                                   SDL_rmask,
                                   SDL_gmask,
                                   SDL_bmask,
                                   SDL_amask
                                   );
    assert(surface != NULL);

    // This copies a square from the image into our little surface here.
    // This maintains the Uint32 ARGB format. LE Byte stream is then BGRA.
    CopyImageToSurface(surface, 0, 0, GetWidth(), GetHeight());

    switch(type) {
    case ITYPE_BMP:
        SDL_SaveBMP(surface, (path + "/" + filename).c_str());
        break;
#if TARGET_OS_IPHONE == 0
    case ITYPE_PNG:
        IMG_SavePNG(static_cast<const char*>((path + "/" + filename).c_str()), surface, 7);
        break;
#endif
    default:
        break;
    }
    SDL_FreeSurface(surface);
}

void MemoryImage::SaveImageToBMP(const std::string& filename, const std::string& path) 
{
    SaveImage(ITYPE_BMP, filename + ".bmp", path);
}

void MemoryImage::SaveImageToPNG(const std::string& filename, const std::string& path) 
{
    SaveImage(ITYPE_PNG, filename + ".png", path);
}

/* original taken from a post by Sam Lantinga, thanks Sam for this and for SDL :-)*/
GLuint MemoryImage::CreateTexture(int x, int y, int w, int h)
{
    static SDL_Surface *image = NULL;

    if (image != NULL) {
        if (image->w != w || image->h != h) {

            SDL_FreeSurface(image);
            image = NULL;
        }
    }

    // From the SDL doc:
    //   The [RGBA]mask parameters are the bitmasks used to extract that color
    //   from a pixel. For instance, Rmask being FF000000 means the red data is
    //   stored in the most significant byte. Using zeros for the RGB masks sets
    //   a default value, based on the depth.
    //   (e.g. SDL_CreateRGBSurface(0,w,h,32,0,0,0,0);) However, using zero for
    //   the Amask results in an Amask of 0.

    // In other words, the masks are used to EXTRACT the color from a pixel.
    // Not sure what the source is in that context.


    /* Create an OpenGL texture for the image */
    GLuint texture;
    glGenTextures(1, &texture);
    GLState::getInstance()->bindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 1);

#if SDL_VERSION_ATLEAST(2,0,0)
    // In the pre SDL2 era we used to call SDL_CreateRGBSurface(SDL_HWSURFACE, ...
    // But in SDL2 a hardware surface is a SDL_Texture. Please be aware that
    // the SDL_HWSURFACE flag has been removed.
#endif

#if defined(USE_GL_RGBA)
    if (image == NULL) {
        // Attention. We use the Uint32 different, namely: ABGR
        const Uint32 SDL_amask = 0xFF000000;
        const Uint32 SDL_bmask = 0x00FF0000;
        const Uint32 SDL_gmask = 0x0000FF00;
        const Uint32 SDL_rmask = 0x000000FF;
        image = SDL_CreateRGBSurface(
                0, // Pre SDL2 had this: SDL_HWSURFACE,
                w,
                h,
                32,
                SDL_rmask,
                SDL_gmask,
                SDL_bmask,
                SDL_amask
                );
        assert(image != NULL);
    }
    SDL_FillRect(image, NULL, SDL_MapRGBA(image->format, 0,0,0,0));

    // This copies a square from the image into our little surface here.
    // OpenGLES only has RGBA, so we need to convert the surface data.
    CopyImageToSurface(image, x, y, w, h);

    glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_RGBA,                    // We could also use 4. Its probably doesn't matter much.
            w, h,
            0,
            GL_RGBA,                    // OpenGLES only has RGBA
            GL_UNSIGNED_BYTE,
            image->pixels);
#else
    if (image == NULL) {
        // Keep the RGB fields the same as in the rest of TuxCap
        const Uint32 SDL_amask = 0xFF000000;
        const Uint32 SDL_rmask = 0x00FF0000;
        const Uint32 SDL_gmask = 0x0000FF00;
        const Uint32 SDL_bmask = 0x000000FF;
        image = SDL_CreateRGBSurface(
                0, // Pre SDL2 had this: SDL_HWSURFACE,
                w,
                h,
                32,
                SDL_rmask,
                SDL_gmask,
                SDL_bmask,
                SDL_amask
                );
        assert(image != NULL);
    }
    SDL_FillRect(image, NULL, SDL_MapRGBA(image->format, 0,0,0,0));

    // This copies a square from the image into our little surface here.
    // This maintains the Uint32 ARGB format. LE Byte stream is then BGRA.
    CopyImageToSurface(image, x, y, w, h);

    glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_RGBA,                    // We could also use 4. Its probably doesn't matter much.
            w, h,
            0,
            GL_BGRA,                    // SDL_image reads images as BGRA.
            GL_UNSIGNED_BYTE,
            image->pixels);
#endif
    return texture;
}
