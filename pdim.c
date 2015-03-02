/*---------------------------------------------------------------------------*/
/*                                                                           */
/* File:     PDIM.C                                                          */
/*                                                                           */
/* Purpose:  The purpose of these routines is to handle the mapping of       */
/*           pixel coordinates to patient coordinates.  This is done         */
/*           by describing each slice of the image using a vector-based      */
/*           plane equation.                                                 */
/*                                                                           */
/* Contains: pdim_read(image, pdiminfo)					     */
/*           pdim_write(image, pdiminfo)				     */
/*           pdim_free(pdiminfo)					     */
/*           pdim_append(pdiminfo1, pdiminfo2)				     */
/*           pdim_window(pdiminfo, dimension, low, high)		     */
/*           pdim_scale(pdiminfo, field, dimension, scale, low, high)	     */
/*           pdim_rotate(pdiminfo, field, dimension, angle, low, high)	     */
/*           pdim_translate(pdiminfo, field, dimension, dist, low, high)     */
/*           pdim_map(pdiminfo, field, u,v,w, x,y,z,t)			     */
/*                                                                           */
/* Author:   John Gauch - Version 2                                          */
/*           Chuck Mosher - Version 1                                        */
/*                                                                           */
/* Date:     July 17, 1986                                                   */
/*                                                                           */
/*---------------------------------------------------------------------------*/

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "image.h"

/* Conversion constant from degrees to radians */
#define DEG2RAD (float)(0.0174532)

/* Error string buffer */
static char _imerrbuf[nERROR];

/* Macro for handling system errors */
#define Error(Mesg)\
   {\
   strcpy(_imerrbuf, Mesg);\
   return(INVALID);\
   }

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to read a pdim string from an    */
/*           image and put it into record format.  The PDIM string is read   */
/*           to determine the mapping from pixel to patient coordinates      */
/*           and the TDIM strng is read to determine the mapping from        */
/*           pixel to table coordinates.                                     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_read(IMAGE *image, PDIMREC *pdiminfo)
{
	SLICEREC *slicedata;
	int    pixformat, pixsize, pixcnt;
	int    dimc, dimv[nDIMV]; 
	int    maxmin[nMAXMIN];
	int    pdimlen; 
	char   *pdimstr, *pdimptr;
	int    i; 

	/* Read the image dimension data */
	if (imheader(image,&pixformat,&pixsize,&pixcnt,&dimc,dimv,maxmin)==INVALID)
		Error("Can not read image header");

	/* Determine number of slices */
	if (dimc==2) 
		pdiminfo->slicecnt = 1;
	else if (dimc==3)
		pdiminfo->slicecnt = dimv[0];
	else 
		Error("Can only handle 2D and 3D images");

	/* Allocate slicedata record */
	pdimlen = sizeof(SLICEREC) * pdiminfo->slicecnt;
	pdiminfo->patient = (SLICEREC *)malloc((unsigned)pdimlen);
	pdiminfo->table = (SLICEREC *)malloc((unsigned)pdimlen);
	if ((pdiminfo->patient == NULL) || (pdiminfo->table == NULL)) 
		Error("Can not allocate PDIM record");
   
	/* Read PDIM info field of specified image */
	if ((pdimstr = imgetinfo(image, "pdim")) == NULL)
		Error("Can not read PDIM string");

	/* Decode header information */
	pdimptr = pdimstr;
	sscanf(pdimptr, "v=%d,u=%d,m=%d,", &(pdiminfo->version),
		&(pdiminfo->units), &(pdiminfo->machine));

	/* Loop reading fields of pdim string */
	slicedata = pdiminfo->patient;
	for (i=0; i<pdiminfo->slicecnt; i++)
	{
		/* Advance the pdim string pointer (ugly stuff) */
		pdimptr++;
		while ((*pdimptr != 's') && (*pdimptr != '\0')) pdimptr++;

		sscanf(pdimptr, "s=%d,t=%f(%f,%f,%f)(%f,%f,%f)(%f,%f,%f)",
			&(slicedata[i].number),
			&(slicedata[i].time),
			&(slicedata[i].Ox), &(slicedata[i].Oy), &(slicedata[i].Oz),
			&(slicedata[i].Ux), &(slicedata[i].Uy), &(slicedata[i].Uz),
			&(slicedata[i].Vx), &(slicedata[i].Vy), &(slicedata[i].Vz));
	}

	/* Read TDIM info field of specified image */
	free((char *)pdimstr);
	if ((pdimstr = imgetinfo(image, "tdim")) == NULL)
		Error("Can not read TDIM string");

	/* Decode header information (not really needed) */
	pdimptr = pdimstr;
	sscanf(pdimptr, "v=%d,u=%d,m=%d,", &(pdiminfo->version),
		&(pdiminfo->units), &(pdiminfo->machine));

	/* Loop reading fields of tdim string */
	slicedata = pdiminfo->table;
	for (i=0; i<pdiminfo->slicecnt; i++)
	{
		/* Advance the tdim string pointer (ugly stuff) */
		pdimptr++;
		while ((*pdimptr != 's') && (*pdimptr != '\0')) pdimptr++;

		sscanf(pdimptr, "s=%d,t=%f(%f,%f,%f)(%f,%f,%f)(%f,%f,%f)",
			&(slicedata[i].number),
			&(slicedata[i].time),
			&(slicedata[i].Ox), &(slicedata[i].Oy), &(slicedata[i].Oz),
			&(slicedata[i].Ux), &(slicedata[i].Uy), &(slicedata[i].Uz),
			&(slicedata[i].Vx), &(slicedata[i].Vy), &(slicedata[i].Vz));
	}

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to write a pdim string to an     */
/*           image from record format.                                       */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_write(IMAGE *image, PDIMREC *pdiminfo)
{
	SLICEREC *slicedata;
	int    pdimlen;
	char   *pdimstr;
	char   buffer[REC_SIZE];
	int    i;

	/* Allocate the pdim string */
	pdimlen = (1 + pdiminfo->slicecnt) * REC_SIZE;
	pdimstr = (char *)malloc((unsigned)pdimlen);
	pdimstr[0] = '\0';

	/* Write PDIM header information */
	sprintf(buffer, "v=%d,u=%d,m=%d,", pdiminfo->version,
		pdiminfo->units, pdiminfo->machine);
	strcat(pdimstr, buffer);

	/* Loop writing fields of PDIM string */
	slicedata = pdiminfo->patient;
	for (i=0; i<pdiminfo->slicecnt; i++)
	{
		sprintf(buffer, "s=%d,t=%f(%f,%f,%f)(%f,%f,%f)(%f,%f,%f)",
			slicedata[i].number,
			slicedata[i].time,
			slicedata[i].Ox, slicedata[i].Oy, slicedata[i].Oz,
			slicedata[i].Ux, slicedata[i].Uy, slicedata[i].Uz,
			slicedata[i].Vx, slicedata[i].Vy, slicedata[i].Vz);
		strcat(pdimstr, buffer);
	}

	/* Write PDIM info field to specified image */
	if (imputinfo(image,"pdim",pdimstr) == INVALID) return(INVALID);

	/* Write TDIM header information */
	pdimstr[0] = '\0';
	sprintf(buffer, "v=%d,u=%d,m=%d,", pdiminfo->version,
		pdiminfo->units, pdiminfo->machine);
	strcat(pdimstr, buffer);

	/* Loop writing fields of TDIM string */
	slicedata = pdiminfo->table;
	for (i=0; i<pdiminfo->slicecnt; i++)
	{
		sprintf(buffer, "s=%d,t=%f(%f,%f,%f)(%f,%f,%f)(%f,%f,%f)",
			slicedata[i].number,
			slicedata[i].time,
			slicedata[i].Ox, slicedata[i].Oy, slicedata[i].Oz,
			slicedata[i].Ux, slicedata[i].Uy, slicedata[i].Uz,
			slicedata[i].Vx, slicedata[i].Vy, slicedata[i].Vz);
		strcat(pdimstr, buffer);
	}

	/* Write TDIM info field to specified image */
	if (imputinfo(image,"tdim",pdimstr) == INVALID) return(INVALID);

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to free the space allocated      */
/*           when a pdim record is read.                                     */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_free(PDIMREC *pdiminfo)
{

	/* Free the patient record and the table record */
	free((char *)pdiminfo->patient);
	free((char *)pdiminfo->table);
	pdiminfo->patient = NULL;
	pdiminfo->table = NULL;

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to append pdim record #2 to      */
/*           the END of pdim record #1.   This will be useful to "imzcat".   */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_append(PDIMREC *pdiminfo1, PDIMREC *pdiminfo2)
{
	SLICEREC *slicedata;
	SLICEREC *newslicedata;
	int pdimlen;
	int i, j;

	/* Allocate new slicedata record for PDIM field */
	pdimlen = sizeof(SLICEREC)*(pdiminfo1->slicecnt+pdiminfo2->slicecnt);
	newslicedata = (SLICEREC *)malloc((unsigned)pdimlen);
	if (newslicedata == NULL) 
		Error("Could not allocate new pdim record");

	/* Loop through slices moving data to new slicedata record */
	slicedata = pdiminfo1->patient;
	for (i=0; i<pdiminfo1->slicecnt; i++) {
		newslicedata[i].Ox = slicedata[i].Ox;
		newslicedata[i].Oy = slicedata[i].Oy;
		newslicedata[i].Oz = slicedata[i].Oz;
		newslicedata[i].Ux = slicedata[i].Ux;
		newslicedata[i].Uy = slicedata[i].Uy;
		newslicedata[i].Uz = slicedata[i].Uz;
		newslicedata[i].Vx = slicedata[i].Vx;
		newslicedata[i].Vy = slicedata[i].Vy;
		newslicedata[i].Vz = slicedata[i].Vz;
		newslicedata[i].time = slicedata[i].time;
		newslicedata[i].number = slicedata[i].number;
	} 

	/* Loop through slices appending data to new slicedata record */
	j = i;
	slicedata = pdiminfo2->patient;
	for (i=0; i<pdiminfo2->slicecnt; i++) {
		newslicedata[j].Ox = slicedata[i].Ox;
		newslicedata[j].Oy = slicedata[i].Oy;
		newslicedata[j].Oz = slicedata[i].Oz;
		newslicedata[j].Ux = slicedata[i].Ux;
		newslicedata[j].Uy = slicedata[i].Uy;
		newslicedata[j].Uz = slicedata[i].Uz;
		newslicedata[j].Vx = slicedata[i].Vx;
		newslicedata[j].Vy = slicedata[i].Vy;
		newslicedata[j].Vz = slicedata[i].Vz;
		newslicedata[j].time = slicedata[i].time;
		newslicedata[j].number = slicedata[i].number;
		j++;
	} 

	/* Free old slicedata record and save new record */
	free((char *)pdiminfo1->patient);
	pdiminfo1->patient = newslicedata;
	newslicedata = (SLICEREC *)malloc((unsigned)pdimlen);
	if (newslicedata == NULL)
		Error("Could not allocate new pdim record");

	/* Loop through slices moving data to new slicedata record */
	slicedata = pdiminfo1->table;
	for (i=0; i<pdiminfo1->slicecnt; i++) {
		newslicedata[i].Ox = slicedata[i].Ox;
		newslicedata[i].Oy = slicedata[i].Oy;
		newslicedata[i].Oz = slicedata[i].Oz;
		newslicedata[i].Ux = slicedata[i].Ux;
		newslicedata[i].Uy = slicedata[i].Uy;
		newslicedata[i].Uz = slicedata[i].Uz;
		newslicedata[i].Vx = slicedata[i].Vx;
		newslicedata[i].Vy = slicedata[i].Vy;
		newslicedata[i].Vz = slicedata[i].Vz;
		newslicedata[i].time = slicedata[i].time;
		newslicedata[i].number = slicedata[i].number;
	} 

	/* Loop through slices appending data to new slicedata record */
	j = i;
	slicedata = pdiminfo2->table;
	for (i=0; i<pdiminfo2->slicecnt; i++) {
		newslicedata[j].Ox = slicedata[i].Ox;
		newslicedata[j].Oy = slicedata[i].Oy;
		newslicedata[j].Oz = slicedata[i].Oz;
		newslicedata[j].Ux = slicedata[i].Ux;
		newslicedata[j].Uy = slicedata[i].Uy;
		newslicedata[j].Uz = slicedata[i].Uz;
		newslicedata[j].Vx = slicedata[i].Vx;
		newslicedata[j].Vy = slicedata[i].Vy;
		newslicedata[j].Vz = slicedata[i].Vz;
		newslicedata[j].time = slicedata[i].time;
		newslicedata[j].number = slicedata[i].number;
		j++;
	} 

	/* Free old slicedata record and save new record */
	free((char *)pdiminfo1->table);
	pdiminfo1->table = newslicedata;
	pdiminfo1->slicecnt = pdiminfo1->slicecnt + pdiminfo2->slicecnt;

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to update the pdim record to     */
/*           reflect windowing in the U V W dimensions (pixel coordinates)   */
/*           and X Y Z dimensions (patient coordinates).  This will be       */
/*           useful to "greymap" and "extract".                              */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_window(PDIMREC *pdiminfo, char dimension, int low, int high)
{
	SLICEREC *slicedata;
	SLICEREC *newslicedata;
	int    i,j;
	int    pdimlen;

	/* Determine which dimension has been windowed */
	switch (dimension) {

		case 'u':
			/* Loop through slices adding low*(Ux,Uy,Uz) to the origin */
			slicedata = pdiminfo->patient;
			for (i=0; i<pdiminfo->slicecnt; i++) {
				slicedata[i].Ox = slicedata[i].Ox + low * slicedata[i].Ux;
				slicedata[i].Oy = slicedata[i].Oy + low * slicedata[i].Uy;
				slicedata[i].Oz = slicedata[i].Oz + low * slicedata[i].Uz;
			}
			slicedata = pdiminfo->table;
			for (i=0; i<pdiminfo->slicecnt; i++) {
				slicedata[i].Ox = slicedata[i].Ox + low * slicedata[i].Ux;
				slicedata[i].Oy = slicedata[i].Oy + low * slicedata[i].Uy;
				slicedata[i].Oz = slicedata[i].Oz + low * slicedata[i].Uz;
			}
			break;

		case 'v':
			/* Loop through slices adding low*(Vx,Vy,Vz) to the origin */
			slicedata = pdiminfo->patient;
			for (i=0; i<pdiminfo->slicecnt; i++) {
				slicedata[i].Ox = slicedata[i].Ox + low * slicedata[i].Vx;
				slicedata[i].Oy = slicedata[i].Oy + low * slicedata[i].Vy;
				slicedata[i].Oz = slicedata[i].Oz + low * slicedata[i].Vz;
			}
			slicedata = pdiminfo->table;
			for (i=0; i<pdiminfo->slicecnt; i++) {
				slicedata[i].Ox = slicedata[i].Ox + low * slicedata[i].Vx;
				slicedata[i].Oy = slicedata[i].Oy + low * slicedata[i].Vy;
				slicedata[i].Oz = slicedata[i].Oz + low * slicedata[i].Vz;
			}
			break;

		case 'w':
			/* Check that low and high are within the image */
			if ((low < 0) || (low  >= pdiminfo->slicecnt) ||
				(high < 0) || (high >= pdiminfo->slicecnt)) 
				Error("Slice index out of range");

			/* Allocate new slicedata record */
			pdimlen = sizeof(SLICEREC) * (high-low+1);
			newslicedata = (SLICEREC *)malloc((unsigned)pdimlen);
			if (newslicedata == NULL)
				Error("Could not allocate new pdim record");

			/* Loop through slices moving data to new slicedata record */
			j = 0;
			slicedata = pdiminfo->patient;
			for (i=low; i<=high; i++) {
				newslicedata[j].Ox = slicedata[i].Ox;
				newslicedata[j].Oy = slicedata[i].Oy;
				newslicedata[j].Oz = slicedata[i].Oz;
				newslicedata[j].Ux = slicedata[i].Ux;
				newslicedata[j].Uy = slicedata[i].Uy;
				newslicedata[j].Uz = slicedata[i].Uz;
				newslicedata[j].Vx = slicedata[i].Vx;
				newslicedata[j].Vy = slicedata[i].Vy;
				newslicedata[j].Vz = slicedata[i].Vz;
				newslicedata[j].time = slicedata[i].time;
				newslicedata[j].number = slicedata[i].number;
				j++;
			} 

			/* Free old slicedata record and save new record */
			free((char *)pdiminfo->patient);
			pdiminfo->patient = newslicedata;
			newslicedata = (SLICEREC *)malloc((unsigned)pdimlen);
			if (newslicedata == NULL) 
				Error("Could not allocate new pdim record");

			/* Loop through slices moving data to new slicedata record */
			j = 0;
			slicedata = pdiminfo->table;
			for (i=low; i<=high; i++) {
				newslicedata[j].Ox = slicedata[i].Ox;
				newslicedata[j].Oy = slicedata[i].Oy;
				newslicedata[j].Oz = slicedata[i].Oz;
				newslicedata[j].Ux = slicedata[i].Ux;
				newslicedata[j].Uy = slicedata[i].Uy;
				newslicedata[j].Uz = slicedata[i].Uz;
				newslicedata[j].Vx = slicedata[i].Vx;
				newslicedata[j].Vy = slicedata[i].Vy;
				newslicedata[j].Vz = slicedata[i].Vz;
				newslicedata[j].time = slicedata[i].time;
				newslicedata[j].number = slicedata[i].number;
				j++;
			} 

			/* Free old slicedata record and save new record */
			free((char *)pdiminfo->table);
			pdiminfo->table = newslicedata;
			pdiminfo->slicecnt = (high-low+1);
			break;

		case 'x':
		case 'y':
		case 'z':
		default:
			Error("Illegal dimension for window");
			break;
	}

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to update the pdim record to     */
/*           reflect scaling in the U V W dimensions (pixel coordinates)     */
/*           and X Y Z dimensions (patient coordinates).  This will be       */
/*           useful to "interp" and "ds_rdscans".                            */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_scale(PDIMREC *pdiminfo, char field, char dimension, float scale,
	int low, int high)
{
	SLICEREC *slicedata;
	int    i;

	/* Check that low and high are within the image */
	if ((low < 0) || (low  >= pdiminfo->slicecnt) ||
		(high < 0) || (high >= pdiminfo->slicecnt)) 
		Error("Slice index out of range");

	/* Get patient or table map, depending on field value */
	if (field == 'p')
		slicedata = pdiminfo->patient;
	else
		slicedata = pdiminfo->table;

	/* Determine which dimension has been scaled */
	switch (dimension) {

		case 'u':
			/* Loop through slices scaling the (Ux,Uy,Uz) vector */
			for (i=low; i<=high; i++) {
				slicedata[i].Ux = slicedata[i].Ux * scale;
				slicedata[i].Uy = slicedata[i].Uy * scale;
				slicedata[i].Uz = slicedata[i].Uz * scale;
			}
			break;

		case 'v':
			/* Loop through slices scaling the (Vx,Vy,Vz) vector */
			for (i=low; i<=high; i++) {
				slicedata[i].Vx = slicedata[i].Vx * scale;
				slicedata[i].Vy = slicedata[i].Vy * scale;
				slicedata[i].Vz = slicedata[i].Vz * scale;
			}
			break;

		case 'x':
			/* Loop through slices scaling the X componants */     
			for (i=low; i<=high; i++) {
				slicedata[i].Ux = slicedata[i].Ux * scale;
				slicedata[i].Vx = slicedata[i].Vx * scale;
				slicedata[i].Ox = slicedata[i].Ox * scale;
			}
			break;

		case 'y':
			/* Loop through slices scaling the Y componants */     
			for (i=low; i<=high; i++) {
				slicedata[i].Uy = slicedata[i].Uy * scale;
				slicedata[i].Vy = slicedata[i].Vy * scale;
				slicedata[i].Oy = slicedata[i].Oy * scale;
			}
			break;

		case 'z':
			/* Loop through slices scaling the Z componants */     
			for (i=low; i<=high; i++) {
				slicedata[i].Uz = slicedata[i].Uz * scale;
				slicedata[i].Vz = slicedata[i].Vz * scale;
				slicedata[i].Oz = slicedata[i].Oz * scale;
			}
			break;

		case 'w':
		default:
			/* Other dimensions not handled by scale */
			Error("Illegal dimension for scale");
			break;
	}

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to update the pdim record to     */
/*           reflect rotation in the U V W dimensions (pixel coordinates)    */
/*           or X Y Z dimensions (patient coordinates).  This will be        */
/*           useful to "ds_rdscans".                                         */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_rotate(PDIMREC *pdiminfo, char field, char dimension, float angle,
	int low, int high)
{
	SLICEREC *slicedata;
	int    i;
	float cosangle, sinangle;
	float X,Y,Z;

	/* Check that low and high are within the image */
	if ((low < 0) || (low  >= pdiminfo->slicecnt) ||
		(high < 0) || (high >= pdiminfo->slicecnt)) 
		Error("Slice index out of range");

	/* Get patient or table map, depending on field value */
	if (field == 'p')
		slicedata = pdiminfo->patient;
	else
		slicedata = pdiminfo->table;

	/* Convert to radians and calculate (float) sin and (float) cos of angle */
	angle = angle * DEG2RAD;
	cosangle = (float) cos(angle);
	sinangle = (float) sin(angle);

	/* Determine which dimension has been rotated */
	switch (dimension) {

		case 'x':
			/* Loop through slices rotating about X axis */         
			for (i=low; i<=high; i++) {
				Y = slicedata[i].Oy;
				Z = slicedata[i].Oz;
				slicedata[i].Oy =  Y*cosangle + Z*sinangle;
				slicedata[i].Oz = -Y*sinangle + Z*cosangle;
				Y = slicedata[i].Uy;
				Z = slicedata[i].Uz;
				slicedata[i].Uy =  Y*cosangle + Z*sinangle;
				slicedata[i].Uz = -Y*sinangle + Z*cosangle;
				Y = slicedata[i].Vy;
				Z = slicedata[i].Vz;
				slicedata[i].Vy =  Y*cosangle + Z*sinangle;
				slicedata[i].Vz = -Y*sinangle + Z*cosangle;
			}
			break;

		case 'y':
			/* Loop through slices rotating about Y axis */         
			for (i=low; i<=high; i++) {
				Z = slicedata[i].Oz;
				X = slicedata[i].Ox;
				slicedata[i].Oz =  Z*cosangle + X*sinangle;
				slicedata[i].Ox = -Z*sinangle + X*cosangle;
				Z = slicedata[i].Uz;
				X = slicedata[i].Ux;
				slicedata[i].Uz =  Z*cosangle + X*sinangle;
				slicedata[i].Ux = -Z*sinangle + X*cosangle;
				Z = slicedata[i].Vz;
				X = slicedata[i].Vx;
				slicedata[i].Vz =  Z*cosangle + X*sinangle;
				slicedata[i].Vx = -Z*sinangle + X*cosangle;
			}
			break;

		case 'z':
			/* Loop through slices rotating about Z axis */         
			for (i=low; i<=high; i++) {
				X = slicedata[i].Ox;
				Y = slicedata[i].Oy;
				slicedata[i].Ox =  X*cosangle + Y*sinangle;
				slicedata[i].Oy = -X*sinangle + Y*cosangle;
				X = slicedata[i].Ux;
				Y = slicedata[i].Uy;
				slicedata[i].Ux =  X*cosangle + Y*sinangle;
				slicedata[i].Uy = -X*sinangle + Y*cosangle;
				X = slicedata[i].Vx;
				Y = slicedata[i].Vy;
				slicedata[i].Vx =  X*cosangle + Y*sinangle;
				slicedata[i].Vy = -X*sinangle + Y*cosangle;
			}
			break;

		case 'u':
		case 'v':
		case 'w':
		default:
			Error("Illegal dimension for rotate");
			break;
	}

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to update the pdim record to     */
/*           reflect translaion in the U V W dimensions (pixel coordinates)  */
/*           or X Y Z dimensions (patient coordinates).  This will be        */
/*           useful to "ds_rdscans".                                         */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_translate(PDIMREC *pdiminfo, char field, char dimension, float dist,
	int low, int high)
{
	SLICEREC *slicedata;
	int    i;

	/* Check that low and high are within the image */
	if ((low < 0) || (low  >= pdiminfo->slicecnt) ||
		(high < 0) || (high >= pdiminfo->slicecnt))
		Error("Slice index out of range");

	/* Get patient or table map, depending on field value */
	if (field == 'p')
		slicedata = pdiminfo->patient;
	else
		slicedata = pdiminfo->table;

	/* Determine which dimension has been translated */
	switch (dimension) {

		case 'x':
			/* Loop through slices translating along X axis */         
			for (i=low; i<=high; i++) {
				slicedata[i].Ox = slicedata[i].Ox + dist;
			}
			break;

		case 'y':
			/* Loop through slices translating along Y axis */         
			for (i=low; i<=high; i++) {
				slicedata[i].Oy = slicedata[i].Oy + dist;
			}
			break;

		case 'z':
			/* Loop through slices translating along Z axis */         
			for (i=low; i<=high; i++) {
				slicedata[i].Oz = slicedata[i].Oz + dist;
			}
			break;

		case 'u':
			/* Loop through slices translating along U axis */         
			for (i=low; i<=high; i++) {
				slicedata[i].Ox = slicedata[i].Ox + dist * slicedata[i].Ux;
				slicedata[i].Oy = slicedata[i].Oy + dist * slicedata[i].Uy;
				slicedata[i].Oz = slicedata[i].Oz + dist * slicedata[i].Uz;
			}
			break;

		case 'v':
			/* Loop through slices translating along V axis */         
			for (i=low; i<=high; i++) {
				slicedata[i].Ox = slicedata[i].Ox + dist * slicedata[i].Vx;
				slicedata[i].Oy = slicedata[i].Oy + dist * slicedata[i].Vy;
				slicedata[i].Oz = slicedata[i].Oz + dist * slicedata[i].Vz;
			}
			break;

		case 'w':
		default:
			Error("Illegal dimension for translate");
			break;
	}

	/* Return to calling routine */
	return(VALID);
}

/*---------------------------------------------------------------------------*/
/*                                                                           */
/* Purpose:  The purpose of this routine is to map image coordinates         */
/*           (u,v,w) to patient coordinates (x,y,z,t).  We also check that   */
/*           the pixel is within the image.                                  */
/*                                                                           */
/* Note:     This routine can be made even faster using fancy addressing.    */
/*                                                                           */
/*---------------------------------------------------------------------------*/
int pdim_map(PDIMREC *pdiminfo, char field, int u, int v, int w,
	float *x, float *y, float *z, float *t)
{
	SLICEREC *slicedata;

	/* Check slice number with bounds */
	if ((w < 0) || (w >= pdiminfo->slicecnt)) 
		Error("Slice index out of range");

	/* Get patient or table map, depending on field value */
	if (field == 'p')
		slicedata = pdiminfo->patient;
	else
		slicedata = pdiminfo->table;

	/* Calculate (x,y,z) = (Ox,Oy,Oz) + u*(Ux,Uy,Uz) + v*(Vx,Vy,Vz) */
	*x = slicedata[w].Ox + u*slicedata[w].Ux + v*slicedata[w].Vx;
	*y = slicedata[w].Oy + u*slicedata[w].Uy + v*slicedata[w].Vy;
	*z = slicedata[w].Oz + u*slicedata[w].Uz + v*slicedata[w].Vz;
	*t = slicedata[w].time;

	/* Return to calling routine */
	return(VALID);
}
