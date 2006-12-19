/*
 * Copyright (c) 2003, 2006 Matteo Frigo
 * Copyright (c) 2003, 2006 Massachusetts Institute of Technology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* plans for distributed out-of-place transpose using MPI_Alltoall,
   and which destroy the input array (unless TRANSPOSED_IN is used) */

#include "mpi-transpose.h"
#include <string.h>

typedef struct {
     solver super;
     int copy_transposed_in; /* whether to copy the input for TRANSPOSED_IN,
				which makes the final transpose out-of-place
				but costs an extra copy and requires us
				to destroy the input */
} S;

typedef struct {
     plan_mpi_transpose super;

     plan *cld1, *cld2, *cld2rest;

     MPI_Comm comm;
     int *send_block_sizes, *send_block_offsets;
     int *recv_block_sizes, *recv_block_offsets;

     INT rest_Ioff, rest_Ooff;

     int equal_blocks;
} P;

static void apply(const plan *ego_, R *I, R *O)
{
     const P *ego = (const P *) ego_;
     plan_rdft *cld1, *cld2, *cld2rest;

     /* transpose locally to get contiguous chunks */
     cld1 = (plan_rdft *) ego->cld1;
     if (cld1) {
	  cld1->apply(ego->cld1, I, O);
	  
	  /* transpose chunks globally */
	  if (ego->equal_blocks)
	       MPI_Alltoall(O, ego->send_block_sizes[0], FFTW_MPI_TYPE,
			    I, ego->recv_block_sizes[0], FFTW_MPI_TYPE,
			    ego->comm);
	  else
	       MPI_Alltoallv(O, ego->send_block_sizes, ego->send_block_offsets,
			     FFTW_MPI_TYPE,
			     I, ego->recv_block_sizes, ego->recv_block_offsets,
			     FFTW_MPI_TYPE,
			     ego->comm);
     }
     else { /* TRANSPOSED_IN, no need to destroy input */
	  /* transpose chunks globally */
	  if (ego->equal_blocks)
	       MPI_Alltoall(I, ego->send_block_sizes[0], FFTW_MPI_TYPE,
			    O, ego->recv_block_sizes[0], FFTW_MPI_TYPE,
			    ego->comm);
	  else
	       MPI_Alltoallv(I, ego->send_block_sizes, ego->send_block_offsets,
			     FFTW_MPI_TYPE,
			     O, ego->recv_block_sizes, ego->recv_block_offsets,
			     FFTW_MPI_TYPE,
			     ego->comm);
	  I = O; /* final transpose (if any) is in-place */
     }
     
     /* transpose locally, again, to get ordinary row-major */
     cld2 = (plan_rdft *) ego->cld2;
     if (cld2) {
	  cld2->apply(ego->cld2, I, O);
	  cld2rest = (plan_rdft *) ego->cld2rest;
	  if (cld2rest) /* leftover from unequal block sizes */
	       cld2rest->apply(ego->cld2rest,
			       I + ego->rest_Ioff, O + ego->rest_Ooff);
     }
}

static int applicable(const S *ego, const problem *p_,
		      const planner *plnr)
{
     const problem_mpi_transpose *p = (const problem_mpi_transpose *) p_;
     return (1
	     && p->I != p->O
	     && (!NO_DESTROY_INPUTP(plnr) || 
		 ((p->flags & TRANSPOSED_IN) && !ego->copy_transposed_in))
	     && ((p->flags & TRANSPOSED_IN) || !ego->copy_transposed_in)
	     && !SCRAMBLEDP(p->flags)
	  );
}

static void awake(plan *ego_, enum wakefulness wakefulness)
{
     P *ego = (P *) ego_;
     X(plan_awake)(ego->cld1, wakefulness);
     X(plan_awake)(ego->cld2, wakefulness);
     X(plan_awake)(ego->cld2rest, wakefulness);
}

static void destroy(plan *ego_)
{
     P *ego = (P *) ego_;
     X(ifree0)(ego->send_block_sizes);
     MPI_Comm_free(&ego->comm);
     X(plan_destroy_internal)(ego->cld2rest);
     X(plan_destroy_internal)(ego->cld2);
     X(plan_destroy_internal)(ego->cld1);
}

static void print(const plan *ego_, printer *p)
{
     const P *ego = (const P *) ego_;
     p->print(p, "(mpi-transpose-alltoall");
     if (ego->equal_blocks) p->print(p, "/e");
     if (ego->cld1) p->print(p, "%(%p%)", ego->cld1);
     if (ego->cld2) p->print(p, "%(%p%)", ego->cld2);
     if (ego->cld2rest) p->print(p, "%(%p%)", ego->cld2rest);
     p->print(p, ")");
}

static plan *mkplan(const solver *ego_, const problem *p_, planner *plnr)
{
     const S *ego = (const S *) ego_;
     const problem_mpi_transpose *p;
     P *pln;
     plan *cld1 = 0, *cld2 = 0, *cld2rest = 0;
     INT b, bt, nxb, vn, Ioff = 0, Ooff = 0;
     R *I;
     int *sbs, *sbo, *rbs, *rbo;
     int pe, my_pe, n_pes;
     int equal_blocks = 1;
     static const plan_adt padt = {
          XM(transpose_solve), awake, print, destroy
     };

     if (!applicable(ego, p_, plnr))
          return (plan *) 0;

     p = (const problem_mpi_transpose *) p_;
     vn = p->vn;

     MPI_Comm_rank(p->comm, &my_pe);
     MPI_Comm_size(p->comm, &n_pes);

     b = XM(block)(p->nx, p->block, my_pe);

     if (p->flags & TRANSPOSED_IN) { /* I is already transposed */
	  if (ego->copy_transposed_in) {
	       cld1 = X(mkplan_d)(plnr,
				  X(mkproblem_rdft_0_d)(X(mktensor_1d)
							(b * p->ny * vn, 1, 1),
							I = p->I, p->O));
	       if (XM(any_true)(!cld1, p->comm)) goto nada;
	  }
	  else
	       I = p->O; /* final transpose is in-place */
     }
     else { /* transpose b x ny x vn -> ny x b x vn */
	  cld1 = X(mkplan_d)(plnr, 
			     X(mkproblem_rdft_0_d)(X(mktensor_3d)
						   (b, p->ny * vn, vn,
						    p->ny, vn, b * vn,
						    vn, 1, 1),
						   I = p->I, p->O));
	  if (XM(any_true)(!cld1, p->comm)) goto nada;
     }
	  
     bt = XM(block)(p->ny, p->tblock, my_pe);
     nxb = (p->nx + p->block - 1) / p->block;
     if (p->nx != nxb * p->block)
	  nxb -= 1; /* number of equal-sized blocks */
     if (!(p->flags & TRANSPOSED_OUT)) {
	  INT nx = p->nx * vn;
	  b = p->block * vn;
	  cld2 = X(mkplan_d)(plnr, 
			     X(mkproblem_rdft_0_d)(X(mktensor_3d)
						   (nxb, bt * b, b,
						    bt, b, nx,
						    b, 1, 1),
						   I, p->O));
	  if (XM(any_true)(!cld2, p->comm)) goto nada;
	  
	  if (p->nx != nxb * p->block) { /* leftover blocks to transpose */
	       Ioff = bt * b * nxb;
	       Ooff = b * nxb;
	       b = nx - nxb * b;
	       cld2rest = X(mkplan_d)(plnr,
				      X(mkproblem_rdft_0_d)(X(mktensor_2d)
							    (bt, b, nx,
							     b, 1, 1),
							    I + Ioff,
							    p->O + Ooff));
	       if (XM(any_true)(!cld2rest, p->comm)) goto nada;
	  }
     }
     else { /* TRANSPOSED_OUT */
	  b = p->block;
	  cld2 = X(mkplan_d)(plnr, 
			     X(mkproblem_rdft_0_d)(X(mktensor_4d)
						   (nxb, bt * b*vn, bt * b*vn,
						    bt, b*vn, vn,
						    b, vn, bt*vn,
						    vn, 1, 1),
						   I, p->O));
	  if (XM(any_true)(!cld2, p->comm)) goto nada;
	  
	  if (p->nx != nxb * p->block) { /* leftover blocks to transpose */
	       Ioff = Ooff = bt * b * nxb * vn;
	       b = p->nx - nxb * b;
	       cld2rest = X(mkplan_d)(plnr,
				      X(mkproblem_rdft_0_d)(X(mktensor_3d)
							    (bt, b*vn, vn,
							     b, vn, bt*vn,
							     vn, 1, 1),
							    I + Ioff,
							    p->O + Ooff));
	       if (XM(any_true)(!cld2rest, p->comm)) goto nada;
	  }
     }

     pln = MKPLAN_MPI_TRANSPOSE(P, &padt, apply);

     pln->cld1 = cld1;
     pln->cld2 = cld2;
     pln->cld2rest = cld2rest;
     pln->rest_Ioff = Ioff;
     pln->rest_Ooff = Ooff;

     MPI_Comm_dup(p->comm, &pln->comm);

     /* Compute sizes/offsets of blocks to send for all-to-all
        command.  TODO: In the special case where all block sizes are
        equal, we could use the MPI_Alltoall command.  It's not clear
        whether/why this would be any faster, though. */
     sbs = (int *) MALLOC(4 * n_pes * sizeof(int), PLANS);
     sbo = sbs + n_pes;
     rbs = sbo + n_pes;
     rbo = rbs + n_pes;
     b = XM(block)(p->nx, p->block, my_pe);
     bt = XM(block)(p->ny, p->tblock, my_pe);
     for (pe = 0; pe < n_pes; ++pe) {
	  INT db, dbt; /* destination block sizes */
	  db = XM(block)(p->nx, p->block, pe);
	  dbt = XM(block)(p->ny, p->tblock, pe);

	  /* MPI requires type "int" here; apparently it
	     has no 64-bit API?  Grrr. */
	  sbs[pe] = (int) (b * dbt * vn);
	  sbo[pe] = (int) (pe * (b * p->tblock) * vn);
	  rbs[pe] = (int) (db * bt * vn);
	  rbo[pe] = (int) (pe * (p->block * bt) * vn);
	  if (sbs[pe] != (b * p->tblock) * vn
	      || rbs[pe] != (p->block * bt) * vn)
	       equal_blocks = 0;
     }
     pln->send_block_sizes = sbs;
     pln->send_block_offsets = sbo;
     pln->recv_block_sizes = rbs;
     pln->recv_block_offsets = rbo;
     pln->equal_blocks = equal_blocks;

     X(ops_zero)(&pln->super.super.ops);
     if (cld1) X(ops_add2)(&cld1->ops, &pln->super.super.ops);
     if (cld2) X(ops_add2)(&cld2->ops, &pln->super.super.ops);
     if (cld2rest) X(ops_add2)(&cld2rest->ops, &pln->super.super.ops);
     /* FIXME: should MPI operations be counted in "other" somehow? */

     return &(pln->super.super);

 nada:
     X(plan_destroy_internal)(cld2rest);
     X(plan_destroy_internal)(cld2);
     X(plan_destroy_internal)(cld1);
     return (plan *) 0;
}

static solver *mksolver(int copy_transposed_in)
{
     static const solver_adt sadt = { PROBLEM_MPI_TRANSPOSE, mkplan };
     S *slv = MKSOLVER(S, &sadt);
     slv->copy_transposed_in = copy_transposed_in;
     return &(slv->super);
}

void XM(transpose_alltoall_register)(planner *p)
{
     REGISTER_SOLVER(p, mksolver(0));
     REGISTER_SOLVER(p, mksolver(1));
}