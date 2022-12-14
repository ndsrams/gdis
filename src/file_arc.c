/*
Copyright (C) 2003 by Sean David Fleming

sean@ivec.org

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

The GNU GPL can also be found at http://www.gnu.org
*/

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gdis.h"
#include "coords.h"
#include "model.h"
#include "file.h"
#include "parse.h"
#include "matrix.h"
#include "interface.h"

#define DEBUG_MORE 0
#define MAX_KEYS 15

/* main structures */
extern struct sysenv_pak sysenv;
extern struct elem_pak elements[];

/****************************/
/* write a coordinate block */
/****************************/
gint write_arc_frame(FILE *fp, struct model_pak *model)
{
gint m, flag;
gdouble x[3];
GSList *clist, *mlist;
struct core_pak *core;
struct mol_pak *mol;
time_t t1;

/* get & print the date (streamlined slightly by sxm) */
t1 = time(NULL);
fprintf(fp,"!DATE %s",ctime(&t1));

if (model->periodic)
  {
  if (model->periodic == 2)
    {
/* saving a surface as a 3D model - make depth large enough to fit everything */
    fprintf(fp,"PBC %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f (P1)\n",
                model->pbc[0], model->pbc[1], 2.0*model->rmax,
                R2D*model->pbc[3],
                R2D*model->pbc[4],
                R2D*model->pbc[5]);
    }
  else
    {
    fprintf(fp,"PBC %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f (P1)\n",
                model->pbc[0], model->pbc[1], model->pbc[2],
                R2D*model->pbc[3],
                R2D*model->pbc[4],
                R2D*model->pbc[5]);
    }
  }

m=0;
for (mlist=model->moles ; mlist ; mlist=g_slist_next(mlist))
  {
  mol = mlist->data;

/* flag - atom written to file? (ie if UNDELETED) */
  flag=0;
/* m - number of atoms in molecule */
  for (clist=mol->cores ; clist ; clist=g_slist_next(clist))
    {
    core = clist->data;
    if (core->status & DELETED)
      continue;

/* everything is cartesian after latmat mult */
    ARR3SET(x, core->x);
    vecmat(model->latmat, x);
/* unique molecule numbering for BIOSYM files (>0) */
    if (core->atom_type)
      {
      fprintf(fp, "%-4s  %14.9f %14.9f %14.9f CORE %-6d %-7s %-2s ",
                  core->atom_label,x[0],x[1],x[2],m+1, core->atom_type,
                  elements[core->atom_code].symbol);
      }
    else
      {
      fprintf(fp, "%-4s  %14.9f %14.9f %14.9f CORE %-6d ?       %-2s ",
                  core->atom_label,x[0],x[1],x[2],m+1, 
                  elements[core->atom_code].symbol);
      }

    if (core->charge < 0.0)
      fprintf(fp, "%5.3f\n", core->charge);
    else
      fprintf(fp, " %4.3f\n", core->charge);

/* indicate we've written at least one atom */
    flag++;
    }
/* if at least one atom in the molecule was written - inc mol numbering */
  if (flag)
    {
    fprintf(fp,"end\n");
    m++;
    }
  }
fprintf(fp,"end\n");

return(0);
}

/***********************/
/* write biosym header */
/***********************/
gint write_arc_header(FILE *fp, struct model_pak *model)
{
/* print header */
fprintf(fp,"!BIOSYM archive 3\n");
if (model->periodic)
  fprintf(fp,"PBC=ON\n");
else
  fprintf(fp,"PBC=OFF\n");
gdis_blurb(fp);

return(0);
}

/******************/
/* Biosym writing */
/******************/
gint write_arc(gchar *filename, struct model_pak *data)
{
FILE *fp;

/* checks */
g_return_val_if_fail(data != NULL, 1);
g_return_val_if_fail(filename != NULL, 2);

/* open the file */
fp = fopen(filename, "wt");
if (!fp)
  return(3);

/* TODO - write multiple frames if it has them */
write_arc_header(fp, data);
write_arc_frame(fp, data);

fclose(fp);
return(0);
}

/****************************************************************/
/* determine if a BIOSYM fragment label was generated by MARVIN */
/****************************************************************/
gint is_marvin_label(const gchar *label)
{
if (label[0] != 'R' && label[0] != 'r')
  return(FALSE);

if (!g_ascii_isdigit(label[1]))
  return(FALSE);

if (label[2] != 'A' && label[2] != 'a' && label[2] != 'B' && label[2] != 'b')
  return(FALSE);

if (label[3] != 'C' && label[3] != 'c' && label[3] != 'S' && label[3] != 's')
  return(FALSE);

return(TRUE);
}

gint marvin_region(const gchar *label)
{
return(label[1] - '1');
}

gint marvin_core(const gchar *label)
{
if (label[3] == 'C' || label[3] == 'c')
  return(TRUE);
return(FALSE);
}

/*************************************************/
/* read a car/arc block into the model structure */
/*************************************************/
/* NB: assumes fp is at a line before !DATE line */
void read_arc_block(FILE *fp, struct model_pak *data)
{
gint region=0;
gint core_flag, end_count;
GSList *clist, *slist;
struct core_pak *core=NULL;
struct shel_pak *shel;
gchar *line,*tamp;
gdouble energy;
#define DEBUG_ARC 0
gchar c_type[5],c_ptype[8],c_symb[3];
gdouble x,y,z,charge;

g_assert(fp != NULL);
g_assert(data != NULL);

/* FIXME - this breaks measurement updates */
/*
free_core_list(data);
*/

/* init */
clist = data->cores;
slist = data->shels;
end_count=0;

	/*The energy is stored on the frame 1st line starting from the 65th caracter*/
line = file_read_line(fp);
sscanf(&(line[65])," %lf",&energy);
//this now trigger a heap-buffer-overflow, replacing with safer g_strdup_printf version
//sprintf(line,"%lf eV",energy);/*is it really always eV?*/
//g_sprintf(line,"%lf eV",energy);
//property_add_ranked(3,"Energy",line,data);
tamp=g_strdup_printf("%lf eV",energy);
property_add_ranked(3,"Energy",tamp,data);
g_free(tamp);
	/*Note that title of calculation is given in the first 64 character BUT it can be empty*/
/*get to the begining*/
while(g_ascii_strncasecmp("!date", line, 5)!=0) {
	g_free(line);
	line = file_read_line(fp);
}
g_free(line);/*should we do something with the !DATE line?*/
/* loop while there's data */
for (;;) {
/*we do not use the tokenized method to avoid some break in threaded environment*/
  line = file_read_line(fp);
  if(!line) break;

/* increment/reset end counter according to input line <- strstr at the begining of the line*/
  if (g_ascii_strncasecmp("end", line, 3) == 0) end_count++;
  else end_count = 0;
/* skip single end, terminate on double end */
  if (end_count == 1) {
	g_free(line);
	continue;
  }
  if (end_count == 2) break;/*normal exit*/

/* cell dimension search */
  if (g_ascii_strncasecmp("pbc", line, 3) == 0){
    data->pbc[5]=-1.;
sscanf(line,"PBC %lf %lf %lf %lf %lf %lf %*s",&(data->pbc[0]),&(data->pbc[1]),&(data->pbc[2]),&(data->pbc[3]),&(data->pbc[4]),&(data->pbc[5]));
#if DEBUG_ARC
fprintf(stdout,"#DBG: LINE:  %lf %lf %lf %lf %lf %lf\n",data->pbc[0],data->pbc[1],data->pbc[2],data->pbc[3],data->pbc[4],data->pbc[5]);
#endif
    if((data->pbc[5])>=0){/*6 tokens -> PCB=ON with or without implicit 2D*/
		data->pbc[3] *= D2R;
		data->pbc[4] *= D2R;
		data->pbc[5] *= D2R;
		if((data->pbc[2])==0){/*implicit 2D*/
			data->pbc[2] = 1.0;
			data->pbc[3] = 0.5*G_PI;
			data->pbc[4] = 0.5*G_PI;
			data->periodic = 2;
		}
    }else{/*less than 6 tokens -> PCB=2D */
		data->pbc[5] = D2R*(data->pbc[2]);
		data->pbc[2] = 1.0;
		data->pbc[3] = 0.5*G_PI;
		data->pbc[4] = 0.5*G_PI;
    }
#if DEBUG_ARC
fprintf(stdout,"#DBG: a=%lf b=%lf c=%lf alpha=%lf beta=%lf gamma=%lf\n",data->pbc[0],data->pbc[1],data->pbc[2],data->pbc[3],data->pbc[4],data->pbc[5]);
#endif
    matrix_lattice_init(data);/*initialize matrix now*/
    data->fractional = TRUE;/*set fractional now*/
    g_free(line);
    continue;
  }

/* each coord line is defined as:
 * [name], x, y, z, type, [seq. nb], pot. type, elem symbol, charge [, atom nb] */
  c_symb[0]='-';c_symb[1]='\0';
  sscanf(line,"%*s %lf %lf %lf %s %*s %s %s %lf %*s",&x,&y,&z,&(c_type[0]),&(c_ptype[0]),&(c_symb[0]),&charge);
#if DEBUG_ARC
fprintf(stdout,"#DBG: LINE: %lf %lf %lf %s %s %s %f\n",x,y,z,c_type,c_ptype,c_symb,charge);
#endif
  if(c_symb[0]=='-'){
	gui_text_show(ERROR, "unexpected end of file reading arc file\n");
	g_free(line);
	return;
  }
/* process core/shell candidates */
  if (elem_symbol_test(c_symb)){
	core_flag=TRUE;
	region = 0;
/* MARVIN core/shell/region parse */
	if (is_marvin_label(c_type)){
		if (!marvin_core(c_type)) core_flag = FALSE;
		region = marvin_region(c_type);
		data->region_empty[region] = FALSE;
	}
/* overwrite existing core (if any) */
	if (core_flag){
		if (clist){
			core = clist->data;
			clist = g_slist_next(clist);
		}else{
/* otherwise, add a new core */
/* NB: must append (not prepend) since we may overwrite & the order must be the same */
/* TODO - we could prepend cores (it's faster) and then reverse the list at the end */
/* but it's more complicated as we should ONLY reverse newly added cores */
			core=core_new(c_symb,"---",data);/*to make sure we don't need to free/dup atom_label all the time*/
			core->atom_type=g_malloc(9*sizeof(gchar));/*same here for atom_type*/
			data->cores = g_slist_append(data->cores, core);
		}
	
/* set the proper label */
	sprintf(core->atom_label,"%s",c_symb);/*FIX 7637ff*/

/* set values */
	core->x[0] = x;core->x[1] = y;core->x[2] = z;core->charge = charge;
if(data->periodic) {
	/* convert input cartesian coords to fractional now */
	vecmat(data->ilatmat, core->x);
	if(core->x[0]<0) core->x[0]+=1.0;
	if(core->x[1]<0) core->x[1]+=1.0;
	if(core->x[2]<0) core->x[2]+=1.0;
	if(core->x[0]>1.0) core->x[0]-=1.0;
	if(core->x[1]>1.0) core->x[1]-=1.0;
	if(core->x[2]>1.0) core->x[2]-=1.0;
}
	sprintf(core->atom_type,"%s",c_ptype);
	core->region = region;core->lookup_charge = FALSE;
	}/*FIX 7637ff (ugly one: in case core_flag==FALSE core=NULL and all core->xxx FAIL)*/	
  }else{
/* overwrite existing shell (if any) */
	if (slist){
        	shel = slist->data;
        	slist = g_slist_next(slist);
        }else{
/* otherwise, add a new shell */
		shel=shell_new(c_symb,"---",data);/*to make sure we don't need to free/dup shell_label all the time*/
		data->shels = g_slist_append(data->shels, shel);
	}
/* set the proper label */
  sprintf(shel->shell_label,"%s",c_symb);
/* set values */
  shel->x[0] = x;shel->x[1] = y;shel->x[2] = z;shel->charge = charge;
if(data->periodic) {
	/* convert input cartesian coords to fractional now */
	vecmat(data->ilatmat, shel->x);
	if(shel->x[0]<0) shel->x[0]+=1.0;
	if(shel->x[1]<0) shel->x[1]+=1.0;
	if(shel->x[2]<0) shel->x[2]+=1.0;
	if(shel->x[0]>1.0) shel->x[0]-=1.0;
	if(shel->x[1]>1.0) shel->x[1]-=1.0;
	if(shel->x[2]>1.0) shel->x[2]-=1.0;
}
  shel->region = region;shel->lookup_charge = FALSE;
  }
  g_free(line);
}
g_free(line);
}

/*********************/
/* biosym frame read */
/*********************/
gint read_arc_frame(FILE *fp, struct model_pak *data)
{
/* frame overwrite */
read_arc_block(fp, data);
return(0);
}

/******************/
/* Biosym reading */
/******************/
gint read_arc(gchar *filename, struct model_pak *data)
{
gint frame=0;
gchar *line;
FILE *fp;
long int fp_pos;
long int old_fp_pos=0;

/* checks */
g_return_val_if_fail(data != NULL, 1);
g_return_val_if_fail(filename != NULL, 2);

/* NEW - under windows, must be "rb" instead of "rt" - even though it's a text file */
fp = fopen(filename, "rb");
if (!fp)
  return(3);

/* loop while there's data */
for (;;)
  {
  fp_pos=ftell(fp);/*get the past line*/
  line = file_read_line(fp);
  if(!line) break;

/* pbc on/off search */
if(g_ascii_strncasecmp("pbc=on", line, 6) == 0) {
	data->periodic = 3;
	old_fp_pos=fp_pos;
	g_free(line);
	continue;
}
if(g_ascii_strncasecmp("pbc=off", line, 7) == 0) {
	data->periodic = 0;
	old_fp_pos=fp_pos;
	g_free(line);
	continue;
}
if(g_ascii_strncasecmp("pbc=2d", line, 6) == 0) {
	data->periodic = 2;
	old_fp_pos=fp_pos;
	g_free(line);
	continue;
}
/* coords search */
if(g_ascii_strncasecmp("!date", line, 5) == 0) {
/* NEW */
    fp_pos=ftell(fp);/*tag here*/
    fseek(fp,old_fp_pos,SEEK_SET);/* rewind a line before !date (to get energy) */
    add_frame_offset(fp, data);
    
/* only load the required frame */
    if (frame == data->cur_frame)
      read_arc_block(fp, data);
    fseek(fp,fp_pos,SEEK_SET);/*go just after !date*/
/* increment counter */
    frame++;
    }
  g_free(line);
  old_fp_pos=fp_pos;
  }
g_free(line);
/* got everything */
data->num_frames = frame;

/* get rid of frame list if only one frame */
if (data->num_frames == 1)
  {
  free_list(data->frame_list);
  data->frame_list = NULL;
  }

/* model setup */
strcpy(data->filename, filename);
g_free(data->basename);
data->basename = parse_strip(filename);

model_prep(data);

return(0);
}

