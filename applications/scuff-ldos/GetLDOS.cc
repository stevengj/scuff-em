/* Copyright (C) 2005-2011 M. T. Homer Reid
 *
 * This file is part of SCUFF-EM.
 *
 * SCUFF-EM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SCUFF-EM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * GetLDOS.cc -- routine to compute the LDOS at a single 
 *               (Omega, kBloch) point
 *
 * homer reid -- 3/2015
 */
#include <libhrutil.h>
#include <libhmat.h>
#include "libscuff.h"
#include "scuff-ldos.h"

#define ABSTOL 1.0e-20
#define MAXSTR 1000

using namespace scuff;

/***************************************************************/
/***************************************************************/
/***************************************************************/
void WriteData(SLDData *Data, cdouble Omega, double *kBloch,
               int FileType, int WhichTransform, int WhichMatrix,
               double *Result, double *Error)
{
  int LDim         = Data->G->LDim;
  HMatrix *XMatrix = Data->XMatrices[WhichMatrix];
  char *EPFileBase = Data->EPFileBases[WhichMatrix];
  char *FileBase   = Data->FileBase;

  bool TwoPointDGF = (XMatrix->NC==6);

  const char *Extension=0;
  if (FileType==FILETYPE_BYK)
   Extension = "byOmegakBloch";
  else
   Extension = TwoPointDGF ? "2PDGF" : "LDOS";

  char FileName[MAXSTR];
  if (Data->NumXMatrices==1)
   snprintf(FileName,MAXSTR,"%s.%s",FileBase,Extension);
  else
   snprintf(FileName,MAXSTR,"%s.%s.%s",FileBase,EPFileBase,Extension);

  bool HaveGTCList = (Data->GTCs.size()!=0);
  if ( Data->WrotePreamble[FileType][WhichMatrix] == false )
   { Data->WrotePreamble[FileType][WhichMatrix] = true;
     WriteFilePreamble(FileName, FileType, LDim, HaveGTCList, TwoPointDGF);
   };

  int NFun = (Data->LDOSOnly ? 2 : 38);
  int Offset=0;
  for(int nm=0; nm<WhichMatrix; nm++)
   Offset += NFun * (Data->XMatrices[nm]->NR);
  
  FILE *f=fopen(FileName,"a");
  for(int nx=0; nx<XMatrix->NR; nx++)
   { 
     double X[6];
     XMatrix->GetEntriesD(nx,":",X);

     fprintf(f,"%e %e %e ", X[0],X[1],X[2]);
     if (TwoPointDGF)
      fprintf(f,"%e %e %e ", X[3],X[4],X[5]);
     fprintf(f,"%e %e ",real(Omega),imag(Omega));
     if (HaveGTCList)
      fprintf(f,"%s ",Data->GTCs[WhichTransform]->Tag);
     if (FileType==FILETYPE_BYK && LDim>0 && kBloch!=0)
      for(int nd=0; nd<LDim; nd++)
       fprintf(f,"%e ",kBloch[nd]);

     for(int nf=0; nf<NFun; nf++)
      fprintf(f,"%e ",Result[Offset + NFun*nx + nf]);

     if (Error)
      for(int nf=0; nf<NFun; nf++) 
       fprintf(f,"%e ",Error[Offset + NFun*nx + nf]);

     fprintf(f,"\n");
   };
  fclose(f);
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void WriteData(SLDData *Data, cdouble Omega, double *kBloch,
               int FileType, double *Result, double *Error)
{
  for(int nt=0; nt<Data->NumTransforms; nt++)
   for(int nm=0; nm<Data->NumXMatrices; nm++)
    WriteData(Data, Omega, kBloch, FileType, nt, nm, Result, Error);
}

/***************************************************************/
/* routine to compute the LDOS at a single (Omega, kBloch)     */
/* point (but typically multiple spatial evaluation points)    */
/***************************************************************/
void GetLDOS(void *pData, cdouble Omega, double *kBloch,
             double *Result)
{
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  SLDData *Data        = (SLDData *)pData;
  RWGGeometry *G       = Data->G;
  HMatrix *M           = Data->M;
  HMatrix **XMatrices  = Data->XMatrices;
  HMatrix **GMatrices  = Data->GMatrices;
  int NumXMatrices     = Data->NumXMatrices;
  void **ABMBCache     = Data->ABMBCache;
  MatProp *HalfSpaceMP = Data->HalfSpaceMP;
  bool GroundPlane     = Data->GroundPlane;
  bool ScatteringOnly  = Data->ScatteringOnly;
  double RelTol        = Data->RelTol;
  double AbsTol        = Data->AbsTol;
  int MaxEvals         = Data->MaxEvals;
  int NumTransforms    = Data->NumTransforms;

  HMatrix *LBasis      = G->LBasis;
  HMatrix *RLBasis     = G->RLBasis;
  double BZVolume      = G->RLVolume;
  int LDim = RLBasis ? RLBasis->NC : 0;
  switch(LDim)
   { case 0: Log("Computing LDOS at Omega=%s",z2s(Omega));
             break; 
     case 1: Log("Computing LDOS at (Omega,kx)=(%s,%e)",z2s(Omega),kBloch[0]); 
             break;
     case 2: Log("Computing LDOS at (Omega,kx,ky)=(%s,%e,%e),",z2s(Omega),kBloch[0],kBloch[1]);
   };

  /*--------------------------------------------------------------*/
  /*- assemble the BEM matrix at this frequency and Bloch vector, */
  /*- then get DGFs at all evaluation points                      */
  /*--------------------------------------------------------------*/
  if (HalfSpaceMP) 
   {
     for(int nm=0; nm<NumXMatrices; nm++)
      GetHalfSpaceDGFs_BZ(XMatrices[nm], Omega, kBloch,
                          RLBasis, BZVolume, HalfSpaceMP,
                          RelTol, AbsTol, MaxEvals,
                          GMatrices[nm]);
   }
  else if (GroundPlane)
   {
     for(int nm=0; nm<NumXMatrices; nm++)
      GetGroundPlaneDGFs(XMatrices[nm], Omega, kBloch,
                         LBasis, GMatrices[nm]);
   }
  else if (NumTransforms==1)
   { 
     if (LDim==0)
      G->AssembleBEMMatrix(Omega, M);
     else
      { int NS = G->NumSurfaces;
        for(int ns=0, nb=0; ns<NS; ns++)
         for(int nsp=ns; nsp<NS; nsp++, nb++)
          { 
            int RowOffset = G->BFIndexOffset[ns];
            int ColOffset = G->BFIndexOffset[nsp];
            G->AssembleBEMMatrixBlock(ns, nsp, Omega, kBloch,
                                      M, 0, RowOffset, ColOffset,
                                      ABMBCache[nb], false);

            if (nsp>ns)
             G->AssembleBEMMatrixBlock(nsp, ns, Omega, kBloch,
                                       M, 0, ColOffset, RowOffset,
                                       ABMBCache[nb], true);
          };
      };
     M->LUFactorize();
     for(int nm=0; nm<NumXMatrices; nm++)
      G->GetDyadicGFs(Omega, kBloch, XMatrices[nm], M, GMatrices[nm],
                      ScatteringOnly);
   }
  else 
   {
     HMatrix **TBlocks   = Data->TBlocks;
     HMatrix **UBlocks   = Data->UBlocks;

     // assemble diagonal blocks
     for(int ns=0; ns<G->NumSurfaces; ns++)
      if (G->Mate[ns]==-1)
       G->AssembleBEMMatrixBlock(ns, ns, Omega, kBloch, TBlocks[ns]);
   
     // loop over transformations
     for(int nt=0; nt<NumTransforms; nt++)
      { G->Transform(Data->GTCs[nt]);
        Log("Working at transformation %s...",Data->GTCs[nt]->Tag);

        // assemble off-diagonal blocks
        for(int ns=0, nb=0; ns<G->NumSurfaces; ns++)
         for(int nsp=ns+1; nsp<G->NumSurfaces; nsp++, nb++)
          G->AssembleBEMMatrixBlock(ns, nsp, Omega, kBloch, UBlocks[nb]);

        // assemble and factorize BEM matrix
        for(int ns=0, nb=0; ns<G->NumSurfaces; ns++)
         { int RowOffset=G->BFIndexOffset[ns];
           M->InsertBlock(TBlocks[ns], RowOffset, RowOffset);
           for(int nsp=ns+1; nsp<G->NumSurfaces; nsp++, nb++)
            { int ColOffset=G->BFIndexOffset[nsp];
              M->InsertBlock(UBlocks[nb], RowOffset, ColOffset);
              M->InsertBlockAdjoint(UBlocks[nb], ColOffset, RowOffset);
            };
         };
        M->LUFactorize();

        // get LDOS 
        for(int nm=0; nm<NumXMatrices; nm++)
         G->GetDyadicGFs(Omega, kBloch, XMatrices[nm], M,
                         GMatrices[nt*NumXMatrices + nm],
                         ScatteringOnly);

        G->UnTransform();
      };
   };
                               
  /*--------------------------------------------------------------*/
  /*- get LDOS at all evaluation points.                          */
  /*- Note: The LDOS is defined as                                */
  /*-  \Rho = (abs(\omega) / \pi c^2) * Im Tr G                   */
  /*--------------------------------------------------------------*/
  double PreFac = abs(Omega)/M_PI;
  int nResult=0;
  for(int nt=0; nt<NumTransforms; nt++)
   for(int nm=0; nm<NumXMatrices; nm++)
    { 
      HMatrix *XMatrix = Data->XMatrices[nm];
      HMatrix *GMatrix = Data->GMatrices[nt*NumXMatrices + nm];
      for(int nx=0; nx<XMatrix->NR; nx++)
       { cdouble GE[3][3], GM[3][3];
         for(int i=0; i<3; i++)
          for(int j=0; j<3; j++)
           { GE[i][j] = GMatrix->GetEntry(nx, 0 + 3*i + j);
             GM[i][j] = GMatrix->GetEntry(nx, 9 + 3*i + j);
           };
         double ELDOS = PreFac * imag( GE[0][0] + GE[1][1] + GE[2][2] );
         double MLDOS = PreFac * imag( GM[0][0] + GM[1][1] + GM[2][2] );

         Result[nResult++] = ELDOS;
         Result[nResult++] = MLDOS;
         if (Data->LDOSOnly == false)
          { for(int Mu=0; Mu<3; Mu++)
             for(int Nu=0; Nu<3; Nu++)
              { Result[nResult++] = real(GE[Mu][Nu]);
                Result[nResult++] = imag(GE[Mu][Nu]);
              };
            for(int Mu=0; Mu<3; Mu++)
             for(int Nu=0; Nu<3; Nu++)
              { Result[nResult++] = real(GM[Mu][Nu]);
                Result[nResult++] = imag(GM[Mu][Nu]);
              };
          }; // if (Data->LDOSOnly == false)
 
       }; // for(int nx=0; nx<XMatrix->NR; nx++)
 
    /***************************************************************/
    /* write output to kBloch-resolved data file for PBC geometries*/
    /*  or the tput to kBloch-resolved data file for PBC geometries*/
    /***************************************************************/
    if (LDim>0)
     WriteData(Data, Omega, kBloch, FILETYPE_BYK, nt, nm, Result, 0);
    else
     WriteData(Data, Omega, 0,      FILETYPE_LDOS, nt, nm, Result, 0);

   }; // for(int nm=0; nm<NumXMatrices; nm++)

}
