/*#define STANDALONE */
/*#define DICOM_DEBUG*/
/*
	image.c
	
	$Id: image.c 84 2014-01-10 22:03:56Z frey $
*/

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Program:  IMAGE.C                                                         */
/*                                                                           */
/* Purpose:  This is version 2 of the image processing library.  It is       */
/*           intended to provide a simple interface for reading and          */
/*           writing images created by version 1 of the library.  This       */
/*           collection of routines differs from the original routines       */
/*           in the following ways:                                          */
/*                                                                           */
/*           1)  New types of images are handled.  The set now includes      */
/*               GREY, COLOR, COLORPACKED, BYTE, SHORT, LONG, REAL,          */
/*               COMPLEX and USERPACKED.                                     */
/*                                                                           */
/*           2)  Faster access to pixels is provided.  Special routines      */
/*               are included which are optimal for reading/writing 1D,      */
/*               2D and 3D images.  The N dimensional pixel access routine   */
/*               has also been improved.                                     */
/*                                                                           */
/*           3)  Three new routines imread, imwrite and imheader.            */
/*               See the iman entries for a full description of these        */
/*               new routines.                                               */
/*                                                                           */
/* Contains: imcreat            - Image initialization routines              */
/*           imopen                                                          */
/*           imclose                                                         */
/*                                                                           */
/*           imread             - Pixel access routines                      */
/*           imwrite                                                         */
/*           imgetpix                                                        */
/*           imputpix                                                        */
/*           GetPut2D                                                        */
/*           GetPut3D                                                        */
/*           GetPutND                                                        */
/*                                                                           */
/*           imheader           - Information access routines                */
/*           imdim                                                           */
/*           imbounds                                                        */
/*           imgetdesc                                                       */
/*           imtest                                                         */
/*           imgettitle                                                      */
/*           imputtitle                                                      */
/*           imgetinfo                                                       */
/*           imputinfo                                                       */
/*           imcopyinfo                                                      */
/*           iminfoids                                                       */
/*           imerror                                                         */
/*           im_snap                                                         */
/*                                                                           */
/* Author:   John Gauch - Version 2                                          */
/*           Zimmerman, Entenman, Fitzpatrick, Whang - Version 1             */
/*                                                                           */
/* Date:     February 23, 1987                                               */
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Revised:  October 29, 1987 - Fixed one problem with the way histograms    */
/*           are calculated in imgetdesc.  Changes marked with JG1.          */
/*                                                                           */
/* Revised:  June 2, 1989 - Added imtest.  A. G. Gash.			     */
/*	     Sep 20, 1989 - Added im_snap.  A. G. Gash.			     */
/*	     Feb 12, 1991 - Generalized im_snap to work with any type.	     */
/*		 A. G. Gash.						     */
/*           Apr 19, 1991 - Replaced the sizes of the different datatypes    */
/*                          by sizeof calls.  Andre S.E. Koster,	     */
/*                          3D Computer Vision, The Netherlands,	     */
/*           Feb. 1, 1992 - Can automatically swap the byte order of images  */
/*                          across different architectures.   Zhengwen Ju    */
/*           May 3, 1995 -  Allow imcreat() to overwrite existing images if  */
/*                          IMAGE_CLOBBER env. variable set                  */
/*                                                         - Jason Priebe    */
/*           May 5, 1995 -  Support for compressed images.  Added:           */
/*                             imheaderC() -- to obtain header information   */
/*                                            including compression info     */
/*                                                         - Jason Priebe    */
/*                                                                           */
/* Revised:  March 9, 2003 - Added dcmopen/ifopen. Bin He 					 */
/*                                                                           */
/*---------------------------------------------------------------------------*/

#ifndef WIN32
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef WIN32
#pragma warning( disable : 4996 )
#pragma warning( disable : 4313 )
#pragma warning( disable : 4267 )
#include <io.h>
#define ftruncate _chsize
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseek
#define unlink _unlink
#endif

#ifdef WIN32
int strcasecmp(const char * s1, const char * s2)
{
	return stricmp(s1, s2);
}
#endif

#include "image.h"

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  These declarations are private to this library.                 */
/*           These MACROs copy an error message into the error string.       */
/*                                                                           */
/*---------------------------------------------------------------------------*/
/* Constants for lseek calls */
#define FROMBEG		0
#define FROMHERE	1
#define FROMEND		2

/* Address index constants */
#define aMAXMIN		0
#define aHISTO		1
#define aTITLE		2
#define aPIXFORM	3
#define aDIMC		4
#define aDIMV		5
#define aPIXELS		6
#define aINFO		7
#define aVERNO		8

/* compression flags */
#define COMPRESSED	65536

/* a few globals used for compression routines */
compMethod compressionMethods[MAX_NUM_COMP_METHODS];
int haveNotReadCompressionConfigFile = TRUE;
char *tempDir;
int NumberOfCompressionMethods = 0;

#ifndef COMPRESSION_TYPE_FILE
#define NO_COMPRESSION
#endif

/* Modes for GetPut routines */
#define READMODE	0
#define WRITEMODE	1

/* Blocking factor for imgetdesc */
#define MAXGET 4096

/* Error string buffer */
static char _imerrbuf[nERROR];

#define Error(Mesg)\
   {\
   strcpy(_imerrbuf, Mesg);\
   return(INVALID);\
   }

#define ErrorNull(Mesg)\
   {\
   strcpy(_imerrbuf, Mesg);\
   return(NULL);\
   }

#define Warn(Mesg)\
   {\
   strcpy(_imerrbuf, Mesg);\
   }

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine creates an image.  The user specified image        */
/*           parameters are stored in the new image record.  For historical  */
/*           reasons, 4092 bytes are unused between the end of the           */
/*           histogram and the start of the pixel format field - gauchj.     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
#define UNUSED 4092
IMAGE *imcreat (char *Name, int Protection, int PixForm, int Dimc, int *Dimv)
{
	IMAGE *Image;
	int Cnt;
	int Fd;
	int i;
	char Null = '\0';
#ifndef NO_COMPRESSION
	char *envVar;
#endif

	/* Check parameters */
	if (Name == NULL) ErrorNull("Null image name");
	if ((PixForm != GREY) && (PixForm != COLOR) && (PixForm != COLORPACKED) && 
		(PixForm != BYTE) && (PixForm != SHORT) && (PixForm != LONG) && 
		(PixForm != REAL) && (PixForm != COMPLEX) && (PixForm != USERPACKED)) 
		ErrorNull("Invalid pixel format");
	if ((Dimc < 1) || (Dimc > nDIMV)) ErrorNull("Illegal number of dimensions");


	/* Check to see if the environment variable IMAGE_CLOBBER is set.  If it
		 is set, then allow user to create an image on top of an existing 
		 image file */
	if(getenv("IMAGE_CLOBBER") != NULL)
	{
		/* Create image file, modifying the mode flags to remove the  */
		/* O_EXCL flag and add the O_TRUNC flag                       */
		Fd = open(Name,
			(CREATE - (CREATE & O_EXCL) - (CREATE & O_TRUNC) + O_TRUNC),
			Protection);
	}
	else
	{
		/* Create image file */
#ifdef WIN32
		Fd = open(Name,CREATE|O_BINARY,Protection);
#else
		Fd = open(Name,CREATE,Protection);
#endif
		if (Fd == EOF) ErrorNull("Image already exists");
	}

	/* Allocate image record */
	Image = (IMAGE *)malloc((unsigned)sizeof(IMAGE));
	if (Image == NULL) ErrorNull("Allocation error");

	/* Initialize image record */   
	Image->Title[0] = Null;
	Image->ValidMaxMin = FALSE;
	Image->MaxMin[0] = MAXVAL;
	Image->MaxMin[1] = MINVAL;
	Image->ValidHistogram = FALSE;
	for (i=0; i<nHISTOGRAM; i++)
		Image->Histogram[i] = 0;
	Image->PixelFormat = PixForm;
	Image->Dimc = Dimc;

	/* Determine number of pixels in image */
	Image->PixelCnt = 1;
	for (i=0; i<Image->Dimc; i++) {
		Image->Dimv[i] = Dimv[i];
		Image->PixelCnt = Image->PixelCnt * Dimv[i];
	}

	/* Determine size of each pixel */
	switch (Image->PixelFormat) {
		case GREY         : Image->PixelSize = sizeof (GREYTYPE); break;
		case COLOR        : Image->PixelSize = sizeof (COLORTYPE); break;
		case COLORPACKED  : Image->PixelSize = sizeof (CPACKEDTYPE); break;
		case BYTE         : Image->PixelSize = sizeof (BYTETYPE); break;
		case SHORT        : Image->PixelSize = sizeof (SHORTTYPE); break;
		case LONG         : Image->PixelSize = sizeof (LONGTYPE); break;
		case REAL         : Image->PixelSize = sizeof (REALTYPE); break;
		case COMPLEX      : Image->PixelSize = sizeof (COMPLEXTYPE); break;
		case USERPACKED   : Image->PixelSize = sizeof (USERTYPE); break;
	}

	/* Initialize image addresses (do NOT change this) */
	Image->Address[aTITLE] = sizeof( Image->Address );
	Image->Address[aMAXMIN] = Image->Address[aTITLE] 
		+ sizeof( Image->Title );
	Image->Address[aHISTO] = Image->Address[aMAXMIN] 
		+ sizeof( Image->ValidMaxMin ) + sizeof( Image->MaxMin );
	Image->Address[aPIXFORM] = Image->Address[aHISTO] + UNUSED
		+ sizeof( Image->ValidHistogram ) + sizeof( Image->Histogram );
	Image->Address[aDIMC] = Image->Address[aPIXFORM] 
		+ sizeof( Image->PixelFormat );
	Image->Address[aDIMV] = Image->Address[aDIMC] 
		+ sizeof( Image->Dimc );
	Image->Address[aPIXELS] = Image->Address[aDIMV] 
		+ sizeof( Image->Dimv );
	Image->Address[aINFO] = Image->Address[aPIXELS] 
		+ Image->PixelCnt * Image->PixelSize;
	Image->Address[aVERNO] = 1;

	/* Save file pointer */
	/* this must be done before we try to compress the image */
	Image->Fd = Fd;

	Image->Compressed = FALSE;
	
#ifndef NO_COMPRESSION
	/* check for COMPRESS flag */
	if((envVar = getenv("IMAGE_COMPRESS")) != NULL)
	{
		Image->Compressed = TRUE;
		/* find the specified compression method; if it can't be determined,
			 it defaults to 0 */
		if(sscanf(envVar,"%d", &(Image->CompressionMethod)) != 1)
			Image->CompressionMethod = 0;
		if(haveNotReadCompressionConfigFile) readCompressionConfigFile();

		/* this step assumes that NumberOfCompressionMethods > 0 */
		if(Image->CompressionMethod >= NumberOfCompressionMethods)
		{
			char message[256];
			sprintf(message, "Compression method %d out of bounds (only %d methods defined).\nDefaulting to method 0",
				Image->CompressionMethod, NumberOfCompressionMethods);
			Warn(message);
			Image->CompressionMethod = 0;
		}

		Image->PixelsAccessed = TRUE;
		Image->PixelsModified = FALSE;
		Image->Address[aVERNO] =
			Image->Address[aVERNO] | COMPRESSED | Image->CompressionMethod * 4096;

		/* write some bogus data to the uncompressed pixel file; compress it */
		if((tempDir = getenv("IMAGE_TEMPDIR")) == NULL)
			tempDir = "/usr/tmp";
		sprintf(Image->UCPixelsFileName, "%s/tempimXXXXXX", tempDir);
		mkstemp(Image->UCPixelsFileName);
		if((Image->UCPixelsFd = open(Image->UCPixelsFileName,
		      O_RDWR | O_CREAT | O_TRUNC,
		      DEFAULT)) == -1) Error("Could not open temp file");
		lseek(Image->UCPixelsFd, Image->PixelCnt * Image->PixelSize, FROMBEG);
		Null = '1';
		write(Image->UCPixelsFd, (char *)&Null, sizeof(Null));
		ftruncate(Image->UCPixelsFd,
			(off_t)(Image->PixelCnt * Image->PixelSize + 1));

		compressImage(Image);
	}else{
		Image->Compressed = FALSE;
	}
#endif
		
	/* Write null information field */
	Cnt = (int)lseek(Fd, (long)Image->Address[aINFO], FROMBEG);
	Null = '\0';
	Cnt = write(Fd, (char *)&Null, sizeof(Null));
	if (Cnt != sizeof(Null)) ErrorNull("Image write failed");
	Image->InfoCnt = 0;

	/* save the compression type as an info field */
	if(Image->Compressed)
		imputinfo(Image, "Pixel Compression Method", 
			compressionMethods[Image->CompressionMethod].methodName);


	/* Initialize swap flag */
	Image->SwapNeeded = FALSE;
	Image->nImgFormat = 0;
	return(Image);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine opens an image.  The image parameters are          */
/*           read from the file and stored in the image record.              */
/*                                                                           */
/*---------------------------------------------------------------------------*/

IMAGE *dcmopen(char * Name, int Mode)
{
	IMAGE *Image;
	int Fd;
	unsigned int    tag;
	unsigned int    length;
	int				i=0, bImplicit=0, bCompressed=0;
	char            strDICM[5] = "";
	char            strVR[3] = "";
	char            is_FNum[128] = "1";
	char			strTemp[128];
	unsigned short
		us = 0,
		us_Rows = 0,
		us_Cols = 0,
		us_Bits = 0,
		us_BSto = 0,
		us_HBit = 0,
		us_Samp = 0,
		us_PixR = 0,
		us_FNum = 1;
	int bFirstItemCheck = 0;

	/* Check parameters */
	if (Name == NULL) ErrorNull("Null image name");
	if ((Mode != READ) && (Mode != UPDATE)) ErrorNull("Invalid open mode");

	/* Open image file */
#ifdef WIN32
		Fd = open(Name,Mode|O_BINARY);
#else
	Fd = open(Name,Mode);
#endif
	if (Fd == EOF) ErrorNull("Image file not found");

	/* Allocate image record */
	Image = (IMAGE *)malloc((unsigned)sizeof(IMAGE));
	if (Image == NULL) ErrorNull("Allocation error");

	lseek(Fd, 0x80, FROMBEG);
	if ((read(Fd, strDICM, 4)) != 4)
	{
		close(Fd);
		free(Image);
		return NULL;
	}
	if (strcmp(strDICM, "DICM") != 0)
	{
		lseek(Fd, 0, FROMBEG);
		bImplicit = 1;
		bFirstItemCheck = 1;
#ifdef DICOM_DEBUG
		printf("Implicit\n");
#endif
	}

	i = 0;
	// Read TAG and VR
	while (read(Fd, &tag, sizeof(unsigned int)) == sizeof(unsigned int))
	{
		if (bFirstItemCheck && (((tag & 0xFFFF) < 2) || ((tag & 0xFFFF) > 8)))	// For implicit transfer systax, no "DICM" identity, need check if first element belongs to (0x0002, 0x0008] group
		{
#ifdef DICOM_DEBUG
			printf("failed first tag check for implicit file: %xd",tag);
#endif
			close(Fd);
			free(Image);
			return NULL;
		}
		else
			bFirstItemCheck = 0;

		// Implicit Transfer Syntax (except group 0002) or Element (FFFE,E000), (FFFE,E00D), (FFFE,E0DD) have no VR
		if ((bImplicit && ((tag & 0xFFFF) != 0x0002)) || (tag == 0xE000FFFE) || (tag == 0xE00DFFFE) || (tag == 0xE0DDFFFE))
		{
			read(Fd, &length, sizeof(unsigned int));
			if (length == -1) length = 0;
#ifdef DICOM_DEBUG
			printf("tag=%xd,implicit,%d\n",tag,length);
#endif
		}
		else
		{
			read(Fd, strVR, sizeof(unsigned short));
			if ((strcmp(strVR, "OB") == 0) | (strcmp(strVR, "OW") == 0) | 
				(strcmp(strVR, "SQ") == 0) | (strcmp(strVR, "UN") == 0) | 
				(strcmp(strVR, "OF") == 0) | (strcmp(strVR, "UT") == 0))
			{
				lseek(Fd, 2, FROMHERE);
				read(Fd, &length, sizeof(unsigned int));
			}
			else
			{
				read(Fd, &us, sizeof(unsigned short));
				length = (unsigned int) us;
			}
			// For unknown length SQ
			if (length == -1) length = 0;
#ifdef DICOM_DEBUG
			printf("tag=%xd,%s,%d,%d\n",tag,strVR,length);
#endif
		}

		switch (tag)
		{
			case 0x00100002: // Check if implict or compressed
				read(Fd, strTemp, length);
				if (strcmp(strTemp, "1.2.840.10008.1.2") == 0)
					bImplicit = 1;
				else if ((strcmp(strTemp, "1.2.840.10008.1.2.1") != 0) && (strcmp(strTemp, "1.2.840.10008.1.2.2") != 0))
					bCompressed = 1;
				break;
			case 0x00100028:
				i |= 1;
				read(Fd, &us_Rows, length);
				break;
			case 0x00110028:
				i |= 2;
				read(Fd, &us_Cols, length);
				break;
			case 0x01010028:
				i |= 4;
				read(Fd, &us_BSto, length);
				break;
			case 0x01000028:
				i |= 8;
				read(Fd, &us_Bits, length);
				break;
			case 0x00020028:
				i |= 16;
				read(Fd, &us_Samp, length);
				break;
			case 0x01020028:
				i |= 32;
				read(Fd, &us_HBit, length);
				break;
			case 0x01030028:
				i |= 64;
				read(Fd, &us_PixR, length);
				break;
			case 0x00080028:
				read(Fd, is_FNum, length);
				us_FNum = atoi(is_FNum);
				break;
			case 0x00107FE0:
				break;
			default:
				lseek(Fd, length, FROMHERE);
				break;
		}
		// Reach Pixels, break out
		if (tag == 0x00107FE0) break;
	}

	if ((i != 127) || (us_Samp != 1) || (bCompressed))
	{
		close(Fd);
		free(Image);
		return NULL;
	}

	// Check if Image size correct
	if (length != (unsigned int) us_Rows * us_Cols * us_Samp * us_Bits * us_FNum / 8)
	{
		close(Fd);
		free(Image);
		return NULL;
	}

	// Set class member variables
	Image->PixelSize = us_Bits / 8;
	if (us_Bits == 8)
		Image->PixelFormat = 0001;
	else if (us_Bits == 16)
		Image->PixelFormat = 0010;
  // What? GREY and SHORT are both typedef'd to short in image.h!
  //		Image->PixelFormat = (us_PixR == 1 ? 0010:0002);	// If us_PixR == 1, singed else unsigned
	else if (us_Bits == 32)
		Image->PixelFormat = 0004;
	else
	{
		close(Fd);
		free(Image);
		return NULL;
	}
	Image->Address[aPIXELS] = lseek(Fd, 0, FROMHERE);

	Image->Dimc = 3;
	Image->Dimv[0] = us_FNum;
	Image->Dimv[1] = us_Rows;
	Image->Dimv[2] = us_Cols;
	Image->PixelCnt = us_FNum * us_Rows * us_Cols;
	Image->InfoCnt = 0;
	Image->Fd = Fd;
	Image->nImgFormat = 1;
	Image->Compressed = FALSE;
  Image->SwapNeeded = FALSE;

	return Image;
}

/*---------------------------------------------------------------------------*/
// Interperate interfile element, return name and value pointer
// (For Name field, remove all space, !, LF and CR, and change to low case)
/*---------------------------------------------------------------------------*/
int GetIFElement(char *buffer, char ** strName, char ** strValue)
{
    char *ch1 = buffer;
    char *ch2 = buffer;
    *strName  = buffer;

	for (; *ch2 != 0 && ((*ch2 != ':') || (*(ch2+1) != '=')); ch2++)
	{
		// Ignore !, space, LF and CR
		if ((*ch2 == '!') || (*ch2 == ' ') || (*ch2 == 10)  || (*ch2 == 13))
			continue;
		//change to low case
		if ((*ch2 >= 'A') && (*ch2 <= 'Z')) *ch2 += 32;
		*(ch1++) = *ch2;
    }

	if (*ch2 == 0) // No :=
		return 0;
	
	*(ch1++) = 0;
	ch2+=2;
	
	// Remove Value leading space
	while (*ch2 == ' ')
		ch2++;
	*strValue = ch2;
	
	// Remove LF and CR
	while ((*ch2 != 0) && (*ch2 != 10) && (*ch2 != 13))
		ch2++;
	*ch2 = 0;
	
	// Remove string padding space
	//strtrimr(*strValue);
    return 1;
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine opens an Interfile heder. The image parameters are */
/*           read from the file and stored in the image record.              */
/*                                                                           */
/*---------------------------------------------------------------------------*/
IMAGE *ifopen(char * Name, int Mode)
{
	IMAGE *Image;
	int Fd;
	int i, nRows, nCols, nFNum;
	FILE *fp;
	char buffer[255];
	char strPixelFormat[20];
	char *ch, *strIFName, *strIFValue;
	char strName[256], strDataFile[256];

	/* Check parameters */
	if (Name == NULL) ErrorNull("Null image name");
	if ((Mode != READ) && (Mode != UPDATE)) ErrorNull("Invalid open mode");

	/* Allocate image record */
	Image = (IMAGE *)malloc((unsigned)sizeof(IMAGE));
	if (Image == NULL) ErrorNull("Allocation error");

	// Open image header file
	if ((fp = fopen(Name,"r"))==NULL)
		ErrorNull("Image file not found");

	if ((fgets(buffer, sizeof(buffer), fp)) == NULL)
	{
		free(Image);
		fclose(fp);
		return NULL;
	}

	// Check if Interfile Header
	GetIFElement(buffer, &strIFName, &strIFValue);
	if (strcmp(buffer, "interfile") != 0)
	{
		free(Image);
		fclose(fp);
		return NULL;
	}

	//
	i = 0;
	nFNum = -1;
	while ((fgets(buffer, sizeof(buffer), fp)) != NULL)
	{
		if (!GetIFElement(buffer, &strIFName, &strIFValue))
			continue;
		//printf("Name=%s; Value=%s\n", strIFName,strIFValue);
		
		if (strcmp(strIFName, "nameofdatafile") == 0)
		{
			// Get image data file full path (assume it is in the same dir as header file)
			strcpy(strDataFile, strName);
#ifdef WIN32
			ch = strrchr(strDataFile, '\\');
#else
			ch = strrchr(strDataFile, '/');
#endif
			if (ch != NULL)
			{
				ch[1] = 0;
				strcat(strDataFile, strIFValue);
			}
			else
			{
				strcpy(strDataFile, strIFValue);
			}
			i|=1;
		}
		else if ((strcmp(strIFName, "dataoffsetinbytes") == 0) || (strcmp(strIFName, "dataoffsetinbytes[1]") == 0))  //"!data starting block"
		{
			Image->Address[aPIXELS] = atoi(strIFValue);
			i|=2;
		}
		else if (strcmp(strIFName, "matrixsize[1]") == 0)
		{
			nCols = atoi(strIFValue);
			i|=4;
		}
		else if (strcmp(strIFName, "matrixsize[2]") == 0)
		{
			nRows = atoi(strIFValue);
			i|=8;
		}
		else if (strcmp(strIFName, "totalnumberofimages") == 0)
		{
			if (nFNum == -1) nFNum = atoi(strIFValue);	// some files have both matrixsize[3] and totalnumberofimages, but only matrixsize[3] contains correct info
			i|=16;
		}
		else if (strcmp(strIFName, "matrixsize[3]") == 0) // in order to support STIR interfile
		{
			nFNum = atoi(strIFValue);
			i|=16;
		}
		else if (strcmp(strIFName, "numberformat") == 0)
		{
			strcpy(strPixelFormat, strIFValue);
			i|=32;
		}
		else if (strcmp(strIFName, "numberofbytesperpixel") == 0)
		{
			Image->PixelSize = atoi(strIFValue);
			i|=64;
		}
	}
	
	if ((i & 2) == 0)
	{
		// No dataoffsetinbytes, default it as 0
		Image->Address[aPIXELS] = 0;
		i|= 2;
	}

	if (i != 127)
	{
		free(Image);
		fclose(fp);
		return NULL;
	}

	if ((strcasecmp(strPixelFormat, "unsigned integer") != 0) &&
		(strcasecmp(strPixelFormat, "signed integer") != 0)  &&
		(strcasecmp(strPixelFormat, "float") != 0) &&
		(strcasecmp(strPixelFormat, "short float") != 0) )
	{
		free(Image);
		fclose(fp);
		return NULL;
	}

	if (Image->PixelSize == 2)
    {
        if (strcasecmp(strPixelFormat, "unsigned integer") == 0)
            Image->PixelFormat = 0002;	//short type (Unsigned Signed)
        else
            Image->PixelFormat = 0010;	//GREY type (Signed)
    }
	else if (Image->PixelSize == 4)
		Image->PixelFormat = 0004;

	Image->Dimc = 3;
	Image->Dimv[0] = nFNum;
	Image->Dimv[1] = nRows;
	Image->Dimv[2] = nCols;
	Image->PixelCnt = nFNum * nRows * nCols;

	// Get image data file full path (assume it is in the same dir as header file)
	if ( ((ch = strrchr(Name, '\\')) != NULL) || ((ch = strrchr(Name, '/')) != NULL) )
	{
		strcpy(buffer, strName);
		strcpy(strName, Name);
		strName[ch-Name+1] = 0;
		strcat(strName, buffer);
	}

	// Open image file
#ifdef WIN32
	if ((Fd = open(strName,Mode|O_BINARY))==EOF)
#else
	if ((Fd = open(strName,Mode))==EOF)
#endif
	{
		close(Fd);
		free(Image);
		return NULL;
	}
	fclose(fp);

	Image->Fd = Fd;
	Image->nImgFormat = 2;
	Image->Compressed = FALSE;
	return Image;
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine opens an image.  The image parameters are          */
/*           read from the file and stored in the image record.              */
/*                                                                           */
/*---------------------------------------------------------------------------*/
IMAGE *imopen (char *ImName, int Mode)
{
	IMAGE *Image;
	int Cnt;
	int Fd;
	int i;
	int Length;
	char *Buffer;
	char *StrPtr;
	char *Name;
	char *Data;

	// Try to open as DICOM file
	if ((Image = dcmopen(ImName, Mode)) != NULL)
		return Image;
#ifdef DICOM_DEBUG
	else
		printf("not dicom image\n");
#endif

	// Try to open as Interfile Header file
	if ((Image = ifopen(ImName, Mode)) != NULL)
		return Image;

	/* Check parameters */
	if (ImName == NULL) ErrorNull("Null image name");
	if ((Mode != READ) && (Mode != UPDATE)) ErrorNull("Invalid open mode");

	/* Open image file */
#ifdef WIN32	
	Fd = open(ImName,Mode|O_BINARY);
#else
	Fd = open(ImName,Mode);
#endif
	if (Fd == EOF) ErrorNull("Image file not found");

	/* Allocate image record */
	Image = (IMAGE *)malloc((unsigned)sizeof(IMAGE));
	if (Image == NULL) ErrorNull("Allocation error");

	/* Read addresses of image header fields */
	Cnt = read(Fd, (char *)&Image->Address[0], sizeof(Image->Address));
	if (Cnt != sizeof(Image->Address)) ErrorNull("Image read failed");
   
	/* this is a bitwise comparison b/c we are using some of the
		 bits in the Version int to indicate whether and what type of
		 compression was used in the image.  */
	if (!(Image->Address[aVERNO] & 1))
		/* Swap the byte order of each address */
	{
		Swap((char *)&Image->Address[0], sizeof(Image->Address), INT);
		Image->SwapNeeded = TRUE;
	}
	else
	{
		Image->SwapNeeded = FALSE;
	}
	
	/* is the image compressed? */
	if (Image->Address[aVERNO] & COMPRESSED)
	{
		Image->Compressed = TRUE;
		Image->CompressionMethod =
			(int)((Image->Address[aVERNO] - COMPRESSED) / 4096);
	}
	else
	{
		Image->Compressed = FALSE;
	}
	Image->PixelsAccessed = FALSE;
	Image->PixelsModified = FALSE;


	/* Read Title field */
	Cnt = (int)lseek(Fd, (long)Image->Address[aTITLE], FROMBEG);
	Cnt = read(Fd, (char *)&Image->Title[0], sizeof(Image->Title));
	if (Cnt != sizeof(Image->Title)) ErrorNull("Image read failed");

	/* Read MaxMin field */
	Cnt = (int)lseek(Fd, (long)Image->Address[aMAXMIN], FROMBEG);
	Cnt = read(Fd, (char *)&Image->ValidMaxMin, sizeof(Image->ValidMaxMin));
	if (Cnt != sizeof(Image->ValidMaxMin)) ErrorNull("Image read failed");
	Cnt = read(Fd, (char *)&Image->MaxMin[0], sizeof(Image->MaxMin));
	if (Cnt != sizeof(Image->MaxMin)) ErrorNull("Image read failed");

	/* Read Histogram field */
	Cnt = (int)lseek(Fd, (long)Image->Address[aHISTO], FROMBEG);
	Cnt = read(Fd, (char *)&Image->ValidHistogram,sizeof(Image->ValidHistogram));
	if (Cnt != sizeof(Image->ValidHistogram)) ErrorNull("Image read failed");
	Cnt = read(Fd, (char *)&Image->Histogram[0], sizeof(Image->Histogram));
	if (Cnt != sizeof(Image->Histogram)) ErrorNull("Image read failed");

	/* Read PixelFormat field */
	Cnt = (int)lseek(Fd, (long)Image->Address[aPIXFORM], FROMBEG);
	Cnt = read(Fd, (char *)&Image->PixelFormat, sizeof(Image->PixelFormat));
	if (Cnt != sizeof(Image->PixelFormat)) ErrorNull("Image read failed");

	/* Read DimC field */
	Cnt = (int)lseek(Fd, (long)Image->Address[aDIMC], FROMBEG);
	Cnt = read(Fd, (char *)&Image->Dimc, sizeof(Image->Dimc));
	if (Cnt != sizeof(Image->Dimc)) ErrorNull("Image read failed");

	/* Read DimV field */
	Cnt = (int)lseek(Fd, (long)Image->Address[aDIMV], FROMBEG);
	Cnt = read(Fd, (char *)&Image->Dimv[0], sizeof(Image->Dimv));
	if (Cnt != sizeof(Image->Dimv)) ErrorNull("Image read failed");
    
	/* Swap the byte order of header fields except the address and title */
	if (Image->SwapNeeded) Swapheader(Image);

	/* Determine number of pixels in image */
	Image->PixelCnt = 1;
	for (i=0; i<Image->Dimc; i++)
		Image->PixelCnt = Image->PixelCnt * Image->Dimv[i];

	/* Determine length of information field */
	Cnt = (int)lseek(Fd, (long)0, FROMEND);
	if (Cnt == -1) ErrorNull("Seek EOF failed");
	Length = Cnt - Image->Address[aINFO];

	/* Allocate buffer for information field */
	if (Length < 1) ErrorNull("Invalid information field");
	Buffer = (char *)malloc((unsigned)Length);
	if (Buffer == NULL) ErrorNull("Allocation error");

	/* Read whole information field into a buffer */
	Cnt = (int)lseek(Fd, (long)Image->Address[aINFO], FROMBEG);
	Cnt = read(Fd, (char *)Buffer, Length);
	if (Cnt != Length) ErrorNull("Image read failed");

	/* Prepare to loop through all fields */
	StrPtr = Buffer;
	Length = (int) strlen(StrPtr) + 1;
	Image->InfoCnt = 0;

	/* Loop through all fields */
	while ((Length > 1) && (Image->InfoCnt < nINFO))
	{
		/* Reading field name from buffer */
		Name = (char *)malloc((unsigned)Length);
		if (Name == NULL) ErrorNull("Allocation error");
		strcpy(Name, StrPtr);
		StrPtr = StrPtr + Length;
		Length = (int) strlen(StrPtr) + 1;

		/* Reading field data from buffer */
		Data = (char *)malloc((unsigned)Length);
		if (Data == NULL) ErrorNull("Allocation error");
		strcpy(Data, StrPtr);
		StrPtr = StrPtr + Length;
		Length = (int) strlen(StrPtr) + 1;

		/* Saving name and data in image header */
		Image->InfoName[Image->InfoCnt] = Name;
		Image->InfoData[Image->InfoCnt] = Data;
		Image->InfoCnt ++;
	}

	/* Free buffer used for information string */
	free(Buffer);

	/* Save file pointer */
	Image->Fd = Fd;

	/* Determine size of each pixel */
	switch (Image->PixelFormat) {
		case GREY         : Image->PixelSize = sizeof (GREYTYPE); break;
		case COLOR        : Image->PixelSize = sizeof (COLORTYPE); break;
		case COLORPACKED  : Image->PixelSize = sizeof (CPACKEDTYPE); break;
		case BYTE         : Image->PixelSize = sizeof (BYTETYPE); break;
		case SHORT        : Image->PixelSize = sizeof (SHORTTYPE); break;
		case LONG         : Image->PixelSize = sizeof (LONGTYPE); break;
		case REAL         : Image->PixelSize = sizeof (REALTYPE); break;
		case COMPLEX      : Image->PixelSize = sizeof (COMPLEXTYPE); break;
		case USERPACKED   : Image->PixelSize = sizeof (USERTYPE); break;
	}

	/* set flag indicating this is .im format */
	Image->nImgFormat=0;
	return(Image);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine closes an image.  The image parameters are         */
/*           written to the file from the image record.                      */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imclose (IMAGE *Image)
{
	int Fd;
	int Cnt;
	int Length;
	int InfoLength;
	char Null = '\0';
	int i;
#ifndef NO_COMPRESSION
	char* Buffer;
	char* envVar;
#endif
	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");

	/* Check that file is open */
	Fd = Image->Fd;
	if (Fd == EOF) Error("Image not open");

	if (Image->nImgFormat == 0)
	{
#ifndef NO_COMPRESSION
		/* if the image was opened as an uncompressed file, but the FORCE_COMPRESS
			 environment variable was set, then close it as a compressed file */
		if(!Image->Compressed && getenv("IMAGE_FORCE_COMPRESS"))
		{
			if((tempDir = getenv("IMAGE_TEMPDIR")) == NULL)
				tempDir = "/usr/tmp";
			sprintf(Image->UCPixelsFileName, "%s/tempimXXXXXX", tempDir);
			mkstemp(Image->UCPixelsFileName);
			if((Image->UCPixelsFd = open(Image->UCPixelsFileName,
						O_RDWR | O_CREAT | O_TRUNC,
						DEFAULT)) == -1) Error("Could not open temp file");

			Buffer = (char*)malloc(Image->PixelCnt * Image->PixelSize);

			/* read pixel data from image file */
			Cnt = (int)lseek(Image->Fd, (long)Image->Address[aPIXELS], FROMBEG);
			Cnt = read(Image->Fd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			if (Cnt != Image->PixelCnt*Image->PixelSize)
				Error("Uncompressed pixel read failed");

			/* write pixel data to temp file */
			Cnt = write(Image->UCPixelsFd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			if (Cnt != Image->PixelCnt * Image->PixelSize)
				Error("Uncompressed Image pixel write failed");

			free(Buffer);
			ftruncate(Image->UCPixelsFd, (off_t)(Image->PixelCnt*Image->PixelSize+1));
			Image->Compressed = TRUE;
			Image->PixelsAccessed = TRUE;

			Image->CompressionMethod = 0;
			if((envVar = getenv("IMAGE_COMPRESS")) != NULL)
			{
				Image->Compressed = TRUE;
				/* find the specified compression method; if it can't be determined,
					 it defaults to 0 */
				if(sscanf(envVar,"%d", &(Image->CompressionMethod)) != 1)
					Image->CompressionMethod = 0;

				/* this step assumes that NumberOfCompressionMethods > 0 */
				if(Image->CompressionMethod >= NumberOfCompressionMethods)
				{
					char message[256];
					sprintf(message, "Compression method %d out of bounds (only %d methods defined).\nDefaulting to method 0",
						Image->CompressionMethod, NumberOfCompressionMethods);
					Warn(message);
					Image->CompressionMethod = 0;
				}
			}
			Image->Address[aVERNO] =
				Image->Address[aVERNO] | COMPRESSED | Image->CompressionMethod * 4096;

			compressImage(Image);
			Image->PixelsAccessed = FALSE;
			close(Image->UCPixelsFd);
			unlink(Image->UCPixelsFileName);

			/* save the compression type as an info field */
			if(Image->Compressed)
				imputinfo(Image, "Pixel Compression Method", 
					compressionMethods[Image->CompressionMethod].methodName);
		}

		/* if the image was opened as a compressed file, but the
			 IMAGE_FORCE_UNCOMPRESS environment variable was set, then close it as
			 an uncompressed file */
		if(Image->Compressed && getenv("IMAGE_FORCE_UNCOMPRESS"))
		{
			decompressImage(Image);

			/*
				Buffer = (char*)malloc(Image->PixelCnt * Image->PixelSize);
			*/
			lseek(Image->UCPixelsFd, 0, FROMBEG);
			Cnt = read(Image->UCPixelsFd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			if (Cnt != Image->PixelCnt*Image->PixelSize)
				Error("Uncompressed pixel read failed");

			/* Write pixels into image file */
			Cnt = (int)lseek(Image->Fd, (long)Image->Address[aPIXELS], FROMBEG);
			Cnt = write(Image->Fd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			if (Cnt != Image->PixelCnt * Image->PixelSize)
				Error("Uncompressed Image pixel write failed");

			free(Buffer);
			/* set a few things straight */
			Image->Compressed = FALSE;
			Image->Address[aVERNO] = Image->Address[aVERNO] - COMPRESSED - Image->CompressionMethod * 4096;
			Image->Address[aINFO] =
				Image->Address[aPIXELS] + Image->PixelCnt*Image->PixelSize;

			imputinfo(Image, "Pixel Compression Method", NULL);

			close(Image->UCPixelsFd);
			unlink(Image->UCPixelsFileName);
		}

		/* if the image has been compressed, and we have modified the pixels,
			 we must compress the pixels back into the image file and remove
			 the temp file */
		if(Image->Compressed && Image->PixelsModified)
		{
			compressImage(Image);
			close(Image->UCPixelsFd);
			unlink(Image->UCPixelsFileName);
			/* save the compression type as an info field */
			if(Image->Compressed)
				imputinfo(Image, "Pixel Compression Method", 
					compressionMethods[Image->CompressionMethod].methodName);
		}
		else if(Image->Compressed && Image->PixelsAccessed)
		{
			close(Image->UCPixelsFd);
			unlink(Image->UCPixelsFileName);
		}
#endif

		/* Swap the byte order of header fields except the title and address */
		if (Image->SwapNeeded) Swapheader(Image); 
 
		/* Write Title field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aTITLE], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Title[0], sizeof(Image->Title));
		if (Cnt != sizeof(Image->Title)) Warn("Image write failed");

		/* Write MaxMin field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aMAXMIN], FROMBEG);
		Cnt = write(Fd, (char *)&Image->ValidMaxMin, sizeof(Image->ValidMaxMin));
		if (Cnt != sizeof(Image->ValidMaxMin)) Warn("Image write failed");
		Cnt = write(Fd, (char *)&Image->MaxMin[0], sizeof(Image->MaxMin));
		if (Cnt != sizeof(Image->MaxMin)) Warn("Image write failed");

		/* Write Histogram field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aHISTO], FROMBEG);
		Cnt = write(Fd,(char *)&Image->ValidHistogram,sizeof(Image->ValidHistogram));
		if (Cnt != sizeof(Image->ValidHistogram)) Warn("Image write failed");
		Cnt = write(Fd, (char *)&Image->Histogram[0], sizeof(Image->Histogram));
		if (Cnt != sizeof(Image->Histogram)) Warn("Image write failed");

		/* Write PixelFormat field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aPIXFORM], FROMBEG);
		Cnt = write(Fd, (char *)&Image->PixelFormat, sizeof(Image->PixelFormat));
		if (Cnt != sizeof(Image->PixelFormat)) Warn("Image write failed");

		/* Write DimC field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aDIMC], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Dimc, sizeof(Image->Dimc));
		if (Cnt != sizeof(Image->Dimc)) Warn("Image write failed");

		/* Write DimV field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aDIMV], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Dimv[0], sizeof(Image->Dimv));
		if (Cnt != sizeof(Image->Dimv)) Warn("Image write failed");

		/* Write Info field */
		InfoLength = 0;
		Cnt = (int)lseek(Fd, (long)Image->Address[aINFO], FROMBEG);
		for (i=0; i<Image->InfoCnt; i++)
		{
      /* Write name of field and free string */
      Length = (int) strlen(Image->InfoName[i]) + 1;
      Cnt = write(Fd, (char *)Image->InfoName[i], Length);
      if (Cnt != Length) Warn("Image write failed");
      free(Image->InfoName[i]);
      InfoLength += Length;

      /* Write field data and free string */
      Length = (int) strlen(Image->InfoData[i]) + 1;
      Cnt = write(Fd, (char *)Image->InfoData[i], Length);
      if (Cnt != Length) Warn("Image write failed");
      free(Image->InfoData[i]);
      InfoLength += Length;
		}
		Cnt = write(Fd, (char *)&Null, sizeof(Null));
		InfoLength += 1;

		/* change the length of the file */
		ftruncate(Fd, (off_t)(Image->Address[aINFO] + InfoLength));

		if (Image->SwapNeeded) /* Swap the byte order of each address */
      Swap((char *)&Image->Address[0], sizeof(Image->Address), INT);

		/* Write addresses of image header fields */
		Cnt = (int)lseek(Fd, (long)0, FROMBEG);
		Cnt = write(Fd, (char *)&Image->Address[0], sizeof(Image->Address));
		if (Cnt != sizeof(Image->Address)) Warn("Image write failed");
	}

	/* Close file and free image record */
	free((char *)Image);
	close(Fd);
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine closes an image, forcing it to be compressed.      */
/*                                                                           */
/*---------------------------------------------------------------------------*/
#ifndef NO_COMPRESSION
int imcloseC (IMAGE *Image)
{
	int Fd;
	int Cnt;
	int Length;
	int InfoLength;
	char Null = '\0';
	int i;
	char* Buffer;
	char* envVar;

	if(haveNotReadCompressionConfigFile) readCompressionConfigFile();

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");

	/* Check that file is open */
	Fd = Image->Fd;
	if (Fd == EOF) Error("Image not open");

	if (Image->nImgFormat == 0)
	{
		/* if the image was opened as an uncompressed file, close it as a
			 compressed file */
		if(!Image->Compressed)
		{
			strcpy(Image->UCPixelsFileName, "/usr/tmp/tempimXXXXXX");
			mkstemp(Image->UCPixelsFileName);
			Image->UCPixelsFd = open(Image->UCPixelsFileName,
				O_RDWR | O_CREAT | O_TRUNC,
				DEFAULT);

			Buffer = (char*)malloc(Image->PixelCnt * Image->PixelSize);

			/* read pixel data from image file */
			Cnt = (int)lseek(Image->Fd, (long)Image->Address[aPIXELS], FROMBEG);
			Cnt = read(Image->Fd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			if (Cnt != Image->PixelCnt*Image->PixelSize)
				Error("Uncompressed pixel read failed");
       
			/* write pixel data to temp file */
			Cnt = write(Image->UCPixelsFd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			free(Buffer);
			if (Cnt != Image->PixelCnt * Image->PixelSize)
				Error("Uncompressed Image pixel write failed");

			ftruncate(Image->UCPixelsFd, (off_t)(Image->PixelCnt*Image->PixelSize+1));
			Image->Compressed = TRUE;
			Image->PixelsAccessed = TRUE;
			Image->PixelsModified = TRUE;

			Image->CompressionMethod = 0;
			if((envVar = getenv("IMAGE_COMPRESS")) != NULL)
			{
				Image->Compressed = TRUE;
				/* find the specified compression method; if it can't be determined,
					 it defaults to 0 */
				if(sscanf(envVar,"%d", &(Image->CompressionMethod)) != 1)
					Image->CompressionMethod = 0;

				/* this step assumes that NumberOfCompressionMethods > 0 */
				if(Image->CompressionMethod >= NumberOfCompressionMethods)
				{
					char message[256];
					sprintf(message, "Compression method %d out of bounds (only %d methods defined).\nDefaulting to method 0",
						Image->CompressionMethod, NumberOfCompressionMethods);
					Warn(message);
					Image->CompressionMethod = 0;
				}
			}
			Image->Address[aVERNO] =
				Image->Address[aVERNO] | COMPRESSED | Image->CompressionMethod * 4096;
		}

		if(Image->PixelsModified)
		{
			compressImage(Image);
			close(Image->UCPixelsFd);
			unlink(Image->UCPixelsFileName);
		}
		else if(Image->PixelsAccessed)
		{
			close(Image->UCPixelsFd);
			unlink(Image->UCPixelsFileName);
		}

		/* save the compression type as an info field */
		imputinfo(Image, "Pixel Compression Method", 
			compressionMethods[Image->CompressionMethod].methodName);

		/* Swap the byte order of header fields except the title and address */
		if (Image->SwapNeeded) Swapheader(Image); 
 
		/* Write Title field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aTITLE], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Title[0], sizeof(Image->Title));
		if (Cnt != sizeof(Image->Title)) Warn("Image write failed");

		/* Write MaxMin field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aMAXMIN], FROMBEG);
		Cnt = write(Fd, (char *)&Image->ValidMaxMin, sizeof(Image->ValidMaxMin));
		if (Cnt != sizeof(Image->ValidMaxMin)) Warn("Image write failed");
		Cnt = write(Fd, (char *)&Image->MaxMin[0], sizeof(Image->MaxMin));
		if (Cnt != sizeof(Image->MaxMin)) Warn("Image write failed");

		/* Write Histogram field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aHISTO], FROMBEG);
		Cnt = write(Fd,(char *)&Image->ValidHistogram,sizeof(Image->ValidHistogram));
		if (Cnt != sizeof(Image->ValidHistogram)) Warn("Image write failed");
		Cnt = write(Fd, (char *)&Image->Histogram[0], sizeof(Image->Histogram));
		if (Cnt != sizeof(Image->Histogram)) Warn("Image write failed");

		/* Write PixelFormat field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aPIXFORM], FROMBEG);
		Cnt = write(Fd, (char *)&Image->PixelFormat, sizeof(Image->PixelFormat));
		if (Cnt != sizeof(Image->PixelFormat)) Warn("Image write failed");

		/* Write DimC field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aDIMC], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Dimc, sizeof(Image->Dimc));
		if (Cnt != sizeof(Image->Dimc)) Warn("Image write failed");

		/* Write DimV field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aDIMV], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Dimv[0], sizeof(Image->Dimv));
		if (Cnt != sizeof(Image->Dimv)) Warn("Image write failed");

		/* Write Info field */
		InfoLength = 0;
		Cnt = (int)lseek(Fd, (long)Image->Address[aINFO], FROMBEG);
		for (i=0; i<Image->InfoCnt; i++)
		{
      /* Write name of field and free string */
      Length = (int) strlen(Image->InfoName[i]) + 1;
      Cnt = write(Fd, (char *)Image->InfoName[i], Length);
      if (Cnt != Length) Warn("Image write failed");
      free(Image->InfoName[i]);
      InfoLength += Length;

      /* Write field data and free string */
      Length = (int) strlen(Image->InfoData[i]) + 1;
      Cnt = write(Fd, (char *)Image->InfoData[i], Length);
      if (Cnt != Length) Warn("Image write failed");
      free(Image->InfoData[i]);
      InfoLength += Length;
		}
		Cnt = write(Fd, (char *)&Null, sizeof(Null));
		InfoLength += 1;

		/* change the length of the file */
		ftruncate(Fd, (off_t)(Image->Address[aINFO] + InfoLength));

		if (Image->SwapNeeded) /* Swap the byte order of each address */
      Swap((char *)&Image->Address[0], sizeof(Image->Address), INT);

		/* Write addresses of image header fields */
		Cnt = (int)lseek(Fd, (long)0, FROMBEG);
		Cnt = write(Fd, (char *)&Image->Address[0], sizeof(Image->Address));
		if (Cnt != sizeof(Image->Address)) Warn("Image write failed");
	}else{
		fprintf(stderr,"can only compress .im format images\n");
	}
	
	/* Close file and free image record */
	free((char *)Image);
	close(Fd);
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine closes an image, forcing it to be uncompressed.    */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imcloseU (IMAGE *Image)
{
	int Fd;
	int Cnt;
	int Length;
	int InfoLength;
	char Null = '\0';
	int i;
	char* Buffer;
	char* envVar;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");

	/* Check that file is open */
	Fd = Image->Fd;
	if (Fd == EOF) Error("Image not open");

	if (Image->nImgFormat == 0)
	{
		/* if the image was opened as a compressed file, close it as an
			 uncompressed file */
		if(Image->Compressed)
		{
			decompressImage(Image);

			Buffer = (char*)malloc(Image->PixelCnt * Image->PixelSize);
			lseek(Image->UCPixelsFd, 0, FROMBEG);
			Cnt = read(Image->UCPixelsFd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			if (Cnt != Image->PixelCnt*Image->PixelSize)
				Error("Uncompressed pixel read failed");

			/* Write pixels into image file */
			Cnt = (int)lseek(Image->Fd, (long)Image->Address[aPIXELS], FROMBEG);
			Cnt = write(Image->Fd, (char *)Buffer, Image->PixelCnt*Image->PixelSize);
			if (Cnt != Image->PixelCnt * Image->PixelSize)
				Error("Uncompressed Image pixel write failed");
			free(Buffer);

			/* set a few things straight */
			Image->Compressed = FALSE;
			Image->Address[aVERNO] = Image->Address[aVERNO] - COMPRESSED - Image->CompressionMethod * 4096;
			Image->Address[aINFO] =
				Image->Address[aPIXELS] + Image->PixelCnt*Image->PixelSize;

			imputinfo(Image, "Pixel Compression Method", NULL);

			close(Image->UCPixelsFd);
			unlink(Image->UCPixelsFileName);
		}

		/* Swap the byte order of header fields except the title and address */
		if (Image->SwapNeeded) Swapheader(Image); 
 
		/* Write Title field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aTITLE], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Title[0], sizeof(Image->Title));
		if (Cnt != sizeof(Image->Title)) Warn("Image write failed");

		/* Write MaxMin field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aMAXMIN], FROMBEG);
		Cnt = write(Fd, (char *)&Image->ValidMaxMin, sizeof(Image->ValidMaxMin));
		if (Cnt != sizeof(Image->ValidMaxMin)) Warn("Image write failed");
		Cnt = write(Fd, (char *)&Image->MaxMin[0], sizeof(Image->MaxMin));
		if (Cnt != sizeof(Image->MaxMin)) Warn("Image write failed");

		/* Write Histogram field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aHISTO], FROMBEG);
		Cnt = write(Fd,(char *)&Image->ValidHistogram,sizeof(Image->ValidHistogram));
		if (Cnt != sizeof(Image->ValidHistogram)) Warn("Image write failed");
		Cnt = write(Fd, (char *)&Image->Histogram[0], sizeof(Image->Histogram));
		if (Cnt != sizeof(Image->Histogram)) Warn("Image write failed");

		/* Write PixelFormat field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aPIXFORM], FROMBEG);
		Cnt = write(Fd, (char *)&Image->PixelFormat, sizeof(Image->PixelFormat));
		if (Cnt != sizeof(Image->PixelFormat)) Warn("Image write failed");

		/* Write DimC field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aDIMC], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Dimc, sizeof(Image->Dimc));
		if (Cnt != sizeof(Image->Dimc)) Warn("Image write failed");

		/* Write DimV field */
		Cnt = (int)lseek(Fd, (long)Image->Address[aDIMV], FROMBEG);
		Cnt = write(Fd, (char *)&Image->Dimv[0], sizeof(Image->Dimv));
		if (Cnt != sizeof(Image->Dimv)) Warn("Image write failed");

		/* Write Info field */
		InfoLength = 0;
		Cnt = (int)lseek(Fd, (long)Image->Address[aINFO], FROMBEG);
		for (i=0; i<Image->InfoCnt; i++)
		{
      /* Write name of field and free string */
      Length = (int) strlen(Image->InfoName[i]) + 1;
      Cnt = write(Fd, (char *)Image->InfoName[i], Length);
      if (Cnt != Length) Warn("Image write failed");
      free(Image->InfoName[i]);
      InfoLength += Length;

      /* Write field data and free string */
      Length = (int) strlen(Image->InfoData[i]) + 1;
      Cnt = write(Fd, (char *)Image->InfoData[i], Length);
      if (Cnt != Length) Warn("Image write failed");
      free(Image->InfoData[i]);
      InfoLength += Length;
		}
		Cnt = write(Fd, (char *)&Null, sizeof(Null));
		InfoLength += 1;

		/* change the length of the file */
		ftruncate(Fd, (off_t)(Image->Address[aINFO] + InfoLength));

		if (Image->SwapNeeded) /* Swap the byte order of each address */
      Swap((char *)&Image->Address[0], sizeof(Image->Address), INT);

		/* Write addresses of image header fields */
		Cnt = (int)lseek(Fd, (long)0, FROMBEG);
		Cnt = write(Fd, (char *)&Image->Address[0], sizeof(Image->Address));
		if (Cnt != sizeof(Image->Address)) Warn("Image write failed");
	}
	/* Close file and free image record */
	free((char *)Image);
	close(Fd);
	return(VALID);
}
#endif

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine swaps the byte order of each word in a buffer.     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int Swap (char *Buffer, int Length, int Type)
{
	int Word, nWord, Byte, nByte;
	char temp;

	switch (Type) {
		case GREY        : nByte = sizeof(GREYTYPE);  break;
		case COLOR       : nByte = sizeof(COLORTYPE); break;
		case SHORT       : nByte = sizeof(SHORTTYPE); break;
		case LONG        : nByte = sizeof(LONGTYPE);  break;
		case INT         : nByte = sizeof(int);       break;
		case USERPACKED  : nByte = sizeof(USERTYPE);  break;
		case REAL        : 
		case COMPLEX     : nByte = sizeof(REALTYPE);  break;
		default          :                            break;
	}

	nWord = Length / nByte;
   
	for (Word = 0; Word < nWord; Word ++) {
		for (Byte = 0; Byte < nByte/2; Byte ++) {
			temp = Buffer[Word * nByte + Byte];
			Buffer[Word * nByte + Byte] = Buffer[Word * nByte + nByte - 1 - Byte];
			Buffer[Word * nByte + nByte - 1 - Byte] = temp;
		}
	}
	return 0;
} 

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine swaps the byte order of header fields of an image. */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int 
Swapheader (IMAGE *Image)
{
	Swap((char *)&Image->ValidMaxMin, sizeof(Image->ValidMaxMin), INT);
	Swap((char *)&Image->MaxMin[0], sizeof(Image->MaxMin), INT);
	Swap((char *)&Image->ValidHistogram, sizeof(Image->ValidHistogram), INT);
	Swap((char *)&Image->Histogram[0], sizeof(Image->Histogram), INT);
	Swap((char *)&Image->PixelFormat, sizeof(Image->PixelFormat), INT);
	Swap((char *)&Image->Dimc, sizeof(Image->Dimc), INT);
	Swap((char *)&Image->Dimv[0], sizeof(Image->Dimv), INT);
	return 0;
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  Reads in the compression types				     */
/*                                                                           */
/*---------------------------------------------------------------------------*/

#ifndef NO_COMPRESSION
int readCompressionConfigFile (void)
{
  FILE *configFile;
  char lineread[241];
  int tempChar;
  int offset;
  int numCompMethods;
  int i;

  if((configFile = fopen(COMPRESSION_TYPE_FILE, "r")) == NULL)
  {
    fprintf(stderr, "Error: could not open the compression config file.\n");
    exit(1);
  }

  numCompMethods = 0;
  while(fgets(lineread, 240, configFile))
  {
    if(numCompMethods == MAX_NUM_COMP_METHODS) break;

    /*   ignore lines beginning with '#'   */
    if(lineread[0] != '#')
    {
      tempChar = 0;

      /* read the compression method name (up to the ':') */
      while(lineread[tempChar] != ':')
      {
        compressionMethods[numCompMethods].methodName[tempChar]
					= lineread[tempChar];
				tempChar++;
      }

      /* read the compression command (up to the ':') */
      tempChar++; offset = tempChar;
      while(lineread[tempChar] != ':')
      {
        compressionMethods[numCompMethods].compressionCommand[tempChar-offset]
					= lineread[tempChar];
				tempChar++;
      }

      /* read the decompression command (up to the EOL) */
      tempChar++; offset = tempChar;
      while(lineread[tempChar] != '\n')
      {
        compressionMethods[numCompMethods].decompressionCommand[tempChar-offset]
					= lineread[tempChar];
				tempChar++;
      }

      numCompMethods++;
    }
  }
  haveNotReadCompressionConfigFile = FALSE;
  fclose(configFile);

  if(numCompMethods == 0)
  {
    fprintf(stderr, "Error: could not read any compression methods.\n");
    exit(1);
  }

  NumberOfCompressionMethods = numCompMethods;
  return 1;
}
#endif

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  takes parameters and generates a specific command string to     */
/*           either compress or decompress pixel data                        */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int fillInCompressionCommand (char *specific, char *generic, char *infile,
	char *outfile, IMAGE *Image)
{
  int gPosition = 0;
  int sPosition = 0;
  int i;
  char tempstring[256];

  while(generic[gPosition] != '\0')
  {
    if(generic[gPosition] != '%')
    {
      specific[sPosition] = generic[gPosition];
      gPosition++;  sPosition++;
    }
    else
    {
      gPosition++;
      switch(generic[gPosition])
      {
				/* print the dimensions */
				case 'd':	sprintf(tempstring, "-d %d ", Image->Dimc);
					for(i = 0; i < Image->Dimc; i++)
					{
						char numString[10];
						sprintf(numString, "%d ", Image->Dimv[i]);
						strcat(tempstring, numString);
					}
					break;
					/* print the image type */
				case 't':	sprintf(tempstring, "-t %d ", Image->PixelFormat);
					break;
					/* print the input file name */
				case 'i':	strcpy(tempstring, infile);
					break;
					/* print the output file name */
				case 'o':	strcpy(tempstring, outfile);
					break;
        default:	break;
      }
      for(i = 0; i < (int) strlen(tempstring); i++)
      {
        specific[sPosition] = tempstring[i];
        sPosition++;
      }
      gPosition++;
    }
  }
  specific[sPosition] = '\0';
	return 0;
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  Compresses the pixel data and updates the addresses.            */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int compressImage (IMAGE *Image)
{
#ifndef NO_COMPRESSION
  char commandString[256];
  FILE *pfp;
  char *Buffer;
  int compressedLength;
  int Cnt;

  if(haveNotReadCompressionConfigFile) readCompressionConfigFile();

  /* call the compression program, passing it the appropriate file names */
  fillInCompressionCommand(commandString,
		compressionMethods[Image->CompressionMethod].compressionCommand,
		Image->UCPixelsFileName, "", Image);

  pfp = popen(commandString, "r");

  /* read the compressed data from the pipe to the compression program */
  Buffer = (char*)malloc(Image->PixelSize * Image->PixelCnt);
  compressedLength = (int)fread(Buffer, sizeof(char), Image->PixelCnt*Image->PixelSize, pfp);

  pclose(pfp);

  /* Write compressed pixels into image file */
  Cnt = (int)lseek(Image->Fd, (long)Image->Address[aPIXELS], FROMBEG);
  Cnt = write(Image->Fd, (char *)Buffer, compressedLength);
  if (Cnt != compressedLength) Error("Image pixel write failed");

  /* update the pointers (offsets) */
  Image->Address[aINFO] = Image->Address[aPIXELS] + compressedLength;
  free(Buffer);
#endif
	return 0;
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  Uncompresses the pixel data                                     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int decompressImage (IMAGE *Image)
{
#ifndef NO_COMPRESSION
  char commandString[256];
  FILE *pfp;
  char *Buffer;
  int compressedLength;
  int Cnt;

  if(Image->PixelsAccessed == TRUE)
    return (VALID);

  strcpy(Image->UCPixelsFileName, "/usr/tmp/tempimXXXXXX");
  mkstemp(Image->UCPixelsFileName);

  if(haveNotReadCompressionConfigFile) readCompressionConfigFile();

  /* call the compression program, passing it the appropriate file names */
  fillInCompressionCommand(commandString,
		compressionMethods[Image->CompressionMethod].decompressionCommand,
		"", Image->UCPixelsFileName, Image);
  pfp = popen(commandString, "w");

  /* read the compressed data from the image file */
  compressedLength = (Image->Address[aINFO] - Image->Address[aPIXELS]);
  Buffer = (char*)malloc(compressedLength);
  Cnt = (int)lseek(Image->Fd, (long)Image->Address[aPIXELS], FROMBEG);
  Cnt = read(Image->Fd, (char *)Buffer, compressedLength);
  if (Cnt != compressedLength) Error("Compressed pixel read failed");

  /* write data to pipe */
  Cnt = (int)fwrite(Buffer, sizeof(char), compressedLength, pfp);
  if (Cnt != compressedLength) Error("Image decompression failed");

  pclose(pfp);

  Image->PixelsAccessed = TRUE;

  /* open the decompressed pixels file for reading or writing */
  Image->UCPixelsFd = open(Image->UCPixelsFileName, O_RDWR, DEFAULT);
  free(Buffer);
#endif
	return (VALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads pixel data from an image.                    */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imread(IMAGE *Image, int LoIndex, int HiIndex, GREYTYPE *Buffer)
{
	int Cnt;
	int Length;
	int Offset;   

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Buffer == NULL) Error("Null read buffer");
	if ((LoIndex < 0) || (HiIndex > Image->PixelCnt) ||
		(LoIndex > HiIndex)) Error("Invalid pixel index");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	/* Determine number of bytes to read and the lseek offset */
	Length = (HiIndex - LoIndex +1) * Image->PixelSize;
	Offset = Image->Address[aPIXELS] + LoIndex * Image->PixelSize;

	if(Image->Compressed)
	{
		/* if pixels have not been accessed since opening the image, then */
		/* the pixel data needs to be decompressed */
		if(Image->PixelsAccessed == FALSE) {
			decompressImage(Image);
		}
		/* Read pixels into buffer from decompressed pixels file */
		Cnt = (int)lseek(Image->UCPixelsFd, (long)Offset - Image->Address[aPIXELS], FROMBEG);
		Cnt = read(Image->UCPixelsFd, (char *)Buffer, Length);
		if (Cnt != Length) Error("Uncompressed pixel read failed");
	}
	else
	{
		/* Read pixels into buffer */
		Cnt = (int)lseek(Image->Fd, (long)Offset, FROMBEG);
		Cnt = read(Image->Fd, (char *)Buffer, Length);
		if (Cnt != Length) Error("Image pixel read failed");
	}

	/* Swap the byte order of pixels in buffer if needed */
	if (Image->SwapNeeded) Swap((char *)Buffer, Length, Image->PixelFormat);

	/* unlink the file so that, when it is closed either by closing the
	   file or the program terminating, its resources will be freed.
		 Note that this relies on the fact that in Unix the resource
		 releasing occurs only after the last open instance of the file
		 is closed.
	*/
	unlink(Image->UCPixelsFileName);

	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine writes pixel data to an image.  For GREY images,   */
/*           the maximum and minimum values are also updated, but the        */
/*           histogram is NOT updated.  The algorithm used to update the     */
/*           maximum and minimum values generates a bound on the image       */
/*           intensities, but not always the tightest bound.  This is the    */
/*           best we can do by looking at pixels as they are written.  To    */
/*           compute the tightest upper bound requires that we look at all   */
/*           pixels in the image.  This is what imgetdesc does.              */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imwrite(IMAGE *Image, int LoIndex, int HiIndex, GREYTYPE *Buffer)
{
	int Cnt;
	int Length;
	int Offset;   
	int TempMin;
	int TempMax;
	int PixelCnt;
	int i;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Buffer == NULL) Error("Null read buffer");
	if ((LoIndex < 0) || (HiIndex > Image->PixelCnt) ||
		(LoIndex > HiIndex)) Error("Invalid pixel index");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	if (Image->nImgFormat != 0) Error("Can not write this format image file");

	/* Determine number of bytes to write and the lseek offset */
	Length = (HiIndex - LoIndex +1) * Image->PixelSize;
	Offset = Image->Address[aPIXELS] + LoIndex * Image->PixelSize;

	/* Swap the byte order of pixels in buffer if Needed */
	if (Image->SwapNeeded) Swap((char *)Buffer, Length, Image->PixelFormat);

	if (Image->Compressed) 
	{
		/* decompress the pixels so we have a file to write to */
		if(Image->PixelsAccessed == FALSE)
			decompressImage(Image);

		/* Write pixels into decompressed pixel file */
		Cnt = (int)lseek(Image->UCPixelsFd, (long)Offset - Image->Address[aPIXELS], FROMBEG);
		Cnt = write(Image->UCPixelsFd, (char *)Buffer, Length);
		if (Cnt != Length) Error("Image pixel write failed");
	}

	else
	{
		/* Write pixels into image file */
		Cnt = (int)lseek(Image->Fd, (long)Offset, FROMBEG);
		Cnt = write(Image->Fd, (char *)Buffer, Length);
		if (Cnt != Length) Error("Image pixel write failed");
	}

	Image->PixelsModified = TRUE;

	/* Invalidate the MaxMin and Histogram fields */
	Image->ValidMaxMin = FALSE;
	Image->ValidHistogram = FALSE;

	/* Compute new Maximum and Minimum for GREY images */
	if (Image->PixelFormat == GREY)
	{
		TempMax = Image->MaxMin[1];
		TempMin = Image->MaxMin[0];
		PixelCnt = HiIndex - LoIndex + 1;
		for (i=0; i<PixelCnt; i++)
		{
			if (Buffer[i] > TempMax) TempMax = Buffer[i];
			if (Buffer[i] < TempMin) TempMin = Buffer[i];
		}
		Image->MaxMin[1] = TempMax;
		Image->MaxMin[0] = TempMin;
	}

	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads an arbitrary subwindow of an image.  It      */
/*           does optimal I/O for 1D, 2D and 3D images.  More general I/O    */
/*           is used for higher dimension images.                            */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imgetpix(IMAGE *Image, int Endpts[][2], int *Coarseness, GREYTYPE
	*Pixels)
{
	int i;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Pixels == NULL) Error("Null pixel buffer");
   
	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	/* Check endpoints */
	for (i=0; i<Image->Dimc; i++)
	{
		if (Coarseness[i] != 1) Error("Coarseness not implemented");
		if (Endpts[i][0] < 0) Error("Bad endpoints range");
		if (Endpts[i][1] >= Image->Dimv[i]) Error("Bad endpoints range");
		if (Endpts[i][1] < Endpts[i][0]) Error("Bad endpoints order");
	}

	/* Check for image dimensions */
	switch (Image->Dimc) {

		/* Handle 1D images */
		case 1: 
			return(imread(Image, Endpts[0][0], Endpts[0][1], Pixels));
			break;

      /* Handle 2D images */
		case 2:
			return(GetPut2D(Image, Endpts, Pixels, READMODE));
			break;

      /* Handle 3D images */
		case 3:
			return(GetPut3D(Image, Endpts, Pixels, READMODE));
			break;

      /* Handle higher dimension images */
		default:
			return(GetPutND(Image, Endpts, Pixels, READMODE));
			break;
	}

	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine writes an arbitrary subwindow of an image.  It     */
/*           does optimal I/O for 1D, 2D and 3D images.  More general I/O    */
/*           is used for higher dimension images.                            */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imputpix(IMAGE *Image, int Endpts[][2], int *Coarseness, GREYTYPE *Pixels)
{
	int i;
	int PixelCnt;
	int TempMax;
	int TempMin;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Pixels == NULL) Error("Null pixel buffer");
   
	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	if (Image->nImgFormat != 0) Error("Can not write this format image file");

	/* Check endpoints */
	PixelCnt = 1;
	for (i=0; i<Image->Dimc; i++)
	{
		if (Coarseness[i] != 1) Error("Coarseness not implemented");
		if (Endpts[i][0] < 0) Error("Bad endpoints range");
		if (Endpts[i][1] >= Image->Dimv[i]) Error("Bad endpoints range");
		if (Endpts[i][1] < Endpts[i][0]) Error("Bad endpoints order");
		PixelCnt = PixelCnt * (Endpts[i][1] - Endpts[i][0] + 1);
	}

	/* Invalidate the MaxMin and Histogram fields */
	Image->ValidMaxMin = FALSE;
	Image->ValidHistogram = FALSE;

	/* Compute new Maximum and Minimum for GREY images */
	if (Image->PixelFormat == GREY)
	{
		TempMax = Image->MaxMin[1];
		TempMin = Image->MaxMin[0];
		for (i=0; i<PixelCnt; i++)
		{
			if (Pixels[i] > TempMax) TempMax = Pixels[i];
			if (Pixels[i] < TempMin) TempMin = Pixels[i];
		}
		Image->MaxMin[1] = TempMax;
		Image->MaxMin[0] = TempMin;
	}

	/* Check for image dimensions */
	switch (Image->Dimc) {

		/* Handle 1D images */
		case 1: 
			return(imwrite(Image, Endpts[0][0], Endpts[0][1], Pixels));
			break;

      /* Handle 2D images */
		case 2:
			return(GetPut2D(Image, Endpts, Pixels, WRITEMODE));
			break;

      /* Handle 3D images */
		case 3:
			return(GetPut3D(Image, Endpts, Pixels, WRITEMODE));
			break;

      /* Handle higher dimension images */
		default:
			return(GetPutND(Image, Endpts, Pixels, WRITEMODE));
			break;
	}

	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads and writes pixels to 2D images.  The         */
/*           minimum number of I/O calls are used.                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int GetPut2D(IMAGE *Image, int Endpts[][2], GREYTYPE *Pixels, int Mode)
{
	int Xdim;
	int Ydim;
	int Xlength;
	int Ylength;
	int ReadBytes;
	int Yloop;
	int Yskipcnt;
	int FirstPixel;
	int Cnt;
	int i;
	char *PixelPtr;

	if (Mode != READMODE && Image->nImgFormat != 0) 
		Error("Can not write this format image file");

	/* The 0th dimension is y; the 1st is x */
	Ydim = 0;
	Xdim = 1;
	    
	/* Determine how many pixels to READ/WRITE in each dimension */
	Ylength = (Endpts[Ydim][1] - Endpts[Ydim][0]) + 1;
	Xlength = (Endpts[Xdim][1] - Endpts[Xdim][0]) + 1;
	    
	/* Determine how many pixels to SKIP in each dimension */
	Yskipcnt = (Image->Dimv[Xdim]-Xlength) * Image->PixelSize;

	/* Determine the maximum number of consecutive pixels that can */
	/* be read in at one time and the number of non-contiguous     */
	/* sections that span the y dimension                          */
	if (Xlength != Image->Dimv[Xdim])
	{
		ReadBytes = Xlength * Image->PixelSize;
		Yloop = Ylength;
	}
	else 
	{
		ReadBytes = Ylength * Xlength * Image->PixelSize;
		Yloop = 1;
	}
 
	/* Calculate position of first pixel */
	FirstPixel = ((Endpts[Ydim][0] * Image->Dimv[Xdim]) 
		+ Endpts[Xdim][0]) * Image->PixelSize
		+ Image->Address[aPIXELS];

	/* if the pixels have not been uncompressed, do so now */
	if(Image->Compressed && !Image->PixelsAccessed)
		decompressImage(Image);

	/* Seek to first pixel in image */
	if(Image->Compressed)
	{
		Cnt = (int)lseek(Image->UCPixelsFd,
			(long)FirstPixel - Image->Address[aPIXELS], FROMBEG);
		if (Cnt == -1) Error("Seek first pixel failed");
	}
	else
	{
		Cnt = (int)lseek(Image->Fd, (long)FirstPixel, FROMBEG);
		if (Cnt == -1) Error("Seek first pixel failed");
	}
	    
	/* Loop reading/writing pixel data in sections */
	PixelPtr = (char *) Pixels;
	for (i=0; i<Yloop; i++)
	{
		/* read data into buffer */
		if (Mode == READMODE)
		{
			if(Image->Compressed)
			{
				/* Read pixels into buffer from decompressed pixels file */
				Cnt = read(Image->UCPixelsFd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Compressed pixel read failed");
			}
			else
			{
				Cnt = read(Image->Fd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Pixel read failed");
			}

			/* Swap the byte order of pixels read in if Needed */
			if (Image->SwapNeeded) Swap(PixelPtr, ReadBytes, Image->PixelFormat);
		}

		/* write data from buffer */
		else
		{
			/* Swap the byte order of pixels written out if Needed */
			if (Image->SwapNeeded) Swap(PixelPtr, ReadBytes, Image->PixelFormat);

			if (Image->Compressed) 
			{
				/* Write pixels into decompressed pixel file */
				Cnt = write(Image->UCPixelsFd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Compressed pixel write failed");
			}
			else
			{
				Cnt = write(Image->Fd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Pixel write failed");
			}
		}

		/* Advance buffer pointer */
		PixelPtr += ReadBytes;

		/* Seek to next line of pixels to read/write */
		if(Image->Compressed)
		{
			Cnt = (int)lseek(Image->UCPixelsFd, (long)Yskipcnt, FROMHERE);
			if (Cnt == -1) Error("Seek next pixel failed");
		}
		else
		{
			Cnt = (int)lseek(Image->Fd, (long)Yskipcnt, FROMHERE);
			if (Cnt == -1) Error("Seek next pixel failed");
		}
	}

	Image->PixelsModified = TRUE;
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads and writes pixels to 3D images.  The         */
/*           minimum number of I/O calls are used.                           */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int GetPut3D(IMAGE *Image, int Endpts[][2], GREYTYPE *Pixels, int Mode)
{
	int Xdim;
	int Ydim;
	int Zdim;
	int Xlength;
	int Ylength;
	int Zlength;
	int ReadBytes;
	int Yloop;
	int Zloop;
	int Yskipcnt;
	int Zskipcnt;
	int FirstPixel;
	int Cnt;
	int i;
	int j;
	char *PixelPtr;

	if (Mode != READMODE && Image->nImgFormat != 0) 
		Error("Can not write this format image file");

	/* 0th dimension is z; 1st is y; 2nd is x */
	Zdim = 0;
	Ydim = 1;
	Xdim = 2;

	/* Determine how many pixels to READ/WRITE in each dimension*/
	Zlength = (Endpts[Zdim][1] - Endpts[Zdim][0]) + 1;
	Ylength = (Endpts[Ydim][1] - Endpts[Ydim][0]) + 1;
	Xlength = (Endpts[Xdim][1] - Endpts[Xdim][0]) + 1;

	/* Determine how many pixels to SKIP in each dimension*/
	Yskipcnt = (Image->Dimv[Xdim]-Xlength) * Image->PixelSize;
	Zskipcnt = (Image->Dimv[Ydim]-Ylength) 
		* Image->Dimv[Xdim] * Image->PixelSize;

	/* Determine the maximum number of consecutive pixels that can */
	/* be read in at one time and the number of non-contiguous     */
	/* sections that span the y and z dimensions                   */
	if (Xlength != Image->Dimv[Xdim])
	{
		ReadBytes = Xlength * Image->PixelSize;
		Yloop = Ylength;
		Zloop = Zlength;
	}
	else if (Ylength != Image->Dimv[Ydim]) 
	{
		ReadBytes = Ylength * Xlength * Image->PixelSize;
		Yloop = 1;
		Zloop = Zlength;
	}
	else 
	{
		ReadBytes = Zlength * Ylength * Xlength * Image->PixelSize;
		Yloop = 1;
		Zloop = 1;
	}
	    
	/* Calculate position of first pixel in image */
	FirstPixel = ((Endpts[Zdim][0] * Image->Dimv[Xdim] * Image->Dimv[Ydim])
		+ (Endpts[Ydim][0] * Image->Dimv[Xdim]) 
		+  Endpts[Xdim][0]) * Image->PixelSize
		+ Image->Address[aPIXELS];

	/* if the pixels have not been uncompressed, do so now */
	if(Image->Compressed && !Image->PixelsAccessed)
		decompressImage(Image);

	/* Seek to first pixel in image */
	if(Image->Compressed)
	{
		Cnt = (int)lseek(Image->UCPixelsFd,
			(long)FirstPixel - Image->Address[aPIXELS], FROMBEG);
		if (Cnt == -1) Error("Seek first pixel failed");
	}
	else
	{
		Cnt = (int)lseek(Image->Fd, (long)FirstPixel, FROMBEG);
		if (Cnt == -1) Error("Seek first pixel failed");
	}

	/* Loop reading/writing pixel data in sections */
	PixelPtr = (char *) Pixels;
	for (i=0; i<Zloop; i++)
	{
		for (j=0; j<Yloop; j++)
		{

			/* Read data from file into buffer */
			if (Mode == READMODE)
			{
				if(Image->Compressed)
				{
					/* Read pixels into buffer from decompressed pixels file */
					Cnt = read(Image->UCPixelsFd, (char *)PixelPtr, ReadBytes);
					if (Cnt != ReadBytes) Error("Compressed pixel read failed");
				}
				else
				{
					Cnt = read(Image->Fd, (char *)PixelPtr, ReadBytes);
					if (Cnt != ReadBytes) Error("Pixel read failed");
				}

				/* Swap the byte order of pixels read in if Needed */
				if (Image->SwapNeeded) Swap(PixelPtr, ReadBytes, Image->PixelFormat);
			}

			/* write to file from buffer */
			else
			{
				/* Swap the byte order of pixels written out if Needed */
				if (Image->SwapNeeded) Swap(PixelPtr, ReadBytes, Image->PixelFormat);

				if (Image->Compressed) 
				{
					/* Write pixels into decompressed pixel file */
					Cnt = write(Image->UCPixelsFd, (char *)PixelPtr, ReadBytes);
					if (Cnt != ReadBytes) Error("Compressed pixel write failed");
				}
				else
				{
					Cnt = write(Image->Fd, (char *)PixelPtr, ReadBytes);
					if (Cnt != ReadBytes) Error("Pixel write failed");
				}
			}
   
			/* Advance buffer pointer */
			PixelPtr += ReadBytes;
   
			/* Seek to next line of pixels to read/write */
			if(Image->Compressed)
			{
				Cnt = (int)lseek(Image->UCPixelsFd, (long)Yskipcnt, FROMHERE);
				if(Cnt == -1) Error("Seek next pixel failed");
			}
			else
			{
				Cnt = (int)lseek(Image->Fd, (long)Yskipcnt, FROMHERE);
				if (Cnt == -1) Error("Seek next pixel failed");
			}
		}

		/* Seek to next slice of pixels to read/write */
		if(Image->Compressed)
		{
			Cnt = (int)lseek(Image->UCPixelsFd, (long)Zskipcnt, FROMHERE);
			if (Cnt == -1) Error("Seek next pixel failed");
		}
		else
		{
			Cnt = (int)lseek(Image->Fd, (long)Zskipcnt, FROMHERE);
			if (Cnt == -1) Error("Seek next pixel failed");
		}
	}

	Image->PixelsModified = TRUE;
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads and writes pixels to N dimensional images.   */
/*           The minimum number of I/O calls are used.                       */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int GetPutND(IMAGE *Image, int Endpts[][2], GREYTYPE *Pixels, int Mode)
{
	int SliceSize[nDIMV];
	int ReadCnt[nDIMV];
	int SkipCnt[nDIMV];
	int Index[nDIMV];
	int Dimc;
	int ReadBytes;
	int NextPixel;
	int i;
	int Cnt;
	char *PixelPtr;

	if (Mode != READMODE && Image->nImgFormat != 0) Error("Can not write this format image file");

	/* Determine size of one "slice" in each dimension */
	Dimc = Image->Dimc;
	SliceSize[Dimc-1] = Image->PixelSize;
	for (i=Dimc-2; i>=0; i--)
		SliceSize[i] = SliceSize[i+1] * Image->Dimv[i+1];

	/* Determine number of "slices" to read and skip in each dimension */
	for (i=0; i<Dimc; i++)
	{
		ReadCnt[i] = Endpts[i][1] - Endpts[i][0] + 1;
		SkipCnt[i] = Image->Dimv[i] - ReadCnt[i];
	}
	ReadBytes = ReadCnt[Dimc-1] * SliceSize[Dimc-1];
    
	/* if the pixels have not been uncompressed, do so now */
	if(Image->Compressed && !Image->PixelsAccessed)
		decompressImage(Image);

	/* Seek to beginning of pixels */
	if(Image->Compressed)
	{
		Cnt = (int)lseek(Image->UCPixelsFd, (long)0, FROMBEG);
		if (Cnt == -1) Error("Seek first pixel failed");
	}
	else
	{
		Cnt = (int)lseek(Image->Fd, (long)Image->Address[aPIXELS], FROMBEG);
		if (Cnt == -1) Error("Seek first pixel failed");
	}

	/* Find offset to first pixel */
	NextPixel = 0;
	for (i=0; i<Dimc; i++)
	{
		NextPixel = NextPixel + SliceSize[i]*Endpts[i][0];
		Index[i] = Endpts[i][0];
	}

	/* Loop reading and skipping pixels */
	PixelPtr = (char *) Pixels;
	while (Index[0] <= Endpts[0][1])
	{
		/* Seek to next line of pixels to read/write */
		if(Image->Compressed)
		{
			Cnt = (int)lseek(Image->UCPixelsFd, (long)NextPixel, FROMHERE);
			if (Cnt == -1) Error("Seek next pixel failed");
		}
		else
		{
			Cnt = (int)lseek(Image->Fd, (long)NextPixel, FROMHERE);
			if (Cnt == -1) Error("Seek next pixel failed");
		}

		/* read data from file into buffer */
		if (Mode == READMODE)
		{
			if(Image->Compressed)
			{
				/* Read pixels into buffer from decompressed pixels file */
				Cnt = read(Image->UCPixelsFd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Compressed pixel read failed");
			}
			else
			{
				Cnt = read(Image->Fd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Pixel read failed");
			}

			/* Swap the byte order of pixels read in if Needed */
			if (Image->SwapNeeded) Swap(PixelPtr, ReadBytes, Image->PixelFormat);
		}

		/* write data from buffer to file */
		else
		{
			/* Swap the byte order of pixels written out if Needed */
			if (Image->SwapNeeded) Swap(PixelPtr, ReadBytes, Image->PixelFormat);

			if (Image->Compressed) 
			{
				/* Write pixels into decompressed pixel file */
				Cnt = write(Image->UCPixelsFd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Compressed pixel write failed");
			}
			else
			{
				Cnt = write(Image->Fd, (char *)PixelPtr, ReadBytes);
				if (Cnt != ReadBytes) Error("Pixel write failed");
			}
		}
  
		/* Advance buffer pointer */
		PixelPtr += ReadBytes;
		Index[Dimc-1] = Endpts[Dimc-1][1] + 1;
 
		/* Find offset to next pixel */
		NextPixel = 0;
		for (i=Dimc-1; i>=0; i--)
		{
			if (Index[i] > Endpts[i][1])
			{
				NextPixel = NextPixel + SkipCnt[i] * SliceSize[i];
				if (i > 0) 
				{
					Index[i] = Endpts[i][0];
					Index[i-1]++;
				}
			}
		}
	}

	Image->PixelsModified = TRUE;
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads all useful image information from the        */
/*           image image header.  This routine can be used in place of       */
/*           imdim, imbounds, and imgetdesc.                                 */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imheader (IMAGE *Image, int *PixFormat, int *PixSize, int *PixCnt, int
	*Dimc, int *Dimv, int *MaxMin)
{
	int i;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (PixFormat == NULL) Error("Null pixformat pointer");
	if (PixSize == NULL) Error("Null pixsize pointer");
	if (PixCnt == NULL) Error("Null pixcnt pointer");
	if (Dimc == NULL) Error("Null dimc pointer");
	if (Dimv == NULL) Error("Null dimv pointer");
	if (MaxMin == NULL) Error("Null maxmin pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	/* Copy data */
	*PixFormat = Image->PixelFormat;
	*PixSize = Image->PixelSize;
	*PixCnt = Image->PixelCnt;
	*Dimc = Image->Dimc;
	for (i=0; i<*Dimc; i++)
		Dimv[i] = Image->Dimv[i];

	/* Get correct MINMAX field */
	imgetdesc(Image, MINMAX, MaxMin);
   
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads all useful image information from the        */
/*           image image header.  This routine can be used in place of       */
/*           imdim, imbounds, and imgetdesc.                                 */
/*                                                                           */
/*           This version also returns information about compression used    */
/*           in the image (on/off, type, compression ratio)                  */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imheaderC (IMAGE *Image, int *PixFormat, int *PixSize, int *PixCnt, int
	*Dimc, int *Dimv, int *MaxMin, int *Compressed, int *CompMethod, float
	*CompRatio)
{
	int i;
	
	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (PixFormat == NULL) Error("Null pixformat pointer");
	if (PixSize == NULL) Error("Null pixsize pointer");
	if (PixCnt == NULL) Error("Null pixcnt pointer");
	if (Dimc == NULL) Error("Null dimc pointer");
	if (Dimv == NULL) Error("Null dimv pointer");
	if (MaxMin == NULL) Error("Null maxmin pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	/* Copy data */
	*PixFormat = Image->PixelFormat;
	*PixSize = Image->PixelSize;
	*PixCnt = Image->PixelCnt;
	*Dimc = Image->Dimc;
	for (i=0; i<*Dimc; i++)
		Dimv[i] = Image->Dimv[i];

	/* Get correct MINMAX field */
	imgetdesc(Image, MINMAX, MaxMin);
   
	*Compressed = Image->Compressed;
	*CompMethod = Image->CompressionMethod;
	*CompRatio = (float)(Image->Address[aINFO] - Image->Address[aPIXELS]) / 
		(float)(Image->PixelCnt * Image->PixelSize);

	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*	Purpose:  returns the compression information for the image          */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imgetcompinfo (IMAGE *Image, int *Compressed, int *CompMethod, float *CompRatio)
{
	*Compressed = Image->Compressed;
	*CompMethod = Image->CompressionMethod;
	*CompRatio = (float)(Image->Address[aINFO] - Image->Address[aPIXELS]) / 
		(float)(Image->PixelCnt * Image->PixelSize);
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine returns the pixel format and dimension count.      */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imdim (IMAGE *Image, int *PixFormat, int *Dimc)
{

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (PixFormat == NULL) Error("Null pixformat pointer");
	if (Dimc == NULL) Error("Null dimc pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	/* Copy data */
	*PixFormat = Image->PixelFormat;
	*Dimc = Image->Dimc;

	return(VALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine returns the image dimension vector.                */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imbounds (IMAGE *Image, int *Dimv)
{
	int i;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Dimv == NULL) Error("Null dimv pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	/* Copy data */
	for (i=0; i<Image->Dimc; i++)
		Dimv[i] = Image->Dimv[i];
   
	return(VALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads the MAXMIN or HISTO field from the image.    */
/*           The histogram field contains 4096 buckets which contain         */
/*           pixel counts for pixels in the range [MinPixel..MaxPixel].      */
/*           When there are less than 4096 pixel values this technique       */
/*           works perfectly.  When there are more than 4096 pixel values    */
/*           all pixels greater than MinPixel+4095 are included in the       */
/*           4096th bucket.  While this seems like a bit of a hack, it was   */
/*           the best solution given historical contraints.  Ideally, the    */
/*           size of the histogram array should be [MinPixel..MaxPixel].     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imgetdesc (IMAGE *Image, int Type, int Buffer[])
{
	GREYTYPE Pixels[MAXGET];
	int Low, High;
	int TempMax, TempMin;
	int PixelCnt;
	int Index;
	int i;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Image->PixelFormat != GREY) Error("Image type is not GREY");

	/* Handle request for MINMAX field */
	if (Type == MINMAX)
	{
		if (Image->ValidMaxMin == TRUE)
		{
			/* Copy current MaxMin field */
			Buffer[0] = Image->MaxMin[0];
			Buffer[1] = Image->MaxMin[1];
		}
		else
		{
			/* Compute new MaxMin field */
			TempMin = MAXVAL;
			TempMax = MINVAL;

			/* Loop reading pixels */
			for (Low=0; Low<Image->PixelCnt; Low=Low+MAXGET)
			{
				/* Compute read range */
				High = Low + MAXGET - 1;
				if (High >= Image->PixelCnt) High = Image->PixelCnt -1;

				/* Read pixels */
				if (imread(Image, Low, High, Pixels) == INVALID)
					Error("Could not read pixels");
            
				/* Search for new MaxMin Values */
				PixelCnt = High - Low + 1;
				for (i=0; i<PixelCnt; i++)
				{
					if (Pixels[i]<TempMin) TempMin = Pixels[i];
					if (Pixels[i]>TempMax) TempMax = Pixels[i];
				}
			}

			/* Save new MaxMin field */
			Image->ValidMaxMin = TRUE;
			Image->MaxMin[0] = Buffer[0] = TempMin;
			Image->MaxMin[1] = Buffer[1] = TempMax;
		}
	}

	/* Handle request for HISTOGRAM field */
	else if (Type == HISTO) 
	{
		if (Image->ValidHistogram == TRUE)
		{
			/* Copy current Histogram field */
			for (i=0; i<nHISTOGRAM; i++)
				Buffer[i] = (int) Image->Histogram[i];
		}
		else
		{
			/* Compute MAX and MIN pixel values JG1 */
			if (imgetdesc(Image, MINMAX, Buffer) == INVALID)
				Error("Could not obtain minmax values");
			TempMin = Buffer[0];
			TempMax = Buffer[1];
         
			/* Compute new Histogram field */
			for(i=0; i<nHISTOGRAM; i++)
				Buffer[i] = 0;

			/* Loop reading pixels */
			for (Low=0; Low<Image->PixelCnt; Low=Low+MAXGET)
			{
				/* Compute read range */
				High = Low + MAXGET - 1;
				if (High >= Image->PixelCnt) High = Image->PixelCnt -1;
 
				/* Read pixels */
				if (imread(Image, Low, High, Pixels) == INVALID)
					Error("Could not read pixels");
            
				/* Update Histogram and handle any pixels out of range */
				for (i=0; i<=(High-Low); i++)
				{
					/* Determine index into histogram map JG1 */
					Index = Pixels[i] - TempMin;
					if (Index < 0)
						Buffer[ 0 ]++;
					else if (Index >= nHISTOGRAM)
						Buffer[ nHISTOGRAM-1 ]++;
					else
						Buffer[ Index ]++;
				}
			}

			/* Save new Histogram field */
			Image->ValidHistogram = TRUE;
			for (i=0; i<nHISTOGRAM; i++)
				Image->Histogram[i] = Buffer[i];
		}
	}

	return(VALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine returns the validity of the HISTO or MAXMIN	     */
/*	     fields.							     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imtest (IMAGE *Image, int Type)
{

	/* Check parameters */
	if (Image == NULL) {Error("Null image pointer");}

	/* Check that file is open */
	else if (Image->Fd == EOF) {Error("Image not open");}

	/* Handle request for MINMAX field */
	else if (Type == MINMAX)
		return (Image->ValidMaxMin);

	/* Handle request for HISTOGRAM field */
	else if (Type == HISTO) 
		return (Image->ValidHistogram);

	return(INVALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads the title field from the image header.       */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imgettitle (IMAGE *Image, char *Title)
{

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Title == NULL) Error("Null title string pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	/* Copy title field */
	strcpy(Title, Image->Title);

	return(VALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine writes the title field to the image header.        */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imputtitle (IMAGE *Image, char *Title)
{
	int Length;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Title == NULL) Error("Null title string pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	if (Image->nImgFormat != 0) Error("Can not write this format image file");

	/* Copy title field */
	Length = (int) strlen(Title) + 1;
	if (Length > nTITLE) Error("Title too long");
	strcpy(Image->Title, Title);

	return(VALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine reads the specified information field.             */
/*                                                                           */
/*---------------------------------------------------------------------------*/
char *imgetinfo (IMAGE *Image, char *Name)
{
	int i;
	int Length;
	char *Data;

	/* Check parameters */
	if (Image == NULL) ErrorNull("Null image pointer");
	if (Name == NULL) ErrorNull("Null field name pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) ErrorNull("Image not open");

	/* Initialize return pointer */
	Data = NULL;

	/* Search list of information fields */
	for (i=0; i<Image->InfoCnt; i++)
	{
		/* If names match, return the data */
		if (strcmp(Name, Image->InfoName[i]) == 0)
		{
			Length = (int) strlen(Image->InfoData[i]) + 1;
			Data = (char *)malloc((unsigned)Length);
			if (Data == NULL) ErrorNull("Allocation error");
			strcpy(Data, Image->InfoData[i]);
		} 
	}

	return(Data);
}
 

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine writes the specified information field.  If the    */
/*           data is NULL, the field is undefined (ie removed from list).    */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imputinfo (IMAGE *Image, char *Name, char *Data)
{
	int i;
	int Length;
	int Last;
	int Found;
	int Match;

	/* Check parameters */
	if (Image == NULL) Error("Null image pointer");
	if (Name == NULL) Error("Null field name pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) Error("Image not open");

	if (Image->nImgFormat != 0) Error("Can not write this format image file");

	/* Search list of information fields */
	Found = FALSE;
	for (i=0; i<Image->InfoCnt; i++)
	{
		/* Determine if names match */
		Match = strcmp(Name, Image->InfoName[i]);

		/* Either: Replace the data */
		if ((Match == 0) && (Data != NULL))
		{
			free(Image->InfoData[i]);
			Length = (int) strlen(Data) + 1;
			Image->InfoData[i] = (char *)malloc((unsigned)Length);
			if (Image->InfoData[i] == NULL) Error("Allocation error");
			strcpy(Image->InfoData[i], Data);
			Found = TRUE;
			break;
		}

		/* Or: Delete the data and field name */
		else if ((Match == 0) && (Data == NULL))
		{
			free(Image->InfoName[i]);
			free(Image->InfoData[i]);
			Last = Image->InfoCnt - 1;
			Image->InfoName[i] = Image->InfoName[Last];
			Image->InfoData[i] = Image->InfoData[Last];
			Image->InfoCnt--;
			Found = TRUE;
			break;
		} 
	}

	/* Append to end of information field if not found */
	if ((Found == FALSE) && (Image->InfoCnt < nINFO) && (Data != NULL))
	{
		/* Add name field */
		Length = (int) strlen(Name) + 1;
		Image->InfoName[i] = (char *)malloc((unsigned)Length);
		if (Image->InfoName[i] == NULL) Error("Allocation error");
		strcpy(Image->InfoName[i], Name);

		/* Add data field */
		Length = (int) strlen(Data) + 1;
		Image->InfoData[i] = (char *)malloc((unsigned)Length);
		if (Image->InfoData[i] == NULL) Error("Allocation error");
		strcpy(Image->InfoData[i], Data);

		/* Increment field counter */
		Image->InfoCnt ++;
	}

	return(VALID);
}
 

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine copies all information fields from Image1 to       */
/*           Image2.                                                         */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int imcopyinfo (IMAGE *Image1, IMAGE *Image2)
{
	int Length;
	int i;

	/* Check parameters */
	if (Image1 == NULL) Error("Null image pointer");
	if (Image2 == NULL) Error("Null image pointer");

	/* Check that file is open */
	if (Image1->Fd == EOF) Error("Image not open");
	if (Image2->Fd == EOF) Error("Image not open");

	/* Loop through list of information fields */
	Image2->InfoCnt = 0;
	for (i=0; i<Image1->InfoCnt; i++)
	{
		/* Copy name field */
		Length = (int) strlen(Image1->InfoName[i]) + 1;
		Image2->InfoName[i] = (char *)malloc((unsigned)Length);
		if (Image2->InfoName[i] == NULL) Error("Allocation error");
		strcpy(Image2->InfoName[i], Image1->InfoName[i]);

		/* Copy data field */
		Length = (int) strlen(Image1->InfoData[i]) + 1;
		Image2->InfoData[i] = (char *)malloc((unsigned)Length);
		if (Image2->InfoData[i] == NULL) Error("Allocation error");
		strcpy(Image2->InfoData[i], Image1->InfoData[i]);

		/* Increment field counter */
		Image2->InfoCnt ++;
	}

	return(VALID);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine returns a pointer to a list of information         */
/*           field names.                                                    */
/*                                                                           */
/*---------------------------------------------------------------------------*/
char **
iminfoids (IMAGE *Image)
{
	char **Name;
	int Length;
	int i;

	/* Check parameters */
	if (Image == NULL) ErrorNull("Null image pointer");

	/* Check that file is open */
	if (Image->Fd == EOF) ErrorNull("Image not open");

	/* Allocate array of pointers */
	Length = sizeof(char *) * (Image->InfoCnt + 1);
	Name = (char **)malloc((unsigned)Length);
   
	/* Loop through list of information fields */
	for (i=0; i<Image->InfoCnt; i++)
	{
		/* Copy name field */
		Length = (int) strlen(Image->InfoName[i]) + 1;
		Name[i] = (char *)malloc((unsigned)Length);
		if (Name[i] == NULL) ErrorNull("Allocation error");
		strcpy(Name[i], Image->InfoName[i]);
	}

	/* Put a null pointer at the end of the list */
	Name[Image->InfoCnt] = 0;

	return(Name);
}


/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  This routine returns the error string from the error buffer.    */
/*                                                                           */
/*---------------------------------------------------------------------------*/
char *imerror (void)
{
	char *Message;
	int Length;

	/* Copy error string */
	Length = (int) strlen(_imerrbuf) + 1;
	Message = (char *)malloc((unsigned)Length);
	if (Message == NULL) ErrorNull("Allocation error");
	strcpy(Message, _imerrbuf);

	return(Message);
}




/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Function:  Im_snap							     */
/*                                                                           */
/* Purpose:  To privide a quick means to writing out a 2D image from a	     */
/*	     program undergoing development.  This is not intended to be     */
/*	     used in finished programs.					     */
/*                                                                           */
/*	     The image may be of any valid type.  Note that no checking	     */
/*	     of the calling parameters is made.	 The programmer must use     */
/*	     due care.							     */
/*                                                                           */
/* Return:  Returns INVALID if there is a failure, VALID if the image is     */
/*	    successfully dumped.					     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int im_snap (int xdim, int ydim, int pixformat, char *name, char *newtitle,
	char *pixel)
{

	IMAGE  *image;
	int	dimc;
	int	dimv[nDIMV];
	int	pixcnt;
	int	maxmin[nMAXMIN];
	int	histo[nHISTOGRAM];


	pixcnt = xdim*ydim;
	dimc = 2;
	dimv[0] = ydim;
	dimv[1] = xdim;

	/* Create new image file */
	if ((image = imcreat(name, DEFAULT, pixformat, dimc, dimv)) ==
		INVALID) Error("Can not create snapshot image");

	/* Write out new image file */
	if (imwrite(image, 0, pixcnt - 1, (GREYTYPE*) pixel) == INVALID) {
		Error("Can not write pixels to snapshot image\n");
	}

	/* Update the values of minmax and histo for new image */
	if (pixformat == GREY) {
		if (imgetdesc(image, MINMAX, maxmin) == INVALID) {
			Error("Can not update maxmin field");
		}
		if (imgetdesc(image, HISTO, histo) == INVALID) {
			Error("Can not update histo field");
		}
	}

	if (newtitle != NULL) 
		if (imputtitle(image, newtitle) == INVALID) {
			Error("Can not write title to new image");
		}

	/* Close image files */
	imclose(image);

	return (VALID);
}

#ifdef STANDALONE
int main(int argc, char **argv)
{
	IMAGE *im;
	int pixformat, pixsz, pixcnt, dimc, dimv[nDIMV], maxmin[nMAXMIN];

	if (argc != 2){
		fprintf(stderr,"usage: image in.im");
		exit(1);
	}
	im=imopen(argv[1], O_RDONLY);
	if (im == NULL){
		printf("open error\n");
		exit(1);
	}
	imheader(im, &pixformat, &pixsz, &pixcnt, &dimc, dimv, maxmin);
	printf("format=%d, size=%d, cnt=%d, dimc=%d\n", 
		pixformat, pixsz, pixcnt, dimc);

}

#endif
