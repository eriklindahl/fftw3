/*
 * Copyright (c) 2002 Matteo Frigo
 * Copyright (c) 2002 Steven G. Johnson
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

#include "api.h"

apiplan *X(mkapiplan)(unsigned int flags, problem *prb)
{
     plan *pln, *pln0;
     apiplan *p = (apiplan *)fftw_malloc(sizeof(apiplan), PLANS);
     planner *plnr = X(the_planner)();

     /* map API flags into FFTW flags */
     X(mapflags)(plnr, flags);

     /* create plan */
     plnr->planner_flags &= ~BLESSING;
     pln = plnr->adt->mkplan(plnr, prb);
     X(plan_use)(pln);
     AWAKE(pln, 1);

     /* build apiplan */
     p->pln = pln;
     p->prb = prb;

     /* blessing protocol */
     plnr->planner_flags |= BLESSING;
     pln0 = plnr->adt->mkplan(plnr, prb);
     X(plan_destroy)(pln0);

     /* discard all information not necessary to reconstruct the
	plan */
     plnr->adt->forget(plnr, FORGET_ACCURSED);

     return p;
}

void X(apiplan_destroy)(apiplan *p)
{
     AWAKE(p->pln, 0);
     X(plan_destroy)(p->pln);
     X(problem_destroy)(p->prb);
     X(free)(p);
}