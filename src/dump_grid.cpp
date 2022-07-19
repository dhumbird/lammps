// clang-format off
/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   https://www.lammps.org/, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "dump_grid.h"

#include "arg_info.h"
#include "compute.h"
#include "domain.h"
#include "error.h"
#include "fix.h"
#include "memory.h"
#include "modify.h"
#include "region.h"
#include "update.h"

#include <cstring>

using namespace LAMMPS_NS;

// customize by adding keyword
// also customize compute_atom_property.cpp

enum{COMPUTE,FIX};

#define ONEFIELD 32
#define DELTA 1048576

/* ---------------------------------------------------------------------- */

DumpGrid::DumpGrid(LAMMPS *lmp, int narg, char **arg) :
  Dump(lmp, narg, arg)
{
  if (narg == 5) error->all(FLERR,"No dump grid arguments specified");

  clearstep = 1;

  nevery = utils::inumeric(FLERR,arg[3],false,lmp);
  if (nevery <= 0) error->all(FLERR,"Illegal dump grid command");

  // expand args if any have wildcard character "*"
  // ok to include trailing optional args,
  //   so long as they do not have "*" between square brackets
  // nfield may be shrunk below if extra optional args exist

  expand = 0;
  nfield = nargnew = utils::expand_args(FLERR,narg-5,&arg[5],1,earg,lmp);
  if (earg != &arg[5]) expand = 1;

  // allocate field vectors

  pack_choice = new FnPtrPack[nfield];
  vtype = new int[nfield];
  memory->create(field2index,nfield,"dump:field2index");
  memory->create(argindex,nfield,"dump:argindex");

  buffer_allow = 1;
  buffer_flag = 1;

  // computes and fixes which the dump accesses

  ncompute = 0;
  nfix = 0;

  // process attributes
  // ioptional = start of additional optional args in expanded args

  ioptional = parse_fields(nfield,earg);

  if (ioptional < nfield &&
      strcmp(style,"image") != 0 && strcmp(style,"movie") != 0)
    error->all(FLERR,"Invalid attribute {} in dump {} command",earg[ioptional],style);

  // noptional = # of optional args
  // reset nfield to subtract off optional args
  // reset ioptional to what it would be in original arg list
  // only dump image and dump movie styles process optional args,
  //   they do not use expanded earg list

  int noptional = nfield - ioptional;
  nfield -= noptional;
  size_one = nfield;
  ioptional = narg - noptional;

  // setup format strings

  vformat = new char*[nfield];
  std::string cols;

  cols.clear();
  for (int i = 0; i < nfield; i++) {
    if (vtype[i] == Dump::INT) cols += "%d ";
    else if (vtype[i] == Dump::DOUBLE) cols += "%g ";
    else if (vtype[i] == Dump::STRING) cols += "%s ";
    else if (vtype[i] == Dump::BIGINT) cols += BIGINT_FORMAT " ";
    vformat[i] = nullptr;
  }
  cols.resize(cols.size()-1);
  format_default = utils::strdup(cols);

  format_column_user = new char*[nfield];
  for (int i = 0; i < nfield; i++) format_column_user[i] = nullptr;

  // setup column string

  cols.clear();
  keyword_user.resize(nfield);
  for (int iarg = 0; iarg < nfield; iarg++) {
    key2col[earg[iarg]] = iarg;
    keyword_user[iarg].clear();
    if (cols.size()) cols += " ";
    cols += earg[iarg];
  }
  columns_default = utils::strdup(cols);
}

/* ---------------------------------------------------------------------- */

DumpGrid::~DumpGrid()
{
  // if wildcard expansion occurred, free earg memory from expand_args()
  // could not do in constructor, b/c some derived classes process earg

  if (expand) {
    for (int i = 0; i < nargnew; i++) delete[] earg[i];
    memory->sfree(earg);
  }

  delete[] pack_choice;
  delete[] vtype;
  memory->destroy(field2index);
  memory->destroy(argindex);

  delete[] idregion;

  for (int i = 0; i < ncompute; i++) delete[] id_compute[i];
  memory->sfree(id_compute);
  delete[] compute;

  for (int i = 0; i < nfix; i++) delete[] id_fix[i];
  memory->sfree(id_fix);
  delete[] fix;

  if (vformat) {
    for (int i = 0; i < nfield; i++) delete[] vformat[i];
    delete[] vformat;
  }

  if (format_column_user) {
    for (int i = 0; i < nfield; i++) delete[] format_column_user[i];
    delete[] format_column_user;
  }

  delete[] columns_default;
  delete[] columns;
}

/* ---------------------------------------------------------------------- */

void DumpGrid::init_style()
{
  // assemble ITEMS: column string from defaults and user values

  delete[] columns;
  std::string combined;
  int icol = 0;
  for (auto item : utils::split_words(columns_default)) {
    if (combined.size()) combined += " ";
    if (keyword_user[icol].size()) combined += keyword_user[icol];
    else combined += item;
    ++icol;
  }
  columns = utils::strdup(combined);

  // format = copy of default or user-specified line format

  delete[] format;
  if (format_line_user) format = utils::strdup(format_line_user);
  else format = utils::strdup(format_default);

  // tokenize the format string and add space at end of each format element
  // if user-specified int/float format exists, use it instead
  // if user-specified column format exists, use it instead
  // lo priority = line, medium priority = int/float, hi priority = column

  auto words = utils::split_words(format);
  if ((int) words.size() < nfield)
    error->all(FLERR,"Dump_modify format line is too short");

  int i=0;
  for (const auto &word : words) {
    delete[] vformat[i];

    if (format_column_user[i])
      vformat[i] = utils::strdup(std::string(format_column_user[i]) + " ");
    else if (vtype[i] == Dump::INT && format_int_user)
      vformat[i] = utils::strdup(std::string(format_int_user) + " ");
    else if (vtype[i] == Dump::DOUBLE && format_float_user)
      vformat[i] = utils::strdup(std::string(format_float_user) + " ");
    else if (vtype[i] == Dump::BIGINT && format_bigint_user)
      vformat[i] = utils::strdup(std::string(format_bigint_user) + " ");
    else vformat[i] = utils::strdup(word + " ");

    // remove trailing blank on last column's format
    if (i == nfield-1) vformat[i][strlen(vformat[i])-1] = '\0';

    ++i;
  }

  // setup boundary string

  domain->boundary_string(boundstr);

  // setup function ptrs

  if (binary && domain->triclinic == 0)
    header_choice = &DumpGrid::header_binary;
  else if (binary && domain->triclinic == 1)
    header_choice = &DumpGrid::header_binary_triclinic;
  else if (!binary && domain->triclinic == 0)
    header_choice = &DumpGrid::header_item;
  else if (!binary && domain->triclinic == 1)
    header_choice = &DumpGrid::header_item_triclinic;

  if (binary) write_choice = &DumpGrid::write_binary;
  else if (buffer_flag == 1) write_choice = &DumpGrid::write_string;
  else write_choice = &DumpGrid::write_lines;

  // find current ptr for each compute and fix
  // check that fix frequency is acceptable

  for (i = 0; i < ncompute; i++) {
    compute[i] = modify->get_compute_by_id(id_compute[i]);
    if (!compute[i]) error->all(FLERR,"Could not find dump grid compute ID {}",id_compute[i]);
  }

  for (i = 0; i < nfix; i++) {
    fix[i] = modify->get_fix_by_id(id_fix[i]);
    if (!fix[i]) error->all(FLERR,"Could not find dump grid fix ID {}", id_fix[i]);
    if (nevery % fix[i]->peratom_freq)
      error->all(FLERR,"Dump grid and fix not computed at compatible times");
  }

  // check validity of region

  if (idregion && !domain->get_region_by_id(idregion))
    error->all(FLERR,"Region {} for dump grid does not exist", idregion);

  // open single file, one time only

  if (multifile == 0) openfile();
}

/* ---------------------------------------------------------------------- */

void DumpGrid::write_header(bigint ndump)
{
  if (multiproc) (this->*header_choice)(ndump);
  else if (me == 0) (this->*header_choice)(ndump);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::format_magic_string_binary()
{
  // use negative ntimestep as marker for new format
  bigint fmtlen = strlen(MAGIC_STRING);
  bigint marker = -fmtlen;
  fwrite(&marker, sizeof(bigint), 1, fp);
  fwrite(MAGIC_STRING, sizeof(char), fmtlen, fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::format_endian_binary()
{
  int endian = ENDIAN;
  fwrite(&endian, sizeof(int), 1, fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::format_revision_binary()
{
  int revision = FORMAT_REVISION;
  fwrite(&revision, sizeof(int), 1, fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_unit_style_binary()
{
  int len = 0;
  if (unit_flag && !unit_count) {
    ++unit_count;
    len = strlen(update->unit_style);
    fwrite(&len, sizeof(int), 1, fp);
    fwrite(update->unit_style, sizeof(char), len, fp);
  } else {
    fwrite(&len, sizeof(int), 1, fp);
  }
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_columns_binary()
{
  int len = strlen(columns);
  fwrite(&len, sizeof(int), 1, fp);
  fwrite(columns, sizeof(char), len, fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_time_binary()
{
  char flag = time_flag ? 1 : 0;
  fwrite(&flag, sizeof(char), 1, fp);

  if (time_flag) {
    double t = compute_time();
    fwrite(&t, sizeof(double), 1, fp);
  }
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_format_binary()
{
  format_magic_string_binary();
  format_endian_binary();
  format_revision_binary();
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_binary(bigint ndump)
{
  header_format_binary();

  fwrite(&update->ntimestep,sizeof(bigint),1,fp);
  fwrite(&ndump,sizeof(bigint),1,fp);
  fwrite(&domain->triclinic,sizeof(int),1,fp);
  fwrite(&domain->boundary[0][0],6*sizeof(int),1,fp);
  fwrite(&boxxlo,sizeof(double),1,fp);
  fwrite(&boxxhi,sizeof(double),1,fp);
  fwrite(&boxylo,sizeof(double),1,fp);
  fwrite(&boxyhi,sizeof(double),1,fp);
  fwrite(&boxzlo,sizeof(double),1,fp);
  fwrite(&boxzhi,sizeof(double),1,fp);
  fwrite(&nfield,sizeof(int),1,fp);

  header_unit_style_binary();
  header_time_binary();
  header_columns_binary();

  if (multiproc) fwrite(&nclusterprocs,sizeof(int),1,fp);
  else fwrite(&nprocs,sizeof(int),1,fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_binary_triclinic(bigint ndump)
{
  header_format_binary();

  fwrite(&update->ntimestep,sizeof(bigint),1,fp);
  fwrite(&ndump,sizeof(bigint),1,fp);
  fwrite(&domain->triclinic,sizeof(int),1,fp);
  fwrite(&domain->boundary[0][0],6*sizeof(int),1,fp);
  fwrite(&boxxlo,sizeof(double),1,fp);
  fwrite(&boxxhi,sizeof(double),1,fp);
  fwrite(&boxylo,sizeof(double),1,fp);
  fwrite(&boxyhi,sizeof(double),1,fp);
  fwrite(&boxzlo,sizeof(double),1,fp);
  fwrite(&boxzhi,sizeof(double),1,fp);
  fwrite(&boxxy,sizeof(double),1,fp);
  fwrite(&boxxz,sizeof(double),1,fp);
  fwrite(&boxyz,sizeof(double),1,fp);
  fwrite(&nfield,sizeof(int),1,fp);

  header_unit_style_binary();
  header_time_binary();
  header_columns_binary();

  if (multiproc) fwrite(&nclusterprocs,sizeof(int),1,fp);
  else fwrite(&nprocs,sizeof(int),1,fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_item(bigint ndump)
{
  if (unit_flag && !unit_count) {
    ++unit_count;
    fmt::print(fp,"ITEM: UNITS\n{}\n",update->unit_style);
  }
  if (time_flag) fmt::print(fp,"ITEM: TIME\n{:.16}\n",compute_time());

  fmt::print(fp,"ITEM: TIMESTEP\n{}\n"
             "ITEM: NUMBER OF ATOMS\n{}\n",
             update->ntimestep, ndump);

  fmt::print(fp,"ITEM: BOX BOUNDS {}\n"
             "{:>1.16e} {:>1.16e}\n"
             "{:>1.16e} {:>1.16e}\n"
             "{:>1.16e} {:>1.16e}\n",
             boundstr,boxxlo,boxxhi,boxylo,boxyhi,boxzlo,boxzhi);

  fmt::print(fp,"ITEM: ATOMS {}\n",columns);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::header_item_triclinic(bigint ndump)
{
  if (unit_flag && !unit_count) {
    ++unit_count;
    fmt::print(fp,"ITEM: UNITS\n{}\n",update->unit_style);
  }
  if (time_flag) fmt::print(fp,"ITEM: TIME\n{:.16}\n",compute_time());

  fmt::print(fp,"ITEM: TIMESTEP\n{}\n"
             "ITEM: NUMBER OF ATOMS\n{}\n",
             update->ntimestep, ndump);

  fmt::print(fp,"ITEM: BOX BOUNDS xy xz yz {}\n"
             "{:>1.16e} {:>1.16e} {:>1.16e}\n"
             "{:>1.16e} {:>1.16e} {:>1.16e}\n"
             "{:>1.16e} {:>1.16e} {:>1.16e}\n",
             boundstr,boxxlo,boxxhi,boxxy,boxylo,boxyhi,boxxz,boxzlo,boxzhi,boxyz);

  fmt::print(fp,"ITEM: ATOMS {}\n",columns);
}

/* ---------------------------------------------------------------------- */

int DumpGrid::count()
{
  int i;

  // grow choose arrays if needed
  // NOTE: needs to change

  /*
  const int nlocal = atom->nlocal;
  if (atom->nmax > maxlocal) {
    maxlocal = atom->nmax;

    memory->destroy(choose);
    memory->destroy(dchoose);
    memory->destroy(clist);
    memory->create(choose,maxlocal,"dump:choose");
    memory->create(dchoose,maxlocal,"dump:dchoose");
    memory->create(clist,maxlocal,"dump:clist");
  }
  */

  // invoke Computes for per-grid quantities
  // only if within a run or minimize
  // else require that computes are current
  // this prevents a compute from being invoked by the WriteDump class

  if (ncompute) {
    if (update->whichflag == 0) {
      for (i = 0; i < ncompute; i++)
        if (compute[i]->invoked_pergrid != update->ntimestep)
          error->all(FLERR,"Compute used in dump between runs is not current");
    } else {
      for (i = 0; i < ncompute; i++) {
        if (!(compute[i]->invoked_flag & Compute::INVOKED_PERGRID)) {
          compute[i]->compute_pergrid();
          compute[i]->invoked_flag |= Compute::INVOKED_PERGRID;
        }
      }
    }
  }

  // choose all local grid pts for output
  // NOTE: this needs to change

  //for (i = 0; i < nlocal; i++) choose[i] = 1;

  // un-choose if not in region
  // NOTE: this needs to change

  if (idregion) {
    auto region = domain->get_region_by_id(idregion);
    region->prematch();
    /*
    double **x = atom->x;
    for (i = 0; i < nlocal; i++)
      if (choose[i] && region->match(x[i][0],x[i][1],x[i][2]) == 0)
        choose[i] = 0;
    */
  }

  // compress choose flags into clist
  // nchoose = # of selected atoms
  // clist[i] = local index of each selected atom
  // NOTE: this neds to change

  nchoose = 0;
  /*
  for (i = 0; i < nlocal; i++)
    if (choose[i]) clist[nchoose++] = i;
  */

  return nchoose;
}

/* ---------------------------------------------------------------------- */

void DumpGrid::pack(tagint *ids)
{
  for (int n = 0; n < size_one; n++) (this->*pack_choice[n])(n);
  // NOTE: this needs to be grid IDs ?
  /*
  if (ids) {
    tagint *tag = atom->tag;
    for (int i = 0; i < nchoose; i++)
      ids[i] = tag[clist[i]];
  }
  */
}

/* ----------------------------------------------------------------------
   convert mybuf of doubles to one big formatted string in sbuf
   return -1 if strlen exceeds an int, since used as arg in MPI calls in Dump
------------------------------------------------------------------------- */

int DumpGrid::convert_string(int n, double *mybuf)
{
  int i,j;

  int offset = 0;
  int m = 0;
  for (i = 0; i < n; i++) {
    if (offset + nfield*ONEFIELD > maxsbuf) {
      if ((bigint) maxsbuf + DELTA > MAXSMALLINT) return -1;
      maxsbuf += DELTA;
      memory->grow(sbuf,maxsbuf,"dump:sbuf");
    }

    for (j = 0; j < nfield; j++) {
      if (vtype[j] == Dump::INT)
        offset += sprintf(&sbuf[offset],vformat[j],static_cast<int> (mybuf[m]));
      else if (vtype[j] == Dump::DOUBLE)
        offset += sprintf(&sbuf[offset],vformat[j],mybuf[m]);
      else if (vtype[j] == Dump::BIGINT)
        offset += sprintf(&sbuf[offset],vformat[j],
                          static_cast<bigint> (mybuf[m]));
      m++;
    }
    offset += sprintf(&sbuf[offset],"\n");
  }

  return offset;
}

/* ---------------------------------------------------------------------- */

void DumpGrid::write_data(int n, double *mybuf)
{
  (this->*write_choice)(n,mybuf);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::write_binary(int n, double *mybuf)
{
  n *= size_one;
  fwrite(&n,sizeof(int),1,fp);
  fwrite(mybuf,sizeof(double),n,fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::write_string(int n, double *mybuf)
{
  if (mybuf)
    fwrite(mybuf,sizeof(char),n,fp);
}

/* ---------------------------------------------------------------------- */

void DumpGrid::write_lines(int n, double *mybuf)
{
  int i,j;

  int m = 0;
  for (i = 0; i < n; i++) {
    for (j = 0; j < nfield; j++) {
      if (vtype[j] == Dump::INT) fprintf(fp,vformat[j],static_cast<int> (mybuf[m]));
      else if (vtype[j] == Dump::DOUBLE) fprintf(fp,vformat[j],mybuf[m]);
      else if (vtype[j] == Dump::BIGINT)
        fprintf(fp,vformat[j],static_cast<bigint> (mybuf[m]));
      m++;
    }
    fprintf(fp,"\n");
  }
}

/* ---------------------------------------------------------------------- */

int DumpGrid::parse_fields(int narg, char **arg)
{
  // customize by adding to if statement

  for (int iarg = 0; iarg < narg; iarg++) {

    int n,flag,cols;
    ArgInfo argi(arg[iarg], ArgInfo::COMPUTE | ArgInfo::FIX);
    argindex[iarg] = argi.get_index1();
    auto name = argi.get_name();
    Compute *icompute = nullptr;
    Fix *ifix = nullptr;
      
    switch (argi.get_type()) {
        
    case ArgInfo::UNKNOWN:
      error->all(FLERR,"Invalid attribute in dump grid command");
      break;

    // compute value = c_ID
    // if no trailing [], then arg is set to 0, else arg is int between []
      
    case ArgInfo::COMPUTE:
      pack_choice[iarg] = &DumpGrid::pack_compute;
      vtype[iarg] = Dump::DOUBLE;

      icompute = modify->get_compute_by_id(name);
      if (!icompute) error->all(FLERR,"Could not find dump grid compute ID: {}",name);
      if (icompute->pergrid_flag == 0)
        error->all(FLERR,"Dump grid compute {} does not compute per-grid info",name);
      /*
        if (argi.get_dim() == 0 && icompute->size_pergrid_cols > 0)
        error->all(FLERR,"Dump grid compute {} does not calculate per-grid vector",name);
        if (argi.get_dim() > 0 && icompute->size_pergrid_cols == 0)
        error->all(FLERR,"Dump grid compute {} does not calculate per-grid array",name);
        if (argi.get_dim() > 0 && argi.get_index1() > icompute->size_pergrid_cols)
        error->all(FLERR,"Dump grid compute {} vector is accessed out-of-range",name);
      */
      
      field2index[iarg] = add_compute(name);
      break;

    // fix value = f_ID
    // if no trailing [], then arg is set to 0, else arg is between []
    
    case ArgInfo::FIX:
      pack_choice[iarg] = &DumpGrid::pack_fix;
      vtype[iarg] = Dump::DOUBLE;

      // name = idfix:gname:fname, split into 3 strings

      char *ptr = strchr(name,':');
      if (!ptr) error->all(FLERR,"Dump grid fix {} does not contain 2 ':' chars");
      *ptr = '\0';
      int n = strlen(name) + 1;
      char *gname = new char[n];
      strcpy(gname,name);
      char *ptr2 = strchr(ptr+1,':');
      if (!ptr) error->all(FLERR,"Dump grid fix {} does not contain 2 ':' chars");
      int n = strlen(ptr+1) + 1;
      char *fname = new char[n];
      strcpy(fname,ptr+1);
      *ptr = ':';
      *ptr2 = ':';

      // error check

      ifix = modify->get_fix_by_id(idfix);
      if (!ifix) error->all(FLERR,"Could not find dump grid fix ID: {}",idfix);
      if (ifix->pergrid_flag == 0)
        error->all(FLERR,"Dump grid fix {} does not compute per-atom info",idfix);
      
      int dim;
      void *grid = ifix->grid_find_name(gname,dim);
      if (!grid) error->all(FLERR,"Dump grid fix {} does not recognize grid {}",
                            idfix,gname);
      
      Grid2d *grid2d;
      Grid3d *grid3d;
      if (dim == 2) grid2d = (Grid2d *) grid;
      if (dim == 3) grid2d = (Grid3d *) grid;

      int ncol;
      void *field = ifix->grid_find_field(fname,ncol);
      if (!grid) error->all(FLERR,"Dump grid fix {} does not recognize field {}",
                            idfix,fname);

      if (argi.get_dim() == 0 && ncol)
        error->all(FLERR,"Dump grid fix {} field {} is not per-grid vector",
                   idfix,fname);
      if (argi.get_dim() > 0 && ncol == 0) 
        error->all(FLERR,"Dump grid fix {} field {} is not per-grid array",
                   idfix,fname);
      if (argi.get_dim() > 0 && argi.get_index1() > ncol)
        error->all(FLERR,"Dump grid fix {} array {} is accessed out-of-range",
                   idfix,fname);

      if (ncol == 0) {
        if (dim == 2) vec2d = (double **) field;
        if (dim == 3) vec3d = (double ***) field;
      } else if (ncol) {
        if (dim == 2) array2d = (double ***) field;
        if (dim == 3) array3d = (double ****) field;
      }
      
      field2index[iarg] = add_fix(idfix);
      break;

    // no match

    default:
      return iarg;
      break;
    }
  }
    
  return narg;
}

/* ----------------------------------------------------------------------
   add Compute to list of Compute objects used by dump
   return index of where this Compute is in list
   if already in list, do not add, just return index, else add to list
------------------------------------------------------------------------- */

int DumpGrid::add_compute(const char *id)
{
  int icompute;
  for (icompute = 0; icompute < ncompute; icompute++)
    if (strcmp(id,id_compute[icompute]) == 0) break;
  if (icompute < ncompute) return icompute;

  id_compute = (char **)
    memory->srealloc(id_compute,(ncompute+1)*sizeof(char *),"dump:id_compute");
  delete[] compute;
  compute = new Compute*[ncompute+1];

  id_compute[ncompute] = utils::strdup(id);
  ncompute++;
  return ncompute-1;
}

/* ----------------------------------------------------------------------
   add Fix to list of Fix objects used by dump
   return index of where this Fix is in list
   if already in list, do not add, just return index, else add to list
------------------------------------------------------------------------- */

int DumpGrid::add_fix(const char *id)
{
  int ifix;
  for (ifix = 0; ifix < nfix; ifix++)
    if (strcmp(id,id_fix[ifix]) == 0) break;
  if (ifix < nfix) return ifix;

  id_fix = (char **)
    memory->srealloc(id_fix,(nfix+1)*sizeof(char *),"dump:id_fix");
  delete[] fix;
  fix = new Fix*[nfix+1];

  id_fix[nfix] = utils::strdup(id);
  nfix++;
  return nfix-1;
}

/* ---------------------------------------------------------------------- */

int DumpGrid::modify_param(int narg, char **arg)
{
  if (strcmp(arg[0],"region") == 0) {
    if (narg < 2) error->all(FLERR,"Illegal dump_modify command");
    if (strcmp(arg[1],"none") == 0) {
      delete[] idregion;
      idregion = nullptr;
    } else {
      delete[] idregion;
      if (!domain->get_region_by_id(arg[1]))
        error->all(FLERR,"Dump_modify region {} does not exist", arg[1]);
      idregion = utils::strdup(arg[1]);
    }
    return 2;
  }

  if (strcmp(arg[0],"format") == 0) {
    if (narg < 2) error->all(FLERR,"Illegal dump_modify command");

    if (strcmp(arg[1],"none") == 0) {
      // just clear format_column_user allocated by this dump child class
      for (int i = 0; i < nfield; i++) {
        delete[] format_column_user[i];
        format_column_user[i] = nullptr;
      }
      return 2;
    }

    if (narg < 3) error->all(FLERR,"Illegal dump_modify command");

    if (strcmp(arg[1],"int") == 0) {
      delete[] format_int_user;
      format_int_user = utils::strdup(arg[2]);
      delete[] format_bigint_user;
      int n = strlen(format_int_user) + 8;
      format_bigint_user = new char[n];
      // replace "d" in format_int_user with bigint format specifier
      // use of &str[1] removes leading '%' from BIGINT_FORMAT string
      char *ptr = strchr(format_int_user,'d');
      if (ptr == nullptr)
        error->all(FLERR,"Dump_modify int format does not contain d character");
      char str[8];
      sprintf(str,"%s",BIGINT_FORMAT);
      *ptr = '\0';
      sprintf(format_bigint_user,"%s%s%s",format_int_user,&str[1],ptr+1);
      *ptr = 'd';

    } else if (strcmp(arg[1],"float") == 0) {
      delete[] format_float_user;
      format_float_user = utils::strdup(arg[2]);

    } else {
      int i = utils::inumeric(FLERR,arg[1],false,lmp) - 1;
      if (i < 0 || i >= nfield)
        error->all(FLERR,"Illegal dump_modify command");
      delete[] format_column_user[i];
      format_column_user[i] = utils::strdup(arg[2]);
    }
    return 3;
  }

  return 0;
}

/* ----------------------------------------------------------------------
   return # of bytes of allocated memory in buf, choose, variable arrays
------------------------------------------------------------------------- */

double DumpGrid::memory_usage()
{
  double bytes = Dump::memory_usage();
  bytes += memory->usage(choose,maxlocal);
  bytes += memory->usage(dchoose,maxlocal);
  bytes += memory->usage(clist,maxlocal);
  return bytes;
}

/* ----------------------------------------------------------------------
   extraction of Compute and Fix data
------------------------------------------------------------------------- */

void DumpGrid::pack_compute(int n)
{
  double *vector = compute[field2index[n]]->vector_atom;
  double **array = compute[field2index[n]]->array_atom;
  int index = argindex[n];

  if (index == 0) {
    for (int i = 0; i < nchoose; i++) {
      buf[n] = vector[clist[i]];
      n += size_one;
    }
  } else {
    index--;
    for (int i = 0; i < nchoose; i++) {
      buf[n] = array[clist[i]][index];
      n += size_one;
    }
  }
}

/* ---------------------------------------------------------------------- */

void DumpGrid::pack_fix(int n)
{
  double *vector = fix[field2index[n]]->vector_atom;
  double **array = fix[field2index[n]]->array_atom;
  int index = argindex[n];

  if (index == 0) {
    for (int i = 0; i < nchoose; i++) {
      buf[n] = vector[clist[i]];
      n += size_one;
    }
  } else {
    index--;
    for (int i = 0; i < nchoose; i++) {
      buf[n] = array[clist[i]][index];
      n += size_one;
    }
  }
}

