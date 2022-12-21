/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   https://www.lammps.org/, Sandia National Laboratories
   LAMMPS development team: developers@lammps.org

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing authors: Axel Kohlmeyer (Temple U)
------------------------------------------------------------------------- */

#include "pair_lepton.h"

#include "atom.h"
#include "error.h"
#include "force.h"
#include "memory.h"
#include "neigh_list.h"
#include "update.h"

#include <LMP_Lepton.h>
#include <cstring>

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

PairLepton::PairLepton(LAMMPS *lmp) : Pair(lmp), cut(nullptr), type2expression(nullptr)
{
  respa_enable = 0;
  single_enable = 1;
  writedata = 1;
  restartinfo = 0;
  reinitflag = 0;
  cut_global = 0.0;
  centroidstressflag = CENTROID_SAME;
}

/* ---------------------------------------------------------------------- */

PairLepton::~PairLepton()
{
  if (allocated) {
    memory->destroy(cut);
    memory->destroy(cutsq);
    memory->destroy(setflag);
    memory->destroy(type2expression);
  }
}

/* ---------------------------------------------------------------------- */

void PairLepton::compute(int eflag, int vflag)
{
  int i, j, ii, jj, inum, jnum, itype, jtype;
  double xtmp, ytmp, ztmp, delx, dely, delz, evdwl, fpair;
  double rsq, factor_lj;
  int *ilist, *jlist, *numneigh, **firstneigh;

  evdwl = 0.0;
  ev_init(eflag, vflag);

  double **x = atom->x;
  double **f = atom->f;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  double *special_lj = force->special_lj;
  int newton_pair = force->newton_pair;

  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  std::vector<LMP_Lepton::CompiledExpression> force;
  std::vector<LMP_Lepton::CompiledExpression> epot;
  for (const auto &expr : expressions) {
    force.emplace_back(
        LMP_Lepton::Parser::parse(expr).differentiate("r").createCompiledExpression());
    if (eflag) epot.emplace_back(LMP_Lepton::Parser::parse(expr).createCompiledExpression());
  }

  // loop over neighbors of my atoms

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    itype = type[i];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      const double factor_lj = special_lj[sbmask(j)];
      j &= NEIGHMASK;

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx * delx + dely * dely + delz * delz;
      jtype = type[j];

      if (rsq < cutsq[itype][jtype]) {
        const double r = sqrt(rsq);
        const int idx = type2expression[itype][jtype];
        double &r_for = force[idx].getVariableReference("r");
        r_for = r;
        fpair = -force[idx].evaluate() / r;
        fpair *= factor_lj;

        f[i][0] += delx * fpair;
        f[i][1] += dely * fpair;
        f[i][2] += delz * fpair;
        if (newton_pair || j < nlocal) {
          f[j][0] -= delx * fpair;
          f[j][1] -= dely * fpair;
          f[j][2] -= delz * fpair;
        }

        if (eflag) {
          double &r_pot = epot[idx].getVariableReference("r");
          r_pot = r;
          evdwl = factor_lj * epot[idx].evaluate();
        } else
          evdwl = 0.0;

        if (evflag) ev_tally(i, j, nlocal, newton_pair, evdwl, 0.0, fpair, delx, dely, delz);
      }
    }
  }

  if (vflag_fdotr) virial_fdotr_compute();
}

/* ----------------------------------------------------------------------
   allocate all arrays
------------------------------------------------------------------------- */

void PairLepton::allocate()
{
  allocated = 1;
  int np1 = atom->ntypes + 1;

  memory->create(setflag, np1, np1, "pair:setflag");
  for (int i = 1; i < np1; i++)
    for (int j = i; j < np1; j++) setflag[i][j] = 0;

  memory->create(cut, np1, np1, "pair:cut");
  memory->create(cutsq, np1, np1, "pair:cutsq");
  memory->create(type2expression, np1, np1, "pair:type2expression");
}

/* ----------------------------------------------------------------------
   global settings
------------------------------------------------------------------------- */

void PairLepton::settings(int narg, char **arg)
{
  if (narg != 1) error->all(FLERR, "Illegal pair_style command");

  cut_global = utils::numeric(FLERR, arg[0], false, lmp);
}

/* ----------------------------------------------------------------------
   set coeffs for all type pairs
------------------------------------------------------------------------- */

void PairLepton::coeff(int narg, char **arg)
{
  if (narg < 3 || narg > 4) error->all(FLERR, "Incorrect number of args for pair coefficients");
  if (!allocated) allocate();

  int ilo, ihi, jlo, jhi;
  utils::bounds(FLERR, arg[0], 1, atom->ntypes, ilo, ihi, error);
  utils::bounds(FLERR, arg[1], 1, atom->ntypes, jlo, jhi, error);

  std::string exp_one = arg[2];
  double cut_one = cut_global;
  if (narg == 4) cut_one = utils::numeric(FLERR, arg[3], false, lmp);

  // check if the expression can be parsed and evaluated as needed without error
  try {
    auto epot = LMP_Lepton::Parser::parse(exp_one).createCompiledExpression();
    auto force = LMP_Lepton::Parser::parse(exp_one).differentiate("r").createCompiledExpression();
    double &r_pot = epot.getVariableReference("r");
    double &r_for = force.getVariableReference("r");
    r_for = r_pot = 1.0;
    epot.evaluate();
    force.evaluate();
  } catch (std::exception &e) {
    error->all(FLERR, e.what());
  }

  std::size_t idx = 0;
  for (const auto &exp : expressions) {
    if (exp == exp_one) break;
    ++idx;
  }

  // not found, add to list
  if ((expressions.size() == 0) || (idx == expressions.size())) expressions.push_back(exp_one);

  int count = 0;
  for (int i = ilo; i <= ihi; i++) {
    for (int j = MAX(jlo, i); j <= jhi; j++) {
      cut[i][j] = cut_one;
      setflag[i][j] = 1;
      type2expression[i][j] = idx;
      count++;
    }
  }

  if (count == 0) error->all(FLERR, "Incorrect args for pair coefficients");
}

/* ---------------------------------------------------------------------- */

double PairLepton::init_one(int i, int j)
{
  if (setflag[i][j] == 0) error->all(FLERR, "All pair coeffs are not set");

  cut[j][i] = cut[i][j];
  type2expression[j][i] = type2expression[i][j];

  return cut[i][j];
}

/* ----------------------------------------------------------------------
   proc 0 writes to data file
------------------------------------------------------------------------- */

void PairLepton::write_data(FILE *fp)
{
  for (int i = 1; i <= atom->ntypes; i++)
    fprintf(fp, "%d '%s' %g\n", i, expressions[type2expression[i][i]].c_str(), cut[i][i]);
}

/* ----------------------------------------------------------------------
   proc 0 writes all pairs to data file
------------------------------------------------------------------------- */

void PairLepton::write_data_all(FILE *fp)
{
  for (int i = 1; i <= atom->ntypes; i++)
    for (int j = i; j <= atom->ntypes; j++)
      fprintf(fp, "%d %d '%s' %g\n", i, j, expressions[type2expression[i][j]].c_str(), cut[i][j]);
}

/* ---------------------------------------------------------------------- */

double PairLepton::single(int /* i */, int /* j */, int itype, int jtype, double rsq,
                          double /* factor_coul */, double factor_lj, double &fforce)
{
  auto expr = expressions[type2expression[itype][jtype]];
  auto epot = LMP_Lepton::Parser::parse(expr).createCompiledExpression();
  auto force = LMP_Lepton::Parser::parse(expr).differentiate("r").createCompiledExpression();

  double r = sqrt(rsq);
  double &r_pot = epot.getVariableReference("r");
  double &r_for = force.getVariableReference("r");

  r_pot = r_for = r;
  fforce = -force.evaluate() / r;
  return epot.evaluate();
}
