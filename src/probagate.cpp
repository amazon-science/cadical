#include "internal.hpp"
#include "macros.hpp"

namespace CaDiCaL {

// These functions are used for probagating (note the 'b') during failed
// literal probing in simplification mode, as replacement of the generic
// propagation routine 'propagate' in 'propagate.cpp'.

// The code is mostly copied from 'propagate.cpp' and specialized.  We only
// comment on the differences.  More explanations are in 'propagate.cpp'.

inline void Internal::probe_assign (int lit, Clause * reason) {
  assert (simplifying);
  int idx = vidx (lit);
  assert (!vals[idx]);
  assert (!flags (idx).eliminated || !reason);
  Var & v = var (idx);
  v.level = level;
  v.reason = reason;
  if (!level) learn_unit_clause (lit);
  const signed char tmp = sign (lit);
  vals[idx] = tmp;
  vals[-idx] = -tmp;
  assert (val (lit) > 0);
  assert (val (-lit) < 0);
  // no phase saving here ...
  propfixed (lit) = stats.fixed;
  trail.push_back (lit);
  LOG (reason, "assign %d", lit);
  assert (watches ());
  if (opts.prefetch)
    __builtin_prefetch (&*(watches (-lit).begin ()));
}

void Internal::probe_assign_decision (int lit) {
  assert (level == 1);
  probe_assign (lit, 0);
}

void Internal::probe_assign_unit (int lit) {
  assert (!level);
  assert (!active (lit));
  probe_assign (lit, 0);
}

// This is essentially the same as 'propagate' except that we prioritize and
// always propagated binary clauses first (see our CPAIOR paper on tree
// based look ahead), stop at a conflict and of course use 'probe_assign'
// instead of 'search_assign'.  Statistics counters are also different.

bool Internal::probagate () {

  assert (simplifying);
  assert (!unsat);

  START (probagate);

  long before = probagated2;

  while (!conflict) {
    if (probagated2 < trail.size ()) {
      const int lit = -trail[probagated2++];
      LOG ("probagating %d over binary clauses", -lit);
      Watches & ws = watches (lit);
      const_watch_iterator i = ws.begin ();
      watch_iterator j = ws.begin ();
      while (!conflict && i != ws.end ()) {
	const Watch w = *j++ = *i++;
	if (!w.binary) continue;
	const int b = val (w.blit);
	if (b > 0) continue;
	if (b < 0) conflict = w.clause;
	else probe_assign (w.blit, w.clause);
      }
    } else if (probagated < trail.size ()) {
      const int lit = -trail[probagated++];
      LOG ("probagating %d over large clauses", -lit);
      Watches & ws = watches (lit);
      const_watch_iterator i = ws.begin ();
      watch_iterator j = ws.begin ();
      while (i != ws.end ()) {
	const Watch w = *j++ = *i++;
	if (w.binary) continue;
	const int b = val (w.blit);
	if (b > 0) continue;
        if (w.clause->garbage) continue;
        literal_iterator lits = w.clause->begin ();
        if (lits[0] == lit) swap (lits[0], lits[1]);
        assert (lits[1] == lit);
        const int u = val (lits[0]);
        if (u > 0) j[-1].blit = lits[0];
        else {
	  const int size = w.clause->size;
          const const_literal_iterator end = lits + size;
          const bool have_pos = w.clause->have.pos;
          literal_iterator k;
          int v = -1;
          literal_iterator start = lits;
          start += have_pos ? w.clause->pos () : 2;
          k = start;
          while (k != end && (v = val (*k)) < 0) k++;
          if (have_pos) {
            if (v < 0) {
              const const_literal_iterator middle = lits + w.clause->pos ();
              k = lits + 2;
              assert (w.clause->pos () <= size);
              while (k != middle && (v = val (*k)) < 0) k++;
            }
            w.clause->pos () = k - lits;
          }
          assert (lits + 2 <= k), assert (k <= w.clause->end ());
          if (v > 0) j[-1].blit = *k;
          else if (!v) {
            LOG (w.clause, "unwatch %d in", *k);
            swap (lits[1], *k);
            watch_literal (lits[1], lit, w.clause, size);
            j--;
          } else if (!u) probe_assign (lits[0], w.clause);
	  else { conflict = w.clause; break; }
        }
      }
      while (i != ws.end ()) *j++ = *i++;
      ws.resize (j - ws.begin ());
    } else break;
  }
  long delta = probagated2 - before;
  stats.probagations += delta;
  if (conflict) LOG (conflict, "conflict");
  STOP (probagate);
  return !conflict;
}

};
