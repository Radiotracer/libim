/*
	image.h

	$Id: image.h 55 2007-05-09 14:16:02Z mjs $
*/

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Program:  IMAGE.H                                                         */
/*                                                                           */
/* Purpose:  This file contains the constant and type definitions for        */
/*           the routines defined in image.c.                                */
/*                                                                           */
/* Author:   John Gauch - Version 2                                          */
/*           Zimmerman, Entenman, Fitzpatrick, Whang - Version 1             */
/*                                                                           */
/* Date:     February 23, 1987                                               */
/*                                                                           */
/*---------------------------------------------------------------------------*/

#ifndef IMAGE_H
#define IMAGE_H

#ifdef WIN32
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <stdio.h>
#else
#ifdef SYSV
#include <sys/types.h>
#ifndef __FCNTL_HEADER__	/* Because system V does not do this */
#define __FCNTL_HEADER__	/* 	in fcntl.h		     */
#include <fcntl.h>
#endif /*!__FCNTL_HEADER__*/
#endif

#ifdef SYSV
#ifndef __FILE_HEADER__		/* Because system V does not do this */
#define __FILE_HEADER__		/* 	in file.h		     */
#include <sys/file.h>
#endif /*!__FILE_HEADER__*/
#else
#include <sys/file.h>
#endif
#endif

/* Boolean values */
#define TRUE		1
#define FALSE		0

/* Routine return codes */
#define VALID		1
#define INVALID		0

/* Maximum and minimum GREYTYPE values */
#define MINVAL		-32768
#define MAXVAL		32767

/* Pixel format codes */
#define GREY		0010
#define COLOR		0020
#define COLORPACKED	0040
#define USERPACKED	0200
#define BYTE		0001
#define SHORT		0002
#define LONG		0003
#define REAL		0004
#define COMPLEX		0005
#define INT             0006

/* Pixel format types */
typedef short GREYTYPE;
typedef short COLORTYPE;
typedef struct { unsigned char r,g,b,a; } CPACKEDTYPE;
typedef int USERTYPE;
typedef unsigned char BYTETYPE;
typedef short SHORTTYPE;
typedef long LONGTYPE;
typedef float REALTYPE;
typedef struct { float re, im; } COMPLEXTYPE;

/* Constants for open calls */
#define READ		(O_RDONLY)
#define UPDATE		(O_RDWR)
#define CREATE		(O_RDWR | O_CREAT | O_EXCL)

/* compression constants */
#define COMPRESS		16384
#define FORCE_COMPRESS		32768
#define FORCE_DECOMPRESS	65536

/* currently support up to 10 compression methods */
#define MAX_NUM_COMP_METHODS 10
 
/* Constants for imgetdesc calls */
#define MINMAX		0
#define HISTO		1

/* Protection modes for imcreat */
#define UOWNER		0600
#define UGROUP		0060
#define RGROUP		0040
#define UOTHER		0006
#define ROTHER		0004
#define DEFAULT		0644

/* Array length constants */
#define nADDRESS	9
#define nTITLE		81
#define nMAXMIN		2
#define nHISTOGRAM	1024
#define nDIMV		10
#define nERROR		200
#define nINFO		100

/* Old names for array length constants (from version 1) */
#define _NDIMS		nDIMV
#define _ERRLEN		nERROR
#define TITLESIZE	nTITLE
#define MAXPIX		(nHISTOGRAM-1)

/* Structure for image information (everything but pixels) */
typedef struct {
   int   Fd;			/* Computed fields */
   int   PixelSize;
   int   PixelCnt;
   int   SwapNeeded;

   int	 Compressed;		/* is pixel data compressed? */
   int	 CompressionMethod;	/* how is pixel data compressed? */
   int	 PixelsAccessed;	/* have the pixels been accessed yet? */
   int	 PixelsModified;	/* have the pixels been modified yet? */
   int	 UCPixelsFd;		/* where is the uncompressed data? */
   char	 UCPixelsFileName[256];	/* name of the uncompressed data file */

   int   Address[nADDRESS];	/* Header fields from file */
   char  Title[nTITLE];
   int   ValidMaxMin;
   int   MaxMin[nMAXMIN];
   int   ValidHistogram;
   int   Histogram[nHISTOGRAM];
   int   PixelFormat;
   int   Dimc;
   int   Dimv[nDIMV];

   int   InfoCnt;		/* Information fields from file */
   char *InfoName[nINFO];
   char *InfoData[nINFO];

   int   nImgFormat;

   } IMAGE;

/* compression structure */
typedef struct
{
  char methodName[80];
  char compressionCommand[80];
  char decompressionCommand[80];
} compMethod;

/* Function Declarations */

#ifdef __cplusplus
extern "C" {
#endif

IMAGE *imcreat(char *Name, int Protection, int PixForm, int Dimc, int *Dimv);
IMAGE *dcmopen(char *Name, int Mode);
int GetIFElement(char *buffer, char **strName, char **strValue);
IMAGE *ifopen(char *Name, int Mode);
IMAGE *imopen(char *ImName, int Mode);
int imclose(IMAGE *Image);
int imcloseC(IMAGE *Image);
int imcloseU(IMAGE *Image);
int Swap(char *Buffer, int Length, int Type);
int Swapheader(IMAGE *Image);
int readCompressionConfigFile(void);
int fillInCompressionCommand(char *specific, char *generic, char *infile, char *outfile, IMAGE *Image);
int compressImage(IMAGE *Image);
int decompressImage(IMAGE *Image);
int imread(IMAGE *Image, int LoIndex, int HiIndex, GREYTYPE *Buffer);
int imwrite(IMAGE *Image, int LoIndex, int HiIndex, GREYTYPE *Buffer);
int imgetpix(IMAGE *Image, int Endpts[][2], int Coarseness[], GREYTYPE *Pixels);
int imputpix(IMAGE *Image, int Endpts[][2], int Coarseness[], GREYTYPE *Pixels);
int GetPut2D(IMAGE *Image, int Endpts[][2], GREYTYPE *Pixels, int Mode);
int GetPut3D(IMAGE *Image, int Endpts[][2], GREYTYPE *Pixels, int Mode);
int GetPutND(IMAGE *Image, int Endpts[][2], GREYTYPE *Pixels, int Mode);
int imheader(IMAGE *Image, int *PixFormat, int *PixSize, int *PixCnt, int *Dimc, int *Dimv, int *MaxMin);
int imheaderC(IMAGE *Image, int *PixFormat, int *PixSize, int *PixCnt, int *Dimc, int *Dimv, int *MaxMin, int *Compressed, int *CompMethod, float *CompRatio);
int imgetcompinfo(IMAGE *Image, int *Compressed, int *CompMethod, float *CompRatio);
int imdim(IMAGE *Image, int *PixFormat, int *Dimc);
int imbounds(IMAGE *Image, int *Dimv);
int imgetdesc(IMAGE *Image, int Type, int Buffer[]);
int imtest(IMAGE *Image, int Type);
int imgettitle(IMAGE *Image, char *Title);
int imputtitle(IMAGE *Image, char *Title);
char *imgetinfo(IMAGE *Image, char *Name);
int imputinfo(IMAGE *Image, char *Name, char *Data);
int imcopyinfo(IMAGE *Image1, IMAGE *Image2);
char **iminfoids(IMAGE *Image);
char *imerror(void);
int im_snap(int xdim, int ydim, int pixformat, char *name, char *newtitle, char *pixel);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* File:     PDIM.H                                                          */
/*                                                                           */
/* Purpose:  This file contains declarations used by PDIM.C                  */
/*                                                                           */
/* Author:   John Gauch - Version 2                                          */
/*           Chuck Mosher - Version 1                                        */
/*                                                                           */
/* Date:     July 21, 1986                                                   */
/*                                                                           */
/*---------------------------------------------------------------------------*/

/* Global PDIM constants */
#define REC_SIZE 200

/* Types of units possible */
#define MILLIMETER 0
#define CENTIMETER 1

/* Structure for single slice description */
typedef struct {
   float Ox,Oy,Oz;
   float Ux,Uy,Uz;
   float Vx,Vy,Vz;
   float time;
   int   number;
   } SLICEREC;

/* Structure for whole PDIM description */
typedef struct {
   int version;
   int units;
   int machine;
   int slicecnt;
   SLICEREC *patient;
   SLICEREC *table;
   } PDIMREC;
 
/* Function declarations */

#ifdef __cplusplus
extern "C" {
#endif

int pdim_read(IMAGE *image, PDIMREC *pdiminfo);
int pdim_write(IMAGE *image, PDIMREC *pdiminfo);
int pdim_free(PDIMREC *pdiminfo);
int pdim_append(PDIMREC *pdiminfo1, PDIMREC *pdiminfo2);
int pdim_window(PDIMREC *pdiminfo, char dimension, int low, int high);
int pdim_scale(PDIMREC *pdiminfo, char field, char dimension, float scale, int low, int high);
int pdim_rotate(PDIMREC *pdiminfo, char field, char dimension, float angle, int low, int high);
int pdim_translate(PDIMREC *pdiminfo, char field, char dimension, float dist, int low, int high);
int pdim_map(PDIMREC *pdiminfo, char field, int u, int v, int w, float *x, float *y, float *z, float *t);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

/* Fix up linking for the FFT library */
#ifdef IBM
#define cmplft_ cmplft
#endif

/* Fix up linking of FFT library for stardent 3000*/
#ifdef ARDENT
#define hermft_	HERMFT
#define realft_	REALFT
#define rsymft_	RSYMFT
#define sdiad_	SDIAD
#define inv21_	INV21
#define cmplft_	CMPLFT
#define srfp_	SRFP
#define diprp_	DIPRP
#define mdftkd_	MDFTKD
#define r2cftk_	R2CFTK
#define r3cftk_	R3CFTK
#define r4cftk_	R4CFTK
#define r5cftk_	R5CFTK
#define r8cftk_	R8CFTK
#define rpcftk_	RPCFTK
#endif

#endif /* IMAGE_H */
