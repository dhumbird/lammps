/// **************************************************************************
//                              hippo_extra.h
//                             -------------------
//                              Trung Dac Nguyen
//
//  Device code for hippo math routines
//
// __________________________________________________________________________
//    This file is part of the LAMMPS Accelerator Library (LAMMPS_AL)
// __________________________________________________________________________
//
//    begin                :
//    email                : ndactrung@gmail.com
// ***************************************************************************/*

#ifndef LAL_HIPPO_EXTRA_H
#define LAL_HIPPO_EXTRA_H

#if defined(NV_KERNEL) || defined(USE_HIP)
#include "lal_aux_fun1.h"
#else
#endif

#define MY_PI2 (numtyp)1.57079632679489661923
#define MY_PI4 (numtyp)0.78539816339744830962

/* ----------------------------------------------------------------------
   damprep generates coefficients for the Pauli repulsion
   damping function for powers of the interatomic distance

   literature reference:

   J. A. Rackers and J. W. Ponder, "Classical Pauli Repulsion: An
   Anisotropic, Atomic Multipole Model", Journal of Chemical Physics,
   150, 084104 (2019)
------------------------------------------------------------------------- */

ucl_inline void damprep(const numtyp r, const numtyp r2, const numtyp rr1,
                        const numtyp rr3, const numtyp rr5, const numtyp rr7,
                        const numtyp rr9, const numtyp rr11, const int rorder,
                        const numtyp dmpi, const numtyp dmpk, numtyp dmpik[11])
{
  numtyp r3,r4;
  numtyp r5,r6,r7,r8;
  numtyp s,ds,d2s;
  numtyp d3s,d4s,d5s;
  numtyp dmpi2,dmpk2;
  numtyp dmpi22,dmpi23;
  numtyp dmpi24,dmpi25;
  numtyp dmpi26,dmpi27;
  numtyp dmpk22,dmpk23;
  numtyp dmpk24,dmpk25;
  numtyp dmpk26;
  numtyp eps,diff;
  numtyp expi,expk;
  numtyp dampi,dampk;
  numtyp pre,term,tmp;

  // compute tolerance value for damping exponents

  eps = (numtyp)0.001;
  diff = dmpi-dmpk; 
  if (diff < (numtyp)0) diff = -diff;

  // treat the case where alpha damping exponents are equal

  if (diff < eps) {
    r3 = r2 * r;
    r4 = r3 * r;
    r5 = r4 * r;
    r6 = r5 * r;
    r7 = r6 * r;
    dmpi2 = (numtyp)0.5 * dmpi;
    dampi = dmpi2 * r;
    expi = ucl_exp(-dampi);
    dmpi22 = dmpi2 * dmpi2;
    dmpi23 = dmpi22 * dmpi2;
    dmpi24 = dmpi23 * dmpi2;
    dmpi25 = dmpi24 * dmpi2;
    dmpi26 = dmpi25 * dmpi2;
    pre = (numtyp)128.0;
    s = (r + dmpi2*r2 + dmpi22*r3/(numtyp)3.0) * expi;

    ds = (dmpi22*r3 + dmpi23*r4) * expi / 3.0;
    d2s = dmpi24 * expi * r5 / 9.0;
    d3s = dmpi25 * expi * r6 / 45.0;
    d4s = (dmpi25*r6 + dmpi26*r7) * expi / 315.0;
    if (rorder >= 11) {
      r8 = r7 * r;
      dmpi27 = dmpi2 * dmpi26;
      d5s = (dmpi25*r6 + dmpi26*r7 + dmpi27*r8/3.0) * expi / 945.0;
    }

  // treat the case where alpha damping exponents are unequal

  } else {
    r3 = r2 * r;
    r4 = r3 * r;
    r5 = r4 * r;
    dmpi2 = 0.5 * dmpi;
    dmpk2 = 0.5 * dmpk;
    dampi = dmpi2 * r;
    dampk = dmpk2 * r;
    expi = exp(-dampi);
    expk = exp(-dampk);
    dmpi22 = dmpi2 * dmpi2;
    dmpi23 = dmpi22 * dmpi2;
    dmpi24 = dmpi23 * dmpi2;
    dmpi25 = dmpi24 * dmpi2;
    dmpk22 = dmpk2 * dmpk2;
    dmpk23 = dmpk22 * dmpk2;
    dmpk24 = dmpk23 * dmpk2;
    dmpk25 = dmpk24 * dmpk2;
    term = dmpi22 - dmpk22;
    pre = 8192.0 * dmpi23 * dmpk23 / pow(term,4.0);
    tmp = 4.0 * dmpi2 * dmpk2 / term;
    s = (dampi-tmp)*expk + (dampk+tmp)*expi;

    ds = (dmpi2*dmpk2*r2 - 4.0*dmpi2*dmpk22*r/term - 
          4.0*dmpi2*dmpk2/term) * expk + 
      (dmpi2*dmpk2*r2 + 4.0*dmpi22*dmpk2*r/term + 4.0*dmpi2*dmpk2/term) * expi;
    d2s = (dmpi2*dmpk2*r2/3.0 + dmpi2*dmpk22*r3/3.0 - 
           (4.0/3.0)*dmpi2*dmpk23*r2/term - 4.0*dmpi2*dmpk22*r/term - 
           4.0*dmpi2*dmpk2/term) * expk + 
      (dmpi2*dmpk2*r2/3.0 + dmpi22*dmpk2*r3/3.0 + 
       (4.0/3.0)*dmpi23*dmpk2*r2/term + 4.0*dmpi22*dmpk2*r/term + 
       4.0*dmpi2*dmpk2/term) * expi;
    d3s = (dmpi2*dmpk23*r4/15.0 + dmpi2*dmpk22*r3/5.0 + dmpi2*dmpk2*r2/5.0 - 
           (4.0/15.0)*dmpi2*dmpk24*r3/term - (8.0/5.0)*dmpi2*dmpk23*r2/term - 
           4.0*dmpi2*dmpk22*r/term - 4.0/term*dmpi2*dmpk2) * expk + 
      (dmpi23*dmpk2*r4/15.0 + dmpi22*dmpk2*r3/5.0 + dmpi2*dmpk2*r2/5.0 + 
       (4.0/15.0)*dmpi24*dmpk2*r3/term + (8.0/5.0)*dmpi23*dmpk2*r2/term + 
       4.0*dmpi22*dmpk2*r/term + 4.0/term*dmpi2*dmpk2) * expi;
    d4s = (dmpi2*dmpk24*r5/105.0 + (2.0/35.0)*dmpi2*dmpk23*r4 + 
           dmpi2*dmpk22*r3/7.0 + dmpi2*dmpk2*r2/7.0 - 
           (4.0/105.0)*dmpi2*dmpk25*r4/term - (8.0/21.0)*dmpi2*dmpk24*r3/term - 
           (12.0/7.0)*dmpi2*dmpk23*r2/term - 4.0*dmpi2*dmpk22*r/term - 
           4.0*dmpi2*dmpk2/term) * expk + 
      (dmpi24*dmpk2*r5/105.0 + (2.0/35.0)*dmpi23*dmpk2*r4 + 
       dmpi22*dmpk2*r3/7.0 + dmpi2*dmpk2*r2/7.0 + 
       (4.0/105.0)*dmpi25*dmpk2*r4/term + (8.0/21.0)*dmpi24*dmpk2*r3/term + 
       (12.0/7.0)*dmpi23*dmpk2*r2/term + 4.0*dmpi22*dmpk2*r/term + 
       4.0*dmpi2*dmpk2/term) * expi;
    
    if (rorder >= 11) {
      r6 = r5 * r;
      dmpi26 = dmpi25 * dmpi2;
      dmpk26 = dmpk25 * dmpk2;
      d5s = (dmpi2*dmpk25*r6/945.0 + (2.0/189.0)*dmpi2*dmpk24*r5 + 
             dmpi2*dmpk23*r4/21.0 + dmpi2*dmpk22*r3/9.0 + dmpi2*dmpk2*r2/9.0 - 
             (4.0/945.0)*dmpi2*dmpk26*r5/term - 
             (4.0/63.0)*dmpi2*dmpk25*r4/term - (4.0/9.0)*dmpi2*dmpk24*r3/term - 
             (16.0/9.0)*dmpi2*dmpk23*r2/term - 4.0*dmpi2*dmpk22*r/term - 
             4.0*dmpi2*dmpk2/term) * expk + 
        (dmpi25*dmpk2*r6/945.0 + (2.0/189.0)*dmpi24*dmpk2*r5 + 
         dmpi23*dmpk2*r4/21.0 + dmpi22*dmpk2*r3/9.0 + dmpi2*dmpk2*r2/9.0 + 
         (4.0/945.0)*dmpi26*dmpk2*r5/term + (4.0/63.0)*dmpi25*dmpk2*r4/term + 
         (4.0/9.0)*dmpi24*dmpk2*r3/term + (16.0/9.0)*dmpi23*dmpk2*r2/term + 
         4.0*dmpi22*dmpk2*r/term + 4.0*dmpi2*dmpk2/term) * expi;
    }
  }

  // convert partial derivatives into full derivatives

  s = s * rr1;
  ds = ds * rr3;
  d2s = d2s * rr5;
  d3s = d3s * rr7;
  d4s = d4s * rr9;
  d5s = d5s * rr11;
  dmpik[0] = 0.5 * pre * s * s;
  dmpik[2] = pre * s * ds;
  dmpik[4] = pre * (s*d2s + ds*ds);
  dmpik[6] = pre * (s*d3s + 3.0*ds*d2s);
  dmpik[8] = pre * (s*d4s + 4.0*ds*d3s + 3.0*d2s*d2s);
  if (rorder >= 11) dmpik[10] = pre * (s*d5s + 5.0*ds*d4s + 10.0*d2s*d3s);
}

/* ----------------------------------------------------------------------
   damppole generates coefficients for the charge penetration
   damping function for powers of the interatomic distance

   literature references:

   L. V. Slipchenko and M. S. Gordon, "Electrostatic Energy in the
   Effective Fragment Potential Method: Theory and Application to
   the Benzene Dimer", Journal of Computational Chemistry, 28,
   276-291 (2007)  [Gordon f1 and f2 models]

   J. A. Rackers, Q. Wang, C. Liu, J.-P. Piquemal, P. Ren and
   J. W. Ponder, "An Optimized Charge Penetration Model for Use with
   the AMOEBA Force Field", Physical Chemistry Chemical Physics, 19,
   276-291 (2017)
------------------------------------------------------------------------- */

ucl_inline void damppole(const numtyp r, const int rorder,
                         const numtyp alphai, const numtyp alphak,
                         numtyp dmpi[9], numtyp dmpk[9], numtyp dmpik[11])
{
  numtyp termi,termk;
  numtyp termi2,termk2;
  numtyp alphai2,alphak2;
  numtyp eps,diff;
  numtyp expi,expk;
  numtyp dampi,dampk;
  numtyp dampi2,dampi3;
  numtyp dampi4,dampi5;
  numtyp dampi6,dampi7;
  numtyp dampi8;
  numtyp dampk2,dampk3;
  numtyp dampk4,dampk5;
  numtyp dampk6;

  // compute tolerance and exponential damping factors

  eps = 0.001;
  diff = fabs(alphai-alphak);
  dampi = alphai * r;
  dampk = alphak * r;
  expi = exp(-dampi);
  expk = exp(-dampk);

  // core-valence charge penetration damping for Gordon f1

  dampi2 = dampi * dampi;
  dampi3 = dampi * dampi2;
  dampi4 = dampi2 * dampi2;
  dampi5 = dampi2 * dampi3;
  dmpi[0] = 1.0 - (1.0 + 0.5*dampi)*expi;
  dmpi[2] = 1.0 - (1.0 + dampi + 0.5*dampi2)*expi;
  dmpi[4] = 1.0 - (1.0 + dampi + 0.5*dampi2 + dampi3/6.0)*expi;
  dmpi[6] = 1.0 - (1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + dampi4/30.0)*expi;
  dmpi[8] = 1.0 - (1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + 
                   4.0*dampi4/105.0 + dampi5/210.0)*expi;
  if (diff < eps) {
    dmpk[0] = dmpi[0];
    dmpk[2] = dmpi[2];
    dmpk[4] = dmpi[4];
    dmpk[6] = dmpi[6];
    dmpk[8] = dmpi[8];
  } else {
    dampk2 = dampk * dampk;
    dampk3 = dampk * dampk2;
    dampk4 = dampk2 * dampk2;
    dampk5 = dampk2 * dampk3;
    dmpk[0] = 1.0 - (1.0 + 0.5*dampk)*expk;
    dmpk[2] = 1.0 - (1.0 + dampk + 0.5*dampk2)*expk;
    dmpk[4] = 1.0 - (1.0 + dampk + 0.5*dampk2 + dampk3/6.0)*expk;
    dmpk[6] = 1.0 - (1.0 + dampk + 0.5*dampk2 + dampk3/6.0 + dampk4/30.0)*expk;
    dmpk[8] = 1.0 - (1.0 + dampk + 0.5*dampk2 + dampk3/6.0 + 
                     4.0*dampk4/105.0 + dampk5/210.0)*expk;
  }

  // valence-valence charge penetration damping for Gordon f1

  if (diff < eps) {
    dampi6 = dampi3 * dampi3;
    dampi7 = dampi3 * dampi4;
    dmpik[0] = 1.0 - (1.0 + 11.0*dampi/16.0 + 3.0*dampi2/16.0 + 
                      dampi3/48.0)*expi;
    dmpik[2] = 1.0 - (1.0 + dampi + 0.5*dampi2 + 
                      7.0*dampi3/48.0 + dampi4/48.0)*expi;
    dmpik[4] = 1.0 - (1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + 
                      dampi4/24.0 + dampi5/144.0)*expi;
    dmpik[6] = 1.0 - (1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + 
                      dampi4/24.0 + dampi5/120.0 + dampi6/720.0)*expi;
    dmpik[8] = 1.0 - (1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + 
                      dampi4/24.0 + dampi5/120.0 + dampi6/720.0 + 
                      dampi7/5040.0)*expi;
    if (rorder >= 11) {
      dampi8 = dampi4 * dampi4;
      dmpik[10] = 1.0 - (1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + 
                         dampi4/24.0 + dampi5/120.0 + dampi6/720.0 + 
                         dampi7/5040.0 + dampi8/45360.0)*expi;
    }

  } else {
    alphai2 = alphai * alphai;
    alphak2 = alphak * alphak;
    termi = alphak2 / (alphak2-alphai2);
    termk = alphai2 / (alphai2-alphak2);
    termi2 = termi * termi;
    termk2 = termk * termk;
    dmpik[0] = 1.0 - termi2*(1.0 + 2.0*termk + 0.5*dampi)*expi - 
      termk2*(1.0 + 2.0*termi + 0.5*dampk)*expk;
    dmpik[2] = 1.0 - termi2*(1.0+dampi+0.5*dampi2)*expi -
      termk2*(1.0+dampk+0.5*dampk2)*expk -
      2.0*termi2*termk*(1.0+dampi)*expi -
      2.0*termk2*termi*(1.0+dampk)*expk;
    dmpik[4] = 1.0 - termi2*(1.0 + dampi + 0.5*dampi2 + dampi3/6.0)*expi - 
      termk2*(1.0 + dampk + 0.5*dampk2 + dampk3/6.0)*expk - 
      2.0*termi2*termk*(1.0 + dampi + dampi2/3.0)*expi - 
      2.0*termk2*termi*(1.0 + dampk + dampk2/3.0)*expk;
    dmpik[6] = 1.0 - termi2*(1.0 + dampi + 0.5*dampi2 + 
                             dampi3/6.0 + dampi4/30.0)*expi - 
      termk2*(1.0 + dampk + 0.5*dampk2 + dampk3/6.0 + dampk4/30.0)*expk - 
      2.0*termi2*termk*(1.0 + dampi + 2.0*dampi2/5.0 + dampi3/15.0)*expi - 
      2.0*termk2*termi*(1.0 + dampk + 2.0*dampk2/5.0 + dampk3/15.0)*expk;
    dmpik[8] = 1.0 - termi2*(1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + 
                             4.0*dampi4/105.0 + dampi5/210.0)*expi - 
      termk2*(1.0 + dampk + 0.5*dampk2 + dampk3/6.0 + 
              4.0*dampk4/105.0 + dampk5/210.0)*expk - 
      2.0*termi2*termk*(1.0 + dampi + 3.0*dampi2/7.0 + 
                        2.0*dampi3/21.0 + dampi4/105.0)*expi - 
      2.0*termk2*termi*(1.0 + dampk + 3.0*dampk2/7.0 + 
                        2.0*dampk3/21.0 + dampk4/105.0)*expk;
    
    if (rorder >= 11) {
      dampi6 = dampi3 * dampi3;
      dampk6 = dampk3 * dampk3;
      dmpik[10] = 1.0 - termi2*(1.0 + dampi + 0.5*dampi2 + dampi3/6.0 + 
                                5.0*dampi4/126.0 + 2.0*dampi5/315.0 + 
                                dampi6/1890.0)*expi - 
        termk2*(1.0 + dampk + 0.5*dampk2 + dampk3/6.0 + 5.0*dampk4/126.0 + 
                2.0*dampk5/315.0 + dampk6/1890.0)*expk - 
        2.0*termi2*termk*(1.0 + dampi + 4.0*dampi2/9.0 + dampi3/9.0 + 
                          dampi4/63.0 + dampi5/945.0)*expi - 
        2.0*termk2*termi*(1.0 + dampk + 4.0*dampk2/9.0 + dampk3/9.0 + 
                          dampk4/63.0 + dampk5/945.0)*expk;
    }
  }
}



#endif
