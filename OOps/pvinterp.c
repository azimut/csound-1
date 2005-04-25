/*
    pvinterp.c:

    Copyright (C) 1996 Richard Karpen

    This file is part of Csound.

    The Csound Library is free software; you can redistribute it
    and/or modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    Csound is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with Csound; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307 USA
*/

/**************************************************************/
/***********PVINTERP, PVCROSS************/
/*** By Richard Karpen 1996************/
/************************************************************/
#include <math.h>
#include "cs.h"
#include "dsputil.h"
#include "pvoc.h"
#include "pvinterp.h"
#include "soundio.h"
#include "oload.h"

static  int     pdebug = 0;
static  PVBUFREAD       *pvbufreadaddr;


#define WLN   1         /* time window is WLN*2*ksmps long */
#define OPWLEN (2*WLN*csound->ksmps)    /* manifest used for final time wdw */

/************************************************************/
/*************PVBUFREAD**************************************/
/************************************************************/


int pvbufreadset(ENVIRON *csound, PVBUFREAD *p)
{
    char     pvfilnam[MAXNAME];
    MEMFIL   *mfp;
    PVSTRUCT *pvh;
    int      frInc, chans, size; /* THESE SHOULD BE SAVED IN PVOC STRUCT */

    pvbufreadaddr=p;

    if (p->auxch.auxp == NULL) {              /* if no buffers yet, alloc now */
      MYFLT *fltp;
      csound->AuxAlloc(csound, (long)(PVDATASIZE + PVFFTSIZE*3 + PVWINLEN) * sizeof(MYFLT),
               &p->auxch);
      fltp = (MYFLT *) p->auxch.auxp;
      p->lastPhase = fltp;   fltp += PVDATASIZE;    /* and insert addresses */
      p->fftBuf = fltp;       /* fltp += PVFFTSIZE; */ /* Not needed */
    }

    csound->strarg2name(csound, pvfilnam, p->ifilno, "pvoc.", p->XINSTRCODE);
    if ((mfp = p->mfp) == NULL ||
        strcmp(mfp->filename, pvfilnam) != 0) { /* if file not already readin */
      if ( (mfp = ldmemfile(csound, pvfilnam)) == NULL) {
        sprintf(csound->errmsg, Str("PVOC cannot load %s"), pvfilnam);
        goto pverr;
      }
    }
    pvh = (PVSTRUCT *)mfp->beginp;
    if (pvh->magic != PVMAGIC) {
      sprintf(csound->errmsg, Str("%s not a PVOC file (magic %ld)"),
                              pvfilnam, pvh->magic);
      goto pverr;
    }
    p->frSiz = pvh->frameSize;
    frInc    = pvh->frameIncr;
    chans    = pvh->channels;
    if ((p->asr = pvh->samplingRate) != csound->esr &&
        (O.msglevel & WARNMSG)) { /* & chk the data */
      printf(Str("WARNING: %s''s srate = %8.0f, orch's srate = %8.0f\n"),
             pvfilnam, p->asr, csound->esr);
    }
    if (pvh->dataFormat != PVMYFLT) {
      sprintf(csound->errmsg, Str("unsupported PVOC data format %ld in %s"),
                              pvh->dataFormat, pvfilnam);
      goto pverr;
    }
    if (p->frSiz > PVFRAMSIZE) {
      sprintf(csound->errmsg, Str("PVOC frame %d bigger than %ld in %s"),
                              p->frSiz, PVFRAMSIZE, pvfilnam);
      goto pverr;
    }
    if (p->frSiz < 128) {
      sprintf(csound->errmsg, Str("PVOC frame %ld seems too small in %s"),
                              p->frSiz, pvfilnam);
      goto pverr;
    }
    if (chans != 1) {
      sprintf(csound->errmsg, Str("%d chans (not 1) in PVOC file %s"),
                              chans, pvfilnam);
      goto pverr;
    }
    p->frPtr = (MYFLT *) ((char *)pvh+pvh->headBsize);
    p->maxFr = -1 + ( pvh->dataBsize / (chans * (p->frSiz+2) * sizeof(MYFLT)));
    p->frPktim = ((MYFLT)csound->ksmps)/((MYFLT)frInc);
    p->frPrtim = csound->esr/((MYFLT)frInc);
    size = pvfrsiz(p);          /* size used in def of OPWLEN ? */
    p->prFlg = 1;    /* true */

    if ((OPWLEN/2 + 1)>PVWINLEN ) {
      sprintf(csound->errmsg,
              Str("ksmps of %d needs wdw of %d, max is %d for pv %s\n"),
              csound->ksmps, (OPWLEN/2 + 1), PVWINLEN, pvfilnam);
      goto pverr;
    }

    return OK;

 pverr:
    return csound->InitError(csound, csound->errmsg);
}


int pvbufread(ENVIRON *csound, PVBUFREAD *p)
{
    MYFLT  frIndx;
    MYFLT  *buf = p->fftBuf;
    int    size = pvfrsiz(p);

    if (p->auxch.auxp==NULL) {  /* RWD fix */
      return csound->PerfError(csound, Str("pvbufread: not initialised"));
    }
    if (pdebug)  fprintf(stderr, "<%7.4f>",*p->ktimpnt);
    if ((frIndx = *p->ktimpnt * p->frPrtim) < 0) {
      return csound->PerfError(csound, Str("PVOC timpnt < 0"));
    }
    if (frIndx > (MYFLT)p->maxFr) {  /* not past last one */
      frIndx = (MYFLT)p->maxFr;
      if (p->prFlg) {
        p->prFlg = 0;   /* false */
        if (O.msglevel & WARNMSG)
          printf(Str("WARNING: PVOC ktimpnt truncated to last frame"));
      }
    }
    FetchIn(p->frPtr,buf,size,frIndx);

    p->buf=buf;
    return OK;
}


/************************************************************/
/*************PVINTERP**************************************/
/************************************************************/
int pvinterpset(ENVIRON *csound, PVINTERP *p)
{
    int      i;
    char     pvfilnam[MAXNAME];
    MEMFIL   *mfp;
    PVSTRUCT *pvh;
    int      frInc, chans, size; /* THESE SHOULD BE SAVED IN PVOC STRUCT */

    p->pvbufread = pvbufreadaddr;

    if (p->auxch.auxp == NULL) {              /* if no buffers yet, alloc now */
      MYFLT *fltp;
      csound->AuxAlloc(csound, (long)(PVDATASIZE + PVFFTSIZE*3 + PVWINLEN) * sizeof(MYFLT),
               &p->auxch);
      fltp = (MYFLT *) p->auxch.auxp;
      p->lastPhase = fltp;   fltp += PVDATASIZE;    /* and insert addresses */
      p->fftBuf = fltp;      fltp += PVFFTSIZE;
      p->dsBuf = fltp;       fltp += PVFFTSIZE;
      p->outBuf = fltp;      fltp += PVFFTSIZE;
      p->window = fltp;
    }
    csound->strarg2name(csound, pvfilnam, p->ifilno, "pvoc.", p->XINSTRCODE);
    if ((mfp = p->mfp) == NULL ||
        strcmp(mfp->filename, pvfilnam) != 0) { /* if file not already readin */
      if ( (mfp = ldmemfile(csound, pvfilnam)) == NULL) {
        sprintf(csound->errmsg, Str("PVOC cannot load %s"), pvfilnam);
        goto pverr;
      }
    }
    pvh = (PVSTRUCT *)mfp->beginp;
    if (pvh->magic != PVMAGIC) {
      sprintf(csound->errmsg, Str("%s not a PVOC file (magic %ld)"),
                              pvfilnam, pvh->magic);
      goto pverr;
    }
    p->frSiz = pvh->frameSize;
    frInc    = pvh->frameIncr;
    chans    = pvh->channels;
    if ((p->asr = pvh->samplingRate) != csound->esr &&
        (O.msglevel & WARNMSG)) { /* & chk the data */
      printf(Str("WARNING: %s''s srate = %8.0f, orch's srate = %8.0f\n"),
             pvfilnam, p->asr, csound->esr);
    }
    if (pvh->dataFormat != PVMYFLT) {
      sprintf(csound->errmsg, Str("unsupported PVOC data format %ld in %s"),
                              pvh->dataFormat, pvfilnam);
      goto pverr;
    }
    if (p->frSiz > PVFRAMSIZE) {
      sprintf(csound->errmsg, Str("PVOC frame %d bigger than %ld in %s"),
                              p->frSiz, PVFRAMSIZE, pvfilnam);
      goto pverr;
    }
    if (p->frSiz < PVFRAMSIZE/8) {
      sprintf(csound->errmsg, Str("PVOC frame %ld seems too small in %s"),
                              p->frSiz, pvfilnam);
      goto pverr;
    }
    if (chans != 1) {
      sprintf(csound->errmsg, Str("%d chans (not 1) in PVOC file %s"),
                              chans, pvfilnam);
      goto pverr;
    }
    /* Check that pv->frSiz is a power of two too ? */
    p->frPtr = (MYFLT *) ((char *)pvh+pvh->headBsize);
    p->baseFr = 0;  /* point to first data frame */
    p->maxFr = -1 + ( pvh->dataBsize / (chans * (p->frSiz+2) * sizeof(MYFLT)));
    /* highest possible frame index */
    p->frPktim = ((MYFLT)csound->ksmps)/((MYFLT)frInc);
    /* factor by which to mult expand phase diffs (ratio of samp spacings) */
    p->frPrtim = csound->esr/((MYFLT)frInc);
    /* factor by which to mulitply 'real' time index to get frame index */
    size = pvfrsiz(p);          /* size used in def of OPWLEN ? */
    p->scale = csound->e0dbfs * FL(2.0)*((MYFLT)csound->ksmps)/((MYFLT)OPWLEN);
    p->scale *= csound->GetInverseRealFFTScale(csound, (int) size);
    /* 2*incr/OPWLEN scales down for win ovlp, windo'd 1ce (but 2ce?) */
    /* 1/frSiz is the required scale down before (i)FFT */
    p->prFlg = 1;    /* true */
    p->opBpos = 0;
    p->lastPex = FL(1.0);    /* needs to know last pitchexp to update phase */
    /* Set up time window */
    for (i=0; i < pvdasiz(p); ++i) {  /* or maybe pvdasiz(p) */
      p->lastPhase[i] = FL(0.0);
    }
    if ((OPWLEN/2 + 1)>PVWINLEN) {
      sprintf(csound->errmsg,
              Str("ksmps of %d needs wdw of %d, max is %d for pv %s\n"),
              csound->ksmps, (OPWLEN/2 + 1), PVWINLEN, pvfilnam);
      goto pverr;
    }
    for (i=0; i < OPWLEN/2+1; ++i)    /* time window is OPWLEN long */
      p->window[i] = (FL(0.54)-
                      FL(0.46)*(MYFLT)cos(TWOPI*(MYFLT)i/(MYFLT)OPWLEN));
    /* NB : HAMMING */
    for (i=0; i< pvfrsiz(p); ++i)
      p->outBuf[i] = FL(0.0);
    MakeSinc( /* p->sncTab */ );        /* sinctab is same for all instances */

    return OK;

 pverr:
    return csound->InitError(csound, csound->errmsg);
}


int pvinterp(ENVIRON *csound, PVINTERP *p)
{
    MYFLT  *ar = p->rslt;
    MYFLT  frIndx;
    MYFLT  *buf = p->fftBuf;
    MYFLT  *buf2 = p->dsBuf;
    int    asize = pvdasiz(p); /* fix */
    int    size = pvfrsiz(p);
    int    buf2Size, outlen;
    int    circBufSize = PVFFTSIZE;
   /* int    specwp = (int)*p->ispecwp; */  /* spectral warping flag */
    MYFLT  pex;
    PVBUFREAD *q = p->pvbufread;
    long         i,j ;

    if (p->auxch.auxp==NULL) {  /* RWD Fix */
      return csound->PerfError(csound, Str("pvinterp: not initialised"));
    }
    if (pdebug) fprintf(stderr, "<%7.4f>",*p->ktimpnt);
    pex = *p->kfmod;
    outlen = (int)(((MYFLT)size)/pex);
    /* use outlen to check window/krate/transpose combinations */
    if (outlen>PVFFTSIZE) { /* Maximum transposition down is one octave */
                            /* ..so we won't run into buf2Size problems */
      return csound->PerfError(csound, Str("PVOC transpose too low"));
    }
    if (outlen<2*csound->ksmps) {   /* minimum post-squeeze windowlength */
      return csound->PerfError(csound, Str("PVOC transpose too high"));
    }
    buf2Size = OPWLEN;     /* always window to same length after DS */
    if ((frIndx = *p->ktimpnt * p->frPrtim) < 0) {
      return csound->PerfError(csound, Str("PVOC timpnt < 0"));
    }
    if (frIndx > (MYFLT)p->maxFr) { /* not past last one */
      frIndx = (MYFLT)p->maxFr;
      if (p->prFlg) {
        p->prFlg = 0;   /* false */
        if (O.msglevel & WARNMSG)
          printf(Str("WARNING: PVOC ktimpnt truncated to last frame"));
      }
    }
    FetchIn(p->frPtr,buf,size,frIndx);

/* Here's where the interpolation happens ***********************/
    for (i=0, j=1; i<=size; i+=2, j+=2) {
      buf[i] = buf[i] * *p->kampscale2;
      q->buf[i] = q->buf[i] * *p->kampscale1;
      buf[j] = buf[j] * *p->kfreqscale2;
      q->buf[j] = q->buf[j] * *p->kfreqscale1;
      buf[i] = (buf[i]  + ((q->buf[i]-buf[i]) * *p->kampinterp)) * p->scale;
      buf[j] = (buf[j]  + ((q->buf[j]-buf[j]) * *p->kfreqinterp));
    }
/*******************************************************************/
    FrqToPhase(buf, asize, pex*(MYFLT)csound->ksmps, p->asr,
           /*a0.0*/(MYFLT)(.5 * ( (pex / p->lastPex) - 1) ));
    /* Offset the phase to align centres of stretched windows, not starts */
    RewrapPhase(buf,asize,p->lastPhase);
    Polar2Rect(buf,size);
    buf[1] = buf[size]; buf[size] = buf[size+1] = FL(0.0);
    csound->InverseRealFFT(csound, buf, (int) size);
    if (pex != 1.0)
      UDSample(buf,(FL(0.5)*((MYFLT)size - pex*(MYFLT)buf2Size))/*a*/,buf2,
               size, buf2Size, pex);
    else
      CopySamps(buf+(int)(FL(0.5)*((MYFLT)size - pex*(MYFLT)buf2Size))/*a*/,buf2,
                buf2Size);
    ApplyHalfWin(buf2, p->window, buf2Size);        /* */

    addToCircBuf(buf2, p->outBuf, p->opBpos, csound->ksmps, circBufSize);
    writeClrFromCircBuf(p->outBuf, ar, p->opBpos, csound->ksmps, circBufSize);
    p->opBpos += csound->ksmps;
    if (p->opBpos > circBufSize)
      p->opBpos -= circBufSize;
    addToCircBuf(buf2+csound->ksmps,p->outBuf,p->opBpos,buf2Size-csound->ksmps,circBufSize);
    p->lastPex = pex;        /* needs to know last pitchexp to update phase */
    return OK;
}



/************************************************************/
/*************PVCROSS**************************************/
/************************************************************/
int pvcrossset(ENVIRON *csound, PVCROSS *p)
{
    int      i;
    char     pvfilnam[MAXNAME];
    MEMFIL   *mfp;
    PVSTRUCT *pvh;
    int      frInc, chans, size; /* THESE SHOULD BE SAVED IN PVOC STRUCT */

    p->pvbufread = pvbufreadaddr;

    if (p->auxch.auxp == NULL) {              /* if no buffers yet, alloc now */
        MYFLT *fltp;
        csound->AuxAlloc(csound, (long)(PVDATASIZE + PVFFTSIZE*3 + PVWINLEN) * sizeof(MYFLT),
                 &p->auxch);
        fltp = (MYFLT *) p->auxch.auxp;
        p->lastPhase = fltp;   fltp += PVDATASIZE;    /* and insert addresses */
        p->fftBuf = fltp;      fltp += PVFFTSIZE;
        p->dsBuf = fltp;       fltp += PVFFTSIZE;
        p->outBuf = fltp;      fltp += PVFFTSIZE;
        p->window = fltp;
    }
    csound->strarg2name(csound, pvfilnam, p->ifilno, "pvoc.", p->XINSTRCODE);
    if ((mfp = p->mfp) == NULL ||
        strcmp(mfp->filename, pvfilnam) != 0) {/* if file not already readin */
        if ( (mfp = ldmemfile(csound, pvfilnam)) == NULL) {
            sprintf(csound->errmsg, Str("PVOC cannot load %s"), pvfilnam);
            goto pverr;
        }
    }
    pvh = (PVSTRUCT *)mfp->beginp;
    if (pvh->magic != PVMAGIC) {
        sprintf(csound->errmsg, Str("%s not a PVOC file (magic %ld)"),
                                pvfilnam, pvh->magic);
        goto pverr;
    }
    p->frSiz = pvh->frameSize;
    frInc    = pvh->frameIncr;
    chans    = pvh->channels;
    if ((p->asr = pvh->samplingRate) != csound->esr &&
        (O.msglevel & WARNMSG)) { /* & chk the data */
      printf(Str("WARNING: %s''s srate = %8.0f, orch's srate = %8.0f\n"),
             pvfilnam, p->asr, csound->esr);
    }
    if (pvh->dataFormat != PVMYFLT) {
        sprintf(csound->errmsg, Str("unsupported PVOC data format %ld in %s"),
                                pvh->dataFormat, pvfilnam);
        goto pverr;
    }
    if (p->frSiz > PVFRAMSIZE) {
        sprintf(csound->errmsg, Str("PVOC frame %d bigger than %ld in %s"),
                                p->frSiz, PVFRAMSIZE, pvfilnam);
        goto pverr;
    }
    if (p->frSiz < PVFRAMSIZE/8) {
        sprintf(csound->errmsg, Str("PVOC frame %ld seems too small in %s"),
                                p->frSiz, pvfilnam);
        goto pverr;
    }
    if (chans != 1) {
        sprintf(csound->errmsg, Str("%d chans (not 1) in PVOC file %s"),
                                chans, pvfilnam);
        goto pverr;
    }
    /* Check that pv->frSiz is a power of two too ? */
    p->frPtr = (MYFLT *) ((char *)pvh+pvh->headBsize);
    p->baseFr = 0;  /* point to first data frame */
    p->maxFr = -1 + ( pvh->dataBsize / (chans * (p->frSiz+2) * sizeof(MYFLT) ) );
    /* highest possible frame index */
    p->frPktim = ((MYFLT)csound->ksmps)/((MYFLT)frInc);
    /* factor by which to mult expand phase diffs (ratio of samp spacings) */
    p->frPrtim = csound->esr/((MYFLT)frInc);
    /* factor by which to mulitply 'real' time index to get frame index */
    size = pvfrsiz(p);          /* size used in def of OPWLEN ? */
    p->scale = FL(2.0) * csound->e0dbfs*((MYFLT)csound->ksmps)/((MYFLT)OPWLEN);
    p->scale *= csound->GetInverseRealFFTScale(csound, (int) size);
    p->prFlg = 1;    /* true */
    p->opBpos = 0;
    p->lastPex = FL(1.0);   /* needs to know last pitchexp to update phase */
    /* Set up time window */
    for (i=0; i < pvdasiz(p); ++i) {  /* or maybe pvdasiz(p) */
        p->lastPhase[i] = FL(0.0);
    }
    if ((OPWLEN/2 + 1)>PVWINLEN ) {
        sprintf(csound->errmsg,
                Str("ksmps of %d needs wdw of %d, max is %d for pv %s\n"),
                csound->ksmps, (OPWLEN/2 + 1), PVWINLEN, pvfilnam);
        goto pverr;
    }
    for (i=0; i < OPWLEN/2+1; ++i)    /* time window is OPWLEN long */
        p->window[i] = (FL(0.54)-FL(0.46)*(MYFLT)cos(TWOPI*(MYFLT)i/(MYFLT)OPWLEN));
    /* NB : HAMMING */
    for (i=0; i< pvfrsiz(p); ++i)
        p->outBuf[i] = FL(0.0);
    MakeSinc( /* p->sncTab */ );        /* sinctab is same for all instances */

    return OK;

pverr:
    return csound->InitError(csound, csound->errmsg);
}


int pvcross(ENVIRON *csound, PVCROSS *p)
{
    int    n;
    MYFLT  *ar = p->rslt;
    MYFLT  frIndx;
    MYFLT  *buf = p->fftBuf;
    MYFLT  *buf2 = p->dsBuf;
    int    asize = pvdasiz(p); /* fix */
    int    size = pvfrsiz(p);
    int    buf2Size, outlen;
    int    circBufSize = PVFFTSIZE;
    int    specwp = (int)*p->ispecwp;   /* spectral warping flag */
    MYFLT  pex;
    PVBUFREAD *q = p->pvbufread;
    long   i,j;
    MYFLT  ampscale1=*p->kampscale1;
    MYFLT  ampscale2=*p->kampscale2;

    if (p->auxch.auxp==NULL) {  /* RWD Fix */
      return csound->PerfError(csound, Str("pvcross: not initialised"));
    }
    if (pdebug) fprintf(stderr, "<%7.4f>",*p->ktimpnt);
    pex = *p->kfmod;
    outlen = (int)(((MYFLT)size)/pex);
    /* use outlen to check window/krate/transpose combinations */
    if (outlen>PVFFTSIZE) { /* Maximum transposition down is one octave */
                            /* ..so we won't run into buf2Size problems */
      return csound->PerfError(csound, Str("PVOC transpose too low"));
    }
    if (outlen<2*csound->ksmps) {  /* minimum post-squeeze windowlength */
      return csound->PerfError(csound, Str("PVOC transpose too high"));
    }
    buf2Size = OPWLEN;     /* always window to same length after DS */
    if ((frIndx = *p->ktimpnt * p->frPrtim) < 0) {
      return csound->PerfError(csound, Str("PVOC timpnt < 0"));
    }
    if (frIndx > (MYFLT)p->maxFr) { /* not past last one */
        frIndx = (MYFLT)p->maxFr;
        if (p->prFlg) {
            p->prFlg = 0;   /* false */
            if (O.msglevel & WARNMSG)
              printf(Str("WARNING: PVOC ktimpnt truncated to last frame"));
        }
    }

    FetchIn(p->frPtr,buf,size,frIndx);

/**** Apply amplitudes from pvbufread ********/
    for (i=0, j=0; i<=size; i+=2, j++)
      buf[i] = ((buf[i] * ampscale2) + (q->buf[i] * ampscale1)) * p->scale;
/***************************************************/

    FrqToPhase(buf, asize, pex*(MYFLT)csound->ksmps, p->asr,
               /*a0.0*/(MYFLT)(.5 * ( (pex / p->lastPex) - 1) ));
    /* Offset the phase to align centres of stretched windows, not starts */
    RewrapPhase(buf,asize,p->lastPhase);
/**/if (specwp == 0 || (p->prFlg)++ == -(int)specwp) /* ?screws up when prFlg used */
  { /* specwp=0 => normal; specwp = -n => just nth frame */
    if (specwp<0) printf(Str("PVOC debug : one frame gets through \n"));
    if (specwp>0)
        PreWarpSpec(buf, asize, pex);
    Polar2Rect(buf,size);

    buf[1] = buf[size]; buf[size] =  buf[size + 1] = FL(0.0);
    csound->InverseRealFFT(csound, buf, (int) size);
    if (pex != 1.0)
      UDSample(buf,(FL(0.5)*((MYFLT)size - pex*(MYFLT)buf2Size))/*a*/,buf2,
               size, buf2Size, pex);
    else
      CopySamps(buf+(int)(FL(0.)*((MYFLT)size - pex*(MYFLT)buf2Size))/*a*/,buf2,
                buf2Size);
/*a*/    if (specwp>=0) ApplyHalfWin(buf2, p->window, buf2Size);        /* */
/**/      }
    else
        for (n = 0; n<buf2Size; ++n)
          buf2[n] = FL(0.0);            /*      */
    addToCircBuf(buf2, p->outBuf, p->opBpos, csound->ksmps, circBufSize);
    writeClrFromCircBuf(p->outBuf, ar, p->opBpos, csound->ksmps, circBufSize);
    p->opBpos += csound->ksmps;
    if (p->opBpos > circBufSize)     p->opBpos -= circBufSize;
    addToCircBuf(buf2+csound->ksmps,p->outBuf,p->opBpos,buf2Size-csound->ksmps,circBufSize);
    p->lastPex = pex;        /* needs to know last pitchexp to update phase */
    return OK;
}

