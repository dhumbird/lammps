/* -*- c++ -*- ----------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   https://www.lammps.org/, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#ifndef GSM_TANGENTIAL_H_
#define GSM_TANGENTIAL_H_

#include "gsm.h"

namespace LAMMPS_NS {
namespace Granular_NS {

class GSMTangential : public GSM {
 public:
  GSMTangential(class GranularModel *, class LAMMPS *);
  virtual ~GSMTangential() {};
  virtual void coeffs_to_local() {};
  virtual void init() {};
  virtual void calculate_forces() = 0;
  int rescale_flag;
  double k, damp, mu; // Used by Marshall twisting model
 protected:
  double xt;
};

/* ---------------------------------------------------------------------- */

class GSMTangentialNone : public GSMTangential {
 public:
  GSMTangentialNone(class GranularModel *, class LAMMPS *);
  void calculate_forces() {};
};

/* ---------------------------------------------------------------------- */

class GSMTangentialLinearNoHistory : public GSMTangential {
 public:
  GSMTangentialLinearNoHistory(class GranularModel *, class LAMMPS *);
  void coeffs_to_local() override;
  void calculate_forces();
};

/* ---------------------------------------------------------------------- */

class GSMTangentialLinearHistory : public GSMTangential {
 public:
  GSMTangentialLinearHistory(class GranularModel *, class LAMMPS *);
  void coeffs_to_local() override;
  void calculate_forces();
};

/* ---------------------------------------------------------------------- */

class GSMTangentialLinearHistoryClassic : public GSMTangentialLinearHistory {
 public:
  GSMTangentialLinearHistoryClassic(class GranularModel *, class LAMMPS *);
  void calculate_forces();
  int scale_area;
};

/* ---------------------------------------------------------------------- */

class GSMTangentialMindlinClassic : public GSMTangentialLinearHistoryClassic {
 public:
  GSMTangentialMindlinClassic(class GranularModel *, class LAMMPS *);
};

/* ---------------------------------------------------------------------- */

class GSMTangentialMindlin : public GSMTangential {
 public:
  GSMTangentialMindlin(class GranularModel *, class LAMMPS *);
  void coeffs_to_local() override;
  void mix_coeffs(double*, double*) override;
  void calculate_forces();
 protected:
  int mindlin_rescale, mindlin_force;
};

/* ---------------------------------------------------------------------- */

class GSMTangentialMindlinForce : public GSMTangentialMindlin {
 public:
  GSMTangentialMindlinForce(class GranularModel *, class LAMMPS *);
};

/* ---------------------------------------------------------------------- */

class GSMTangentialMindlinRescale : public GSMTangentialMindlin {
 public:
  GSMTangentialMindlinRescale(class GranularModel *, class LAMMPS *);
};

/* ---------------------------------------------------------------------- */

class GSMTangentialMindlinRescaleForce : public GSMTangentialMindlin {
 public:
  GSMTangentialMindlinRescaleForce(class GranularModel *, class LAMMPS *);
};

}    // namespace Granular_NS
}    // namespace LAMMPS_NS

#endif /*GSM_TANGENTIAL_H_ */
