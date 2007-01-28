/* main() for emitting sequences from a profile HMM.
 * 
 * SRE, Tue Jan  9 13:22:53 2007 [Janelia] [Verdi, Requiem]
 * SVN $Id$
 */
#include "p7_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_dmatrix.h"
#include "esl_getopts.h"
#include "esl_random.h"
#include "esl_sqio.h"
#include "esl_vectorops.h"

#include "hmmer.h"

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range     toggles      reqs   incomp  help   docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,      NULL,      NULL,    NULL, "show brief help on version and usage",      0 },
  { "-n",        eslARG_INT,      "1", NULL, "n>0",     NULL,      NULL,    NULL, "number of seqs to sample",                  0 },
  { "-p",        eslARG_NONE,   FALSE, NULL, NULL,      NULL,      NULL,    NULL, "sample from profile, not core model",       0 },
  { "-L",        eslARG_INT,    "400", NULL, NULL,      NULL,      "-p",    NULL, "set expected length from profile to <n>",   0 },
  { "--h2",      eslARG_NONE,    NULL, NULL, NULL,      NULL,      "-p",    NULL, "configure profile in old HMMER2 style",     0 },
  { "--kpsfile", eslARG_OUTFILE, NULL, NULL, NULL,      NULL,      NULL,    NULL, "output PostScript mx of k endpoints to <f>",0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static char usage[]  = "hmmemit [-options] <hmmfile (single)>";

struct emitcfg_s {
  int   nseq;			/* how many sequences to emit */
  int   do_profile;		/* TRUE to emit from implicit profile model, not core model */
  int   do_oldconfig;		/* TRUE to do old-style HMMER2 entry/exit configuration */
  int   L;			/* expected length from a profile */
  char *kpsfile;		/* postscript output matrix showing k endpoint distribution */
  FILE *kpsfp;			/* open kpsfile */
  ESL_DMATRIX *kmx;		/* k endpoint frequencies */
};


static int  collect_model_endpoints(P7_TRACE *tr, ESL_DMATRIX *D);
static int  p7_H2_ProfileEmit(ESL_RANDOMNESS *r, P7_PROFILE *gm, ESL_SQ *sq, P7_TRACE *tr);

int
main(int argc, char **argv)
{
  struct emitcfg_s cfg;		       /* command-line configurations             */
  int              status;	       /* status of a function call               */
  ESL_ALPHABET    *abc     = NULL;     /* sequence alphabet                       */
  ESL_GETOPTS     *go      = NULL;     /* command line processing                 */
  ESL_RANDOMNESS  *r       = NULL;     /* source of randomness                    */
  char            *hmmfile = NULL;     /* file to read HMM(s) from                */
  P7_HMMFILE      *hfp     = NULL;     /* open hmmfile                            */
  P7_HMM          *hmm     = NULL;     /* HMM to emit from                        */
  P7_TRACE        *tr      = NULL;     /* sampled trace                           */
  ESL_SQ          *sq      = NULL;     /* sampled digital sequence                */
  char             sqname[64];
  int              nseq;
  int              fmt;

  /*****************************************************************
   * Parse the command line
   *****************************************************************/
  fmt = eslSQFILE_FASTA;

  go = esl_getopts_Create(options, usage);
  esl_opt_ProcessCmdline(go, argc, argv);
  esl_opt_VerifyConfig(go);
  if (esl_opt_IsSet(go, "-h")) {
    puts(usage);
    puts("\n  where options are:\n");
    esl_opt_DisplayHelp(stdout, go, 0, 2, 80); /* 0=all docgroups; 2 = indentation; 80=textwidth*/
    return eslOK;
  }

  esl_opt_GetIntegerOption(go, "-n",        &(cfg.nseq));
  esl_opt_GetBooleanOption(go, "-p",        &(cfg.do_profile));
  esl_opt_GetIntegerOption(go, "-L",        &(cfg.L));
  esl_opt_GetBooleanOption(go, "--h2",      &(cfg.do_oldconfig));
  esl_opt_GetStringOption (go, "--kpsfile", &(cfg.kpsfile));

  if (esl_opt_ArgNumber(go) != 1) {
    puts("Incorrect number of command line arguments.");
    puts(usage);
    return eslFAIL;
  }
  hmmfile = esl_opt_GetCmdlineArg(go, eslARG_STRING, NULL); /* NULL=no range checking */

  /*****************************************************************
   * Initializations, including opening the HMM file
   *****************************************************************/

  if ((r = esl_randomness_CreateTimeseeded()) == NULL)
    esl_fatal("Failed to create random number generator: probably out of memory");

  status = p7_hmmfile_Open(hmmfile, NULL, &hfp);
  if (status == eslENOTFOUND) esl_fatal("Failed to open hmm file %s for reading.\n", hmmfile);
  else if (status != eslOK)   esl_fatal("Unexpected error in opening hmm file %s.\n", hmmfile);
    
  status = p7_trace_Create(256, &tr);
  if (status != eslOK) esl_fatal("Failed to allocate trace\n");

  if ((status = p7_hmmfile_Read(hfp, &abc, &hmm)) != eslOK) {
    if      (status == eslEOD)       esl_fatal("read failed, HMM file %s may be truncated?", hmmfile);
    else if (status == eslEFORMAT)   esl_fatal("bad file format in HMM file %s", hmmfile);
    else if (status == eslEINCOMPAT) esl_fatal("HMM file %s contains different alphabets", hmmfile);
    else                             esl_fatal("Unexpected error in reading HMMs");
  }
   
  /* init sq (need to know abc to do this */
  if (sq == NULL) sq = esl_sq_CreateDigital(abc);
  if (sq == NULL) esl_fatal("Failed to allocated sequence");

  if (cfg.kpsfile != NULL) 
    {
      if ((cfg.kmx   = esl_dmatrix_Create(hmm->M, hmm->M)) == NULL) esl_fatal("matrix allocation failed");
      esl_dmatrix_SetZero(cfg.kmx);
    } 

  if ((hmm->bg = p7_bg_Create(abc))              == NULL)  esl_fatal("failed to created null model");
  if ((hmm->gm = p7_profile_Create(hmm->M, abc)) == NULL)  esl_fatal("failed to create profile");

  if (cfg.do_oldconfig) {
    if (p7_H2_ProfileConfig(hmm, hmm->gm, p7_LOCAL) != eslOK) esl_fatal("failed to configure profile");
  } else {
    if (p7_ProfileConfig(hmm, hmm->gm, p7_LOCAL)    != eslOK) esl_fatal("failed to configure profile");
    if (p7_ReconfigLength(hmm->gm, cfg.L)           != eslOK) esl_fatal("failed to reconfig profile L");
    if (p7_hmm_Validate    (hmm,     0.0001)       != eslOK) esl_fatal("whoops, HMM is bad!");
    if (p7_profile_Validate(hmm->gm, 0.0001)       != eslOK) esl_fatal("whoops, profile is bad!");
  }

  for (nseq = 1; nseq <= cfg.nseq; nseq++) 
    {
      if (cfg.do_profile && cfg.do_oldconfig) {
	status = p7_H2_ProfileEmit(r, hmm->gm, sq, tr);
	if (status != eslOK) esl_fatal("Failed to emit sequence from hmm\n");
      } else if (cfg.do_profile) {
	status = p7_ProfileEmit(r, hmm->gm, sq, tr);
	if (status != eslOK) esl_fatal("Failed to emit sequence from hmm\n");
      } else {
	status = p7_CoreEmit(r, hmm, sq, tr);
	if (status != eslOK) esl_fatal("Failed to emit sequence from hmm\n");
      }
      
      sprintf(sqname, "%s-sample%d", hmm->name, nseq);
      status = esl_sq_SetName(sq, sqname);
      if (status != eslOK) esl_fatal("Failed to set sequence name\n");

      status = esl_sqio_Write(stdout, sq, eslSQFILE_FASTA);
      if (status != eslOK) esl_fatal("Failed to write sequence\n");

      if (cfg.kpsfile != NULL &&
	  collect_model_endpoints(tr, cfg.kmx) != eslOK)
	esl_fatal("failed to collect model endpoints");
    }

  if (cfg.kpsfile != NULL)
    {
      int i,j;

      if ((cfg.kpsfp = fopen(cfg.kpsfile, "w")) == NULL) esl_fatal("Failed to open output postscript file %s", cfg.kpsfile);
      dmx_upper_norm(cfg.kmx);
      esl_dmx_Scale(cfg.kmx, (double) (hmm->M*(hmm->M+1)/2));
      for (i = 0; i < cfg.kmx->m; i++)
	for (j = i; j < cfg.kmx->n; j++)
	  cfg.kmx->mx[i][j] = log(cfg.kmx->mx[i][j]) / log(2.0);
      printf("minimum element = %f\n", dmx_upper_min(cfg.kmx));
      printf("maximum element = %f\n", dmx_upper_max(cfg.kmx));
      dmx_Visualize(cfg.kpsfp, cfg.kmx, -4.0, 4.0);
      /*      dmx_Visualize(cfg.kpsfp, cfg.kmx, dmx_upper_min(cfg.kmx), dmx_upper_max(cfg.kmx)); */
      fclose(cfg.kpsfp);
    }

  esl_sq_Destroy(sq);
  p7_trace_Destroy(tr);
  esl_randomness_Destroy(r);
  esl_alphabet_Destroy(abc);
  esl_getopts_Destroy(go);
  p7_hmmfile_Close(hfp);
  p7_hmm_Destroy(hmm);
  return eslOK;
}


/* collect_model_endpoints
 * Incept:    SRE, Wed Jan 24 15:04:02 2007 [Janelia]
 *
 * Purpose:   
 *
 * Args:      
 *
 * Returns:   
 *
 * Throws:    (no abnormal error conditions)
 *
 * Xref:      
 */
static int
collect_model_endpoints(P7_TRACE *tr, ESL_DMATRIX *D)
{
  int tpos;
  int firstk = -1;
  int lastk  = -1;

  for (tpos = 0; tpos < tr->N; tpos++)
    {
      if (tr->st[tpos] == p7_STM) {
	lastk = tr->k[tpos];
	if (firstk == -1) firstk = tr->k[tpos];
      }
      if (tr->st[tpos] == p7_STD) {
	lastk = tr->k[tpos];
      }
      if (tr->st[tpos] == p7_STE) {
	D->mx[firstk-1][lastk-1] += 1.;
	firstk = -1;
      }
    }
  return eslOK;
}

/* In a HMMER2 profile, the DP model is probabilistic (there is no implicit
 * model), with begin[] as a probability distribution and end[k] being treated
 * as a fourth match transition; sample_endpoints (for the H3 style) is
 * not used
 */
static int
p7_H2_ProfileEmit(ESL_RANDOMNESS *r, P7_PROFILE *gm, ESL_SQ *sq, P7_TRACE *tr)
{
  char      prv, st;		/* prev, current state type */
  int       k;		        /* position in model nodes 1..M */
  int       i;			/* position in sequence 1..L */
  int       x;			/* sampled residue */
  int       status;
  float     p[4];

  if (sq != NULL) esl_sq_Reuse(sq);    
  if (tr != NULL) {
    if ((status = p7_trace_Reuse(tr))                != eslOK) goto ERROR;
    if ((status = p7_trace_Append(tr, p7_STS, 0, 0)) != eslOK) goto ERROR;
    if ((status = p7_trace_Append(tr, p7_STN, 0, 0)) != eslOK) goto ERROR;
  }
  st    = p7_STN;
  k     = 0;
  i     = 0;
  while (st != p7_STT)
    {
      /* Sample a state transition. After this section, prv and st (prev->current state) are set;
       * k also gets set if we make a B->Mk entry transition.
       */
      prv = st;
      switch (st) {
      case p7_STB:  /* Enter the implicit profile: choose our entry and our predestined exit */
	k  = 1 + esl_rnd_FChoose(r, gm->begin+1, gm->M);
	st = p7_STM;		/* must be, because left wing is retracted */
	break;
	
      case p7_STM:
	if (k == gm->M) st = p7_STE;
	else {
	  esl_vec_FCopy(p, gm->hmm->t[k], 3);
	  p[3] = gm->end[k];
	  switch (esl_rnd_FChoose(r, p, 4)) {
	  case 0:  st = p7_STM; break;
	  case 1:  st = p7_STI; break;
	  case 2:  st = p7_STD; break;
	  case 3:  st = p7_STE; break;
	  default: ESL_XEXCEPTION(eslEINCONCEIVABLE, "impossible.");  	    
	  }
	}
	break;

      case p7_STD:
	if (k == gm->M) st = p7_STE;
	else            st = (esl_rnd_FChoose(r, gm->hmm->t[k]+5,2) == 0) ? p7_STM : p7_STD;  
	break;

      case p7_STI: st = (esl_rnd_FChoose(r, gm->hmm->t[k]+3,2) == 0) ? p7_STM : p7_STI;  break;
      case p7_STN: st = (esl_rnd_FChoose(r, gm->xt[p7_XTN], 2) == 0) ? p7_STB : p7_STN;  break;
      case p7_STE: st = (esl_rnd_FChoose(r, gm->xt[p7_XTE], 2) == 0) ? p7_STC : p7_STJ;  break;
      case p7_STJ: st = (esl_rnd_FChoose(r, gm->xt[p7_XTJ], 2) == 0) ? p7_STB : p7_STJ;  break;
      case p7_STC: st = (esl_rnd_FChoose(r, gm->xt[p7_XTC], 2) == 0) ? p7_STT : p7_STC;  break;
      default:     ESL_XEXCEPTION(eslECORRUPT, "impossible state reached during emission");
      }
     
      /* Based on the transition we just sampled, update k. */
      if      (st == p7_STE)                  k = 0;
      else if (st == p7_STM && prv != p7_STB) k++;    /* be careful about B->Mk, where we already set k */
      else if (st == p7_STD)                  k++;

      /* Based on the transition we just sampled, generate a residue. */
      if      (st == p7_STM)                                              x = esl_rnd_FChoose(r, gm->hmm->mat[k], gm->abc->K);
      else if (st == p7_STI)                                              x = esl_rnd_FChoose(r, gm->hmm->ins[k], gm->abc->K);
      else if ((st == p7_STN || st == p7_STC || st == p7_STJ) && prv==st) x = esl_rnd_FChoose(r, gm->bg->f,       gm->abc->K);
      else    x = eslDSQ_SENTINEL;

      if (x != eslDSQ_SENTINEL) i++;

      /* Add residue (if any) to sequence */
      if (sq != NULL && x != eslDSQ_SENTINEL && (status = esl_sq_XAddResidue(sq, x)) != eslOK) goto ERROR;

      /* Add state to trace; distinguish emitting position (pass i=i) from non (pass i=0) */
      if (tr != NULL) {
	if (x == eslDSQ_SENTINEL) {
	  if ((status = p7_trace_Append(tr, st, k, 0)) != eslOK) goto ERROR;
	} else {
	  if ((status = p7_trace_Append(tr, st, k, i)) != eslOK) goto ERROR;
	}
      }
    }
  /* Terminate the sequence (if we're generating one) */
  if (sq != NULL && (status = esl_sq_XAddResidue(sq, eslDSQ_SENTINEL)) != eslOK) goto ERROR;
  return eslOK;

 ERROR:
  return status;
}
