// -*- coding: utf-8 -*-
// Copyright (C) by the Spot authors, see the AUTHORS file for details.
//
// This file is part of Spot, a model checking library.
//
// Spot is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// Spot is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
// License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "config.h"
#include <cmath>

#include <spot/gen/formulas.hh>
#include <spot/tl/exclusive.hh>
#include <spot/tl/relabel.hh>
#include <spot/tl/parse.hh>

#define G_(x) formula::G(x)
#define F_(x) formula::F(x)
#define X_(x) formula::X(x)
#define Xs_(x) formula::strong_X(x)
#define Not_(x) formula::Not(x)

#define Implies_(x, y) formula::Implies((x), (y))
#define Equiv_(x, y) formula::Equiv((x), (y))
#define And_(x, y) formula::And({(x), (y)})
#define Or_(x, y) formula::Or({(x), (y)})
#define Or3_(x, y, z) formula::Or({(x), (y), (z)})
#define U_(x, y) formula::U((x), (y))
#define Xor_(x, y) formula::Xor((x), (y))

namespace spot
{
  namespace gen
  {
    namespace
    {
      static formula
      ms_example(const char* a, const char* b, int n, int m)
      {
        formula ax = formula::tt();
        formula fb = formula::tt();
        for (int i = n; i > 0; --i)
          ax = And_(formula::ap(a + std::to_string(i)), X_(ax));
        for (int i = m; i > 0; --i)
          fb = F_(And_(formula::ap(b + std::to_string(i)), fb));
        return And_(G_(F_(ax)), fb);
      }

      static formula
      ms_phi_h(const char* a, const char* b, int n)
      {
        formula fa = formula::ap(a);
        formula fb = formula::ap(b);
        formula out = formula::ff();
        do
          {
            out = Or_(F_(G_(Or_(fa, fb))), out);
            fa = Not_(fa);
            fb = X_(fb);
          }
        while (n--);
        return out;
      }

      static formula
      ms_phi_rs(const char* a, const char* b, int n, bool r = true)
      {
        formula fgan = [=]() {
          std::ostringstream ans;
          ans << a << n;
          return F_(G_(formula::ap(ans.str())));
        } ();
        formula gfbn = [=]() {
          std::ostringstream ans;
          ans << b << n;
          return G_(F_(formula::ap(ans.str())));
        } ();
        formula top = r ? And_(fgan, gfbn) : Or_(fgan, gfbn);
        if (n == 0)
          return top;
        formula sub = ms_phi_rs(a, b, n - 1, !r);
        return r ? Or_(sub, top) : And_(sub, top);
      }

      // G(p_0 & XF(p_1 & XF(p_2 & ... XF(p_n))))
      // This is a generalization of eh-pattern=9
      static formula
      GXF_and_n(std::string name, int n)
      {
        formula result = formula::tt();

        for (; n >= 0; --n)
          {
            std::ostringstream p;
            p << name << n;
            formula f = formula::ap(p.str());
            result = And_(f, X_(F_(result)));
          }
        return G_(result);
      }

      // F(p_0 | XG(p_1 | XG(p_2 | ... XG(p_n))))
      // This the dual of the above
      static formula
      FXG_or_n(std::string name, int n)
      {
        formula result = formula::ff();

        for (; n >= 0; --n)
          {
            std::ostringstream p;
            p << name << n;
            formula f = formula::ap(p.str());
            result = Or_(f, X_(G_(result)));
          }
        return F_(result);
      }

      // F(p_1 & F(p_2 & F(p_3 & ... F(p_n))))
      static formula
      E_n(std::string name, int n)
      {
        if (n <= 0)
          return formula::tt();

        formula result = nullptr;

        for (; n > 0; --n)
          {
            std::ostringstream p;
            p << name << n;
            formula f = formula::ap(p.str());
            if (result)
              result = And_(f, result);
            else
              result = f;
            result = F_(result);
          }
        return result;
      }

      // p & X(p & X(p & ... X(p)))
      static formula
      phi_n(std::string name, int n,
            op oper = op::And)
      {
        if (n <= 0)
          return formula::tt();

        formula result = nullptr;
        formula p = formula::ap(name);
        for (; n > 0; --n)
          {
            if (result)
              result = formula::multop(oper, {p, X_(result)});
            else
              result = p;
          }
        return result;
      }

      static formula
      N_n(std::string name, int n)
      {
        return formula::F(phi_n(name, n));
      }

      // p & X(p) & XX(p) & XXX(p) & ... X^n(p)
      static formula
      phi_prime_n(std::string name, int n,
                  op oper = op::And)
      {
        if (n <= 0)
          return formula::tt();

        formula result = nullptr;
        formula p = formula::ap(name);
        for (; n > 0; --n)
          {
            if (result)
              {
                p = X_(p);
                result = formula::multop(oper, {result, p});
              }
            else
              {
                result = p;
              }
          }
        return result;
      }

      static formula
      N_prime_n(std::string name, int n)
      {
        return F_(phi_prime_n(name, n));
      }

      // GF(p_1) & GF(p_2) & ... & GF(p_n)   if conj == true
      // GF(p_1) | GF(p_2) | ... | GF(p_n)   if conj == false
      static formula
      GF_n(std::string name, int n, bool conj = true)
      {
        if (n <= 0)
          return conj ? formula::tt() : formula::ff();

        formula result = nullptr;

        op o = conj ? op::And : op::Or;

        for (int i = 1; i <= n; ++i)
          {
            std::ostringstream p;
            p << name << i;
            formula f = G_(F_(formula::ap(p.str())));

            if (result)
              result = formula::multop(o, {f, result});
            else
              result = f;
          }
        return result;
      }

      // FG(p_1) | FG(p_2) | ... | FG(p_n)   if conj == false
      // FG(p_1) & FG(p_2) & ... & FG(p_n)   if conj == true
      static formula
      FG_n(std::string name, int n, bool conj = false)
      {
        if (n <= 0)
          return conj ? formula::tt() : formula::ff();

        formula result = nullptr;

        op o = conj ? op::And : op::Or;

        for (int i = 1; i <= n; ++i)
          {
            std::ostringstream p;
            p << name << i;
            formula f = F_(G_(formula::ap(p.str())));

            if (result)
              result = formula::multop(o, {f, result});
            else
              result = f;
          }
        return result;
      }

      // Builds X(X(...X(p))) with n occurrences of X.
      static formula
      X_n(formula p, int n)
      {
        assert(n >= 0);
        return formula::X(n, p);
      }

      static formula
      GF_equiv_implies(int n, const std::string& a, const std::string& z,
                       bool equiv)
      {
        formula left = GF_n(a, n);
        formula right = formula::G(formula::F(formula::ap(z)));
        if (equiv)
          return formula::Equiv(left, right);
        else
          return formula::Implies(left, right);
      }

      static formula
      GF_equiv_implies_xn(int n, const std::string& a, bool equiv)
      {
        formula ap = formula::ap(a);
        formula xn = X_n(ap, n);
        formula in;
        if (equiv)
          in = formula::Equiv(ap, xn);
        else
          in = formula::Implies(ap, xn);
        return G_(F_(in));
      }

      //  (((p1 OP p2) OP p3)...OP pn)   if right_assoc == false
      //  (p1 OP (p2 OP (p3 OP (... pn)  if right_assoc == true
      static formula
      bin_n(std::string name, int n, op o, bool right_assoc = false)
      {
        if (n <= 0)
          n = 1;

        formula result = nullptr;

        for (int i = 1; i <= n; ++i)
          {
            std::ostringstream p;
            p << name << (right_assoc ? (n + 1 - i) : i);
            formula f = formula::ap(p.str());
            if (!result)
              result = f;
            else if (right_assoc)
              result = formula::binop(o, f, result);
            else
              result = formula::binop(o, result, f);
          }
        return result;
      }

      // (GF(p1)|FG(p2))&(GF(p2)|FG(p3))&...&(GF(pn)|FG(p{n+1}))"
      static formula
      R_n(std::string name, int n)
      {
        if (n <= 0)
          return formula::tt();

        formula pi;

        {
          std::ostringstream p;
          p << name << 1;
          pi = formula::ap(p.str());
        }

        formula result = nullptr;

        for (int i = 1; i <= n; ++i)
          {
            formula gf = G_(F_(pi));
            std::ostringstream p;
            p << name << i + 1;
            pi = formula::ap(p.str());

            formula fg = F_(G_(pi));

            formula f = Or_(gf, fg);

            if (result)
              result = And_(f, result);
            else
              result = f;
          }
        return result;
      }

      // (F(p1)|G(p2))&(F(p2)|G(p3))&...&(F(pn)|G(p{n+1}))"
      static formula
      Q_n(std::string name, int n)
      {
        if (n <= 0)
          return formula::tt();

        formula pi;

        {
          std::ostringstream p;
          p << name << 1;
          pi = formula::ap(p.str());
        }

        formula result = nullptr;

        for (int i = 1; i <= n; ++i)
          {
            formula f = F_(pi);

            std::ostringstream p;
            p << name << i + 1;
            pi = formula::ap(p.str());

            formula g = G_(pi);

            f = Or_(f, g);

            if (result)
              result = And_(f, result);
            else
              result = f;
          }
        return result;
      }

      //  OP(p1) | OP(p2) | ... | OP(Pn) if conj == false
      //  OP(p1) & OP(p2) & ... & OP(Pn) if conj == true
      static formula
      combunop_n(std::string name, int n, op o, bool conj = false)
      {
        if (n <= 0)
          return conj ? formula::tt() : formula::ff();

        formula result = nullptr;

        op cop = conj ? op::And : op::Or;

        for (int i = 1; i <= n; ++i)
          {
            std::ostringstream p;
            p << name << i;
            formula f = formula::unop(o, formula::ap(p.str()));

            if (result)
              result = formula::multop(cop, {f, result});
            else
              result = f;
          }
        return result;
      }

      // !((GF(p1)&GF(p2)&...&GF(pn))->G(q -> F(r)))
      // From "Fast LTL to Büchi Automata Translation" [gastin.01.cav]
      static formula
      fair_response(std::string p, std::string q, std::string r, int n)
      {
        formula fair = GF_n(p, n);
        formula resp = G_(Implies_(formula::ap(q), F_(formula::ap(r))));
        return Not_(Implies_(fair, resp));
      }

      // Based on LTLcounter.pl from Kristin Rozier.
      // http://shemesh.larc.nasa.gov/people/kyr/benchmarking_scripts/
      static formula
      ltl_counter(std::string bit, std::string marker, int n, bool linear)
      {
        formula b = formula::ap(bit);
        formula neg_b = Not_(b);
        formula m = formula::ap(marker);
        formula neg_m = Not_(m);

        std::vector<formula> res(4);

        // The marker starts with "1", followed by n-1 "0", then "1" again,
        // n-1 "0", etc.
        if (!linear)
          {
            // G(m -> X(!m)&XX(!m)&XXX(m))          [if n = 3]
            std::vector<formula> v(n);
            for (int i = 0; i + 1 < n; ++i)
              v[i] = X_n(neg_m, i + 1);
            v[n - 1] = X_n(m, n);
            res[0] = And_(m, G_(Implies_(m, formula::And(std::move(v)))));
          }
        else
          {
            // G(m -> X(!m & X(!m X(m))))          [if n = 3]
            formula p = m;
            for (int i = n - 1; i > 0; --i)
              p = And_(neg_m, X_(p));
            res[0] = And_(m, G_(Implies_(m, X_(p))));
          }

        // All bits are initially zero.
        if (!linear)
          {
            // !b & X(!b) & XX(!b)    [if n = 3]
            std::vector<formula> v2(n);
            for (int i = 0; i < n; ++i)
              v2[i] = X_n(neg_b, i);
            res[1] = formula::And(std::move(v2));
          }
        else
          {
            // !b & X(!b & X(!b))     [if n = 3]
            formula p = neg_b;
            for (int i = n - 1; i > 0; --i)
              p = And_(neg_b, X_(p));
            res[1] = p;
          }

#define AndX_(x, y) (linear ? X_(And_((x), (y))) : And_(X_(x), X_(y)))

        // If the least significant bit is 0, it will be 1 at the next time,
        // and other bits stay the same.
        formula Xnm1_b = X_n(b, n - 1);
        formula Xn_b = X_(Xnm1_b);
        res[2] = G_(Implies_(And_(m, neg_b),
                             AndX_(Xnm1_b,
                                   U_(And_(Not_(m), Equiv_(b, Xn_b)), m))));

        // From the least significant bit to the first 0, all the bits
        // are flipped on the next value.  Remaining bits are identical.
        formula Xnm1_negb = X_n(neg_b, n - 1);
        formula Xn_negb = X_(Xnm1_negb);
        res[3] =
          G_(Implies_(And_(m, b),
                      AndX_(Xnm1_negb,
                            U_(And_(And_(b, neg_m), Xn_negb),
                               Or_(m, And_(And_(neg_m, neg_b),
                                           AndX_(Xnm1_b,
                                                 U_(And_(neg_m,
                                                         Equiv_(b, Xn_b)),
                                                    m))))))));
        return formula::And(std::move(res));
      }

      static formula
      ltl_counter_carry(std::string bit, std::string marker,
                        std::string carry, int n, bool linear)
      {
        formula b = formula::ap(bit);
        formula neg_b = Not_(b);
        formula m = formula::ap(marker);
        formula neg_m = Not_(m);
        formula c = formula::ap(carry);
        formula neg_c = Not_(c);

        std::vector<formula> res(6);

        // The marker starts with "1", followed by n-1 "0", then "1" again,
        // n-1 "0", etc.
        if (!linear)
          {
            // G(m -> X(!m)&XX(!m)&XXX(m))          [if n = 3]
            std::vector<formula> v(n);
            for (int i = 0; i + 1 < n; ++i)
              v[i] = X_n(neg_m, i + 1);
            v[n - 1] = X_n(m, n);
            res[0] = And_(m, G_(Implies_(m, formula::And(std::move(v)))));
          }
        else
          {
            // G(m -> X(!m & X(!m X(m))))          [if n = 3]
            formula p = m;
            for (int i = n - 1; i > 0; --i)
              p = And_(neg_m, X_(p));
            res[0] = And_(m, G_(Implies_(m, X_(p))));
          }

        // All bits are initially zero.
        if (!linear)
          {
            // !b & X(!b) & XX(!b)    [if n = 3]
            std::vector<formula> v2(n);
            for (int i = 0; i < n; ++i)
              v2[i] = X_n(neg_b, i);
            res[1] = formula::And(std::move(v2));
          }
        else
          {
            // !b & X(!b & X(!b))     [if n = 3]
            formula p = neg_b;
            for (int i = n - 1; i > 0; --i)
              p = And_(neg_b, X_(p));
            res[1] = p;
          }

        formula Xn_b = X_n(b, n);
        formula Xn_negb = X_n(neg_b, n);

        // If m is 1 and b is 0 then c is 0 and n steps later b is 1.
        res[2] = G_(Implies_(And_(m, neg_b), And_(neg_c, Xn_b)));

        // If m is 1 and b is 1 then c is 1 and n steps later b is 0.
        res[3] = G_(Implies_(And_(m, b), And_(c, Xn_negb)));

        if (!linear)
          {
            // If there's no carry, then all of the bits stay the same
            // n steps later.
            res[4] = G_(Implies_(And_(neg_c, X_(neg_m)),
                                 And_(X_(Not_(c)), Equiv_(X_(b), X_(Xn_b)))));

            // If there's a carry, then add one: flip the bits of b and
            // adjust the carry.
            res[5] = G_(Implies_(c, And_(Implies_(X_(neg_b),
                                                  And_(X_(neg_c), X_(Xn_b))),
                                         Implies_(X_(b),
                                                  And_(X_(c), X_(Xn_negb))))));
          }
        else
          {
            // If there's no carry, then all of the bits stay the same
            // n steps later.
            res[4] = G_(Implies_(And_(neg_c, X_(neg_m)),
                                 X_(And_(Not_(c), Equiv_(b, Xn_b)))));
            // If there's a carry, then add one: flip the bits of b and
            // adjust the carry.
            res[5] = G_(Implies_(c, X_(And_(Implies_(neg_b, And_(neg_c, Xn_b)),
                                            Implies_(b, And_(c, Xn_negb))))));
          }
        return formula::And(std::move(res));
      }

      static formula
      tv_f1(std::string p, std::string q, int n)
      {
        return G_(Implies_(formula::ap(p), phi_prime_n(q, n, op::Or)));
      }

      static formula
      tv_f2(std::string p, std::string q, int n)
      {
        return G_(Implies_(formula::ap(p), phi_n(q, n, op::Or)));
      }

      static formula
      tv_g1(std::string p, std::string q, int n)
      {
        return G_(Implies_(formula::ap(p), phi_prime_n(q, n)));
      }

      static formula
      tv_g2(std::string p, std::string q, int n)
      {
        return G_(Implies_(formula::ap(p), phi_n(q, n)));
      }

      static formula
      tv_uu(std::string name, int n)
      {
        std::ostringstream p;
        p << name << n + 1;
        formula q = formula::ap(p.str());
        formula f = q;

        for (int i = n; i > 0; --i)
          {
            p.str("");
            p << name << i;
            q = formula::ap(p.str());
            f = U_(q, f);
            if (i > 1)
              f = And_(q, f);
          }
        return G_(Implies_(q,  f));
      }

      static void
      bad_number(const char* pattern, int n, int max = 0)
      {
        std::ostringstream err;
        err << "no pattern " << pattern << '=' << n
            << ", supported range is 1..";
        if (max)
          err << max;
        throw std::runtime_error(err.str());
      }

      static formula
      dac_pattern(int n)
      {
        static const char* formulas[] = {
          "[](!p0)",
          "<>p2 -> (!p0 U p2)",
          "[](p1 -> [](!p0))",
          "[]((p1 & !p2 & <>p2) -> (!p0 U p2))",
          "[](p1 & !p2 -> (!p0 W p2))",

          "<>(p0)",
          "!p2 W (p0 & !p2)",
          "[](!p1) | <>(p1 & <>p0)",
          "[](p1 & !p2 -> (!p2 W (p0 & !p2)))",
          "[](p1 & !p2 -> (!p2 U (p0 & !p2)))",

          "(!p0 W (p0 W (!p0 W (p0 W []!p0))))",
          "<>p2 -> ((!p0 & !p2) U (p2 | ((p0 & !p2) U (p2 |"
          " ((!p0 & !p2) U (p2 | ((p0 & !p2) U (p2 | (!p0 U p2)))))))))",
          "<>p1 -> (!p1 U (p1 & (!p0 W (p0 W (!p0 W (p0 W []!p0))))))",
          "[]((p1 & <>p2) -> ((!p0 & !p2) U (p2 | ((p0 & !p2) U (p2 |"
          "((!p0 & !p2) U (p2 | ((p0 & !p2) U (p2 | (!p0 U p2))))))))))",
          "[](p1 -> ((!p0 & !p2) U (p2 | ((p0 & !p2) U (p2 | ((!p0 & !p2) U"
          "(p2 | ((p0 & !p2) U (p2 | (!p0 W p2) | []p0)))))))))",

          "[](p0)",
          "<>p2 -> (p0 U p2)",
          "[](p1 -> [](p0))",
          "[]((p1 & !p2 & <>p2) -> (p0 U p2))",
          "[](p1 & !p2 -> (p0 W p2))",

          "!p0 W p3",
          "<>p2 -> (!p0 U (p3 | p2))",
          "[]!p1 | <>(p1 & (!p0 W p3))",
          "[]((p1 & !p2 & <>p2) -> (!p0 U (p3 | p2)))",
          "[](p1 & !p2 -> (!p0 W (p3 | p2)))",

          "[](p0 -> <>p3)",
          "<>p2 -> (p0 -> (!p2 U (p3 & !p2))) U p2",
          "[](p1 -> [](p0 -> <>p3))",
          "[]((p1 & !p2 & <>p2) -> ((p0 -> (!p2 U (p3 & !p2))) U p2))",
          "[](p1 & !p2 -> ((p0 -> (!p2 U (p3 & !p2))) W p2))",

          "<>p0 -> (!p0 U (p3 & !p0 & X(!p0 U p4)))",
          "<>p2 -> (!p0 U (p2 | (p3 & !p0 & X(!p0 U p4))))",
          "([]!p1) | (!p1 U (p1 & <>p0 -> (!p0 U (p3 & !p0 & X(!p0 U p4)))))",
          "[]((p1 & <>p2) -> (!p0 U (p2 | (p3 & !p0 & X(!p0 U p4)))))",
          "[](p1 -> (<>p0 -> (!p0 U (p2 | (p3 & !p0 & X(!p0 U p4))))))",

          "(<>(p3 & X<>p4)) -> ((!p3) U p0)",
          "<>p2 -> ((!(p3 & (!p2) & X(!p2 U (p4 & !p2)))) U (p2 | p0))",
          "([]!p1) | ((!p1) U (p1 & ((<>(p3 & X<>p4)) -> ((!p3) U p0))))",
          "[]((p1 & <>p2)->((!(p3 & (!p2) & X(!p2 U (p4 & !p2)))) U (p2|p0)))",
          "[](p1 -> (!(p3 & (!p2) & X(!p2 U (p4 & !p2))) U (p2 | p0) |"
          " [](!(p3 & X<>p4))))",

          "[] (p3 & X<> p4 -> X(<>(p4 & <> p0)))",
          "<>p2 -> (p3 & X(!p2 U p4) -> X(!p2 U (p4 & <> p0))) U p2",
          "[] (p1 -> [] (p3 & X<> p4 -> X(!p4 U (p4 & <> p0))))",
          "[] ((p1 & <>p2)->(p3 & X(!p2 U p4) -> X(!p2 U (p4 & <> p0))) U p2)",
          "[] (p1 -> (p3 & X(!p2 U p4) -> X(!p2 U (p4 & <> p0))) U (p2 |"
          "[] (p3 & X(!p2 U p4) -> X(!p2 U (p4 & <> p0)))))",

          "[] (p0 -> <>(p3 & X<>p4))",
          "<>p2 -> (p0 -> (!p2 U (p3 & !p2 & X(!p2 U p4)))) U p2",
          "[] (p1 -> [] (p0 -> (p3 & X<> p4)))",
          "[] ((p1 & <>p2) -> (p0 -> (!p2 U (p3 & !p2 & X(!p2 U p4)))) U p2)",
          "[] (p1 -> (p0 -> (!p2 U (p3 & !p2 & X(!p2 U p4)))) U (p2 | []"
          "(p0 -> (p3 & X<> p4))))",

          "[] (p0 -> <>(p3 & !p5 & X(!p5 U p4)))",
          "<>p2 -> (p0 -> (!p2 U (p3 & !p2 & !p5 & X((!p2 & !p5) U p4)))) U p2",
          "[] (p1 -> [] (p0 -> (p3 & !p5 & X(!p5 U p4))))",
          "[] ((p1 & <>p2) -> (p0 -> (!p2 U (p3 & !p2 & !p5 & X((!p2 & !p5) U"
          " p4)))) U p2)",
          "[] (p1 -> (p0 -> (!p2 U (p3 & !p2 & !p5 & X((!p2 & !p5) U p4)))) U "
          "(p2 | [] (p0 -> (p3 & !p5 & X(!p5 U p4)))))",
        };

        constexpr unsigned max = (sizeof formulas)/(sizeof *formulas);
        if (n < 1 || (unsigned) n > max)
          bad_number("dac-patterns", n, max);
        return spot::relabel(parse_formula(formulas[n - 1]), Pnn);
      }

      static formula
      hkrss_pattern(int n)
      {
        static const char* formulas[] = {
          "G(Fp0 & F!p0)",
          "GFp0 & GF!p0",
          "GF(!(p1 <-> Xp1) | !(p0 <-> Xp0))",
          "GF(!(p1 <-> Xp1) | !(p0 <-> Xp0) | !(p2 <-> Xp2) | !(p3 <-> Xp3))",
          "G!p0",
          "G((p0 -> F!p0) & (!p0 -> Fp0))",
          "G(p0 -> F(p0 & p1))",
          "G(p0 -> F((!p0 & p1 & p2 & p3) -> Fp4))",
          "G(p0 -> F!p1)",
          "G(p0 -> Fp1)",
          "G(p0 -> F(p1 -> Fp2))",
          "G(p0 -> F((p1 & p2) -> Fp3))",
          "G((p0 -> Fp1) & (p2 -> Fp3) & (p4 -> Fp5) & (p6 -> Fp7))",
          "G(!p0 & !p1)",
          "G!(p0 & p1)",
          "G(p0 -> p1)",
          "G((p0 -> !p1) & (p1 -> !p0))",
          "G(!p0 -> (p1 <-> !p2))",
          "G((!p0 & (p1 | p2 | p3)) -> p4)",
          "G((p0 & p1) -> (p2 | !(p3 & p4)))",
          "G((!p0 & p1 & !p2 & !p3 & !p4) -> F(!p5 & !p6 & !p7 & !p8))",
          "G((p0 & p1 & !p2 & !p3 & !p4) -> F(p5 & !p6 & !p7 & !p8))",
          "G(!p0 -> !(p1 & p2 & p3 & p4 & p5))",
          "G(!p0 -> ((p1 & p2 & p3 & p4) -> !p5))",
          "G((p0 & p1) -> (p2 | p3 | !(p4 & p5)))",
          "G((!p0 & (p1 | p2 | p3 | p4)) -> (!p5 <-> p6))",
          "G((p0 & p1) -> (p2 | p3 | p4 | !(p5 & p6)))",
          "G((p0 & p1) -> (p2 | p3 | p4 | p5 | !(p6 & p7)))",
          "G((p0 & p1 & !p2 & Xp2) -> X(p3 | X(!p1 | p3)))",
          "G((p0 & p1 & !p2 & Xp2)->X(X!p1 | (p2 U (!p2 U (p2 U (!p1|p3))))))",
          "G(p0 & p1 & !p2 & Xp2)->X(X!p1 | (p2 U (!p2 U (p2 U (!p1 | p3)))))",
          "G(p0 -> (p1 U (!p1 U (!p2 | p3))))",
          "G(p0 -> (p1 U (!p1 U (p2 | p3))))",
          "G((!p0 & p1) -> Xp2)",
          "G(p0 -> X(p0 | p1))",
          ("G((!(p1 <-> Xp1) | !(p0 <-> Xp0) | !(p2 <-> Xp2) | !(p3 <-> Xp3)) "
           "-> (X!p4 & X(!(!(p1 <-> Xp1) | !(p0 <-> Xp0) | !(p2 <-> Xp2) | "
           "!(p3 <-> Xp3)) U p4)))"),
          "G((p0 & !p1 & Xp1 & Xp0) -> (p2 -> Xp3))",
          "G(p0 -> X(!p0 U p1))",
          "G((!p0 & Xp0) -> X((p0 U p1) | Gp0))",
          "G((!p0 & Xp0) -> X(p0 U (p0 & !p1 & X(p0 & p1))))",
          ("G((!p0 & Xp0) -> X(p0 U (p0 & !p1 & X(p0 & p1 & (p0 U (p0 & !p1 & "
           "X(p0 & p1)))))))"),
          ("G((p0 & X!p0) -> X(!p0 U (!p0 & !p1 & X(!p0 & p1 & (!p0 U (!p0 & "
           "!p1 & X(!p0 & p1)))))))"),
          ("G((p0 & X!p0) -> X(!p0 U (!p0 & !p1 & X(!p0 & p1 & (!p0 U (!p0 & "
           "!p1 & X(!p0 & p1 & (!p0 U (!p0 & !p1 & X(!p0 & p1))))))))))"),
          "G((!p0 & Xp0) -> X(!(!p0 & Xp0) U (!p1 & Xp1)))",
          ("G(!p0 | X(!p0 | X(!p0 | X(!p0 | X(!p0 | X(!p0 | X(!p0 | X(!p0 | "
           "X(!p0 | X(!p0 | X(!p0 | X!p0)))))))))))"),
          "G((Xp0 -> p0) -> (p1 <-> Xp1))",
          "G((Xp0 -> p0) -> ((p1 -> Xp1) & (!p1 -> X!p1)))",
          ("!p0 U G!((p1 & p2) | (p3 & p4) | (p2 & p3) | (p2 & p4) | "
           "(p1 & p4) | (p1 & p3))"),
          "!p0 U p1",
          "(p0 U p1) | Gp0",
          "p0 & XG!p0",
          "XG(p0 -> (G!p1 | (!Xp1 U p2)))",
          "XG((p0 & !p1) -> (G!p1 | (!p1 U p2)))",
          "XG((p0 & p1) -> ((p1 U p2) | Gp1))",
          "Xp0 & G((!p0 & Xp0) -> XXp0)",
        };

        constexpr unsigned max = (sizeof formulas)/(sizeof *formulas);
        if (n < 1 || (unsigned) n > max)
          bad_number("hkrss-patterns", n, max);
        return spot::relabel(parse_formula(formulas[n - 1]), Pnn);
      }

      static formula
      eh_pattern(int n)
      {
        static const char* formulas[] = {
          "p0 U (p1 & G(p2))",
          "p0 U (p1 & X(p2 U p3))",
          "p0 U (p1 & X(p2 & (F(p3 & X(F(p4 & X(F(p5 & X(F(p6))))))))))",
          "F(p0 & X(G(p1)))",
          "F(p0 & X(p1 & X(F(p2))))",
          "F(p0 & X(p1 U p2))",
          "(F(G(p0))) | (G(F(p1)))",
          "G(p0 -> (p1 U p2))",
          "G(p0 & X(F(p1 & X(F(p2 & X(F(p3)))))))",
          "(G(F(p0))) & (G(F(p1))) & (G(F(p2))) & (G(F(p3))) & (G(F(p4)))",
          "(p0 U (p1 U p2)) | (p1 U (p2 U p0)) | (p2 U (p0 U p1))",
          "G(p0 -> (p1 U ((G(p2)) | (G(p3)))))",
        };

        constexpr unsigned max = (sizeof formulas)/(sizeof *formulas);
        if (n < 1 || (unsigned) n > max)
          bad_number("eh-patterns", n, max);
        return spot::relabel(parse_formula(formulas[n - 1]), Pnn);
      }

      static formula
      lily_pattern(int n)
      {
        static const char* formulas[] = {
          "G(i2->(X(o0&X(o0&Xo0))&(o0->X!o0)&(i0->X(!o0 U i1))))",
          "G(i2->(X(o0|X(o0|Xo0))&(o0->X!o0)&(i0->X(!o0 U i1))))",
          "G(i0->Xi1)->G(i2->(X(o0|X(o0|Xo0))&(o0->X!o0)&(i0->X(!o0 U i1))))",
          "G(i0->X(i1|Xi1))->"
          "G(i2->(X(o0|X(o0|Xo0))&(o0->X!o0)&(i0->X(!o0 U i1))))",
          "G(i0->X(i1|Xi1))->"
          "G(i2->(X(i0|o0|X(i0|o0|X(i0|o0)))&(o0->X!o0)&(i0->X(!o0 U i1))))",
          "G(i0->X(i1|X(i1|Xi1)))->"
          "G(i2->(X(i0|o0|X(i0|o0|X(i0|o0)))&(o0->X!o0)&(i0->X(!o0 U i1))))",
          "G(i0->X(i1|Xi1))->G(i0->(X(!o0 U i1)"
          "&(o0->X!o0)&(i2->(i0|o0|X(i0|o0|X(i0|o0|X(i0|o0)))))))",
          "GFi0->GFo0",
          "GFi0->(!o0&G(!o0->((!o0 U i0)&(i0->Fo0)))&GFo0)",
          "(GFi1|Fi0)->(GFo1|Go0)",
          "!(G(i1->Fo0)&G(i0->Fo1))",
          "G!o1|G(i1->Fo0)|G(i0->Fo1)",
          "Gi0->(Fo0&(G!i0->F!o0))",
          "G!(o0&o1)&(GFi0->GFo0)&(GFi1->GFo1)",
          // lily=15 This matches the original formula from Lily, not
          // the unrealizable version from Syfco.  See
          // https://github.com/reactive-systems/syfco/issues/55
          "G(i0->(!(o0&o1)&Fo0&(i1->Fo1)))&((!o0 U i0)|G!o0)&((!o1 U i1)|G!o1)",
          // lily=16   Same comment as above.
          "G(i0->(!(o0&o1)&!(o0&o2)&!(o1&o2)&Fo0&(i1->Fo1)&(i2->Fo2)))&"
          "((!o0 U i0)|G!o0)&((!o1 U i1)|G!o1)&((!o2 U i2)|G!o2)",
          "G(!(o0&o1)&!(o1&o2)&!(o0&o2))&(GFi0->GFo0)&(GFi1->GFo1)&GFo2",
          "G(!(o0&o1)&!(o0&o2)&!(o0&o3)&!(o1&o2)&!(o1&o3)&!(o2&o3))&"
          "(GFi0->GFo0)&(GFi1->GFo1)&(GFi2->GFo2)&GFo3",
          "GFi1->G(o1->(!(o0&o1)&(o1 U i1)&(o0->(o0 U i1))&(i0->Fo0)&Fo1))",
          "(!i1&G((!i1&!o2)->X!i1)&G(i1->F!i1)&G(o2->Xi1))->"
          "G((o1&X!o1)->(o2&(o0|o1)&((o0&X!o0)->o2)&((!o0&(!i0|!i1))->"
          "Xo0)&((!o1&(i0|!i1))->Xo1)&(i0->F!o0)&F!o1))",
          "(G(!i0|!i1)&G(!i0|!i2)&G(!i0|!i3)&G(!i1|!i2)&G(!i1|!i3)&"
          "G(!i2|!i3))->G((!o0|!o1)&(!o0|!o2)&(!o0|!o3)&(!o1|!o2)&(!o1|!o3)&"
          "(!o2|!o3)&G(i0->(Xo0|XXo0|XXXo0))&G(i1->(Xo1|XXo1|XXXo1))&"
          "G(i2->(Xo2|XXo2|XXXo2))&G(i3->(Xo3|XXo3|XXXo3)))",
          "(!i0&!i1&!i2&G!(i0&i1)&GF!i2&G((!i2&o0)->X!i2)&G(i2->X(!i2|"
          "X(!i2|X(!i2 | X!i2)))))->G(!(i2&Xo0)&(i1->F!o0)&(i0->Fo0))",
          "(G((i0&Xo0)->Xi0)&GF!i0)->GX(!i0&X!o0)",
        };
        constexpr unsigned max = (sizeof formulas)/(sizeof *formulas);
        if (n < 1 || (unsigned) n > max)
          bad_number("lily-patterns", n, max);
        return parse_formula(formulas[n - 1]);
      }

      static formula
      p_pattern(int n)
      {
        static const char* formulas[] = {
          "G(p0 -> Fp1)",
          "(GFp1 & GFp0) -> GFp2",
          "G(p0 -> (p1 & (p2 U p3)))",
          "F(p0 | p1)",
          "GF(p0 | p1)",
          "(p0 U p1) -> ((p2 U p3) | Gp2)",
          "G(p0 -> (!p1 U (p1 U (!p1 & (p2 R !p1)))))",
          "G(p0 -> (p1 R !p2))",
          "G(!p0 -> Fp0)",
          "G(p0 -> F(p1 | p2))",
          "!(!(p0 | p1) U p2) & G(p3 -> !(!(p0 | p1) U p2))",
          "G!p0 -> G!p1",
          "G(p0 -> (G!p1 | (!p2 U p1)))",
          "G(p0 -> (p1 R (p1 | !p2)))",
          "G((p0 & p1) -> (!p1 R (p0 | !p1)))",
          "G(p0 -> F(p1 & p2))",
          "G(p0 -> (!p1 U (p1 U (p1 & p2))))",
          "G(p0 -> (!p1 U (p1 U (!p1 U (p1 U (p1 & p2))))))",
          "GFp0 -> GFp1",
          "GF(p0 | p1) & GF(p1 | p2)",
        };

        constexpr unsigned max = (sizeof formulas)/(sizeof *formulas);
        if (n < 1 || (unsigned) n > max)
          bad_number("p-patterns", n, max);
        return spot::relabel(parse_formula(formulas[n - 1]), Pnn);
      }

      static formula
      sb_pattern(int n)
      {
        static const char* formulas[] = {
          "p0 U p1",
          "p0 U (p1 U p2)",
          "!(p0 U (p1 U p2))",
          "G(F(p0)) -> G(F(p1))",
          "(F(p0)) U (G(p1))",
          "(G(p0)) U p1",
          "!((F(F(p0))) <-> (F(p)))",
          "!((G(F(p0))) -> (G(F(p))))",
          "!((G(F(p0))) <-> (G(F(p))))",
          "p0 R (p0 | p1)",
          "(Xp0 U Xp1) | !X(p0 U p1)",
          "(Xp0 U p1) | !X(p0 U (p0 & p1))",
          "G(p0 -> F(p1)) & (((X(p0)) U p1) | !X(p0 U (p0 & p1)))",
          "G(p0 -> F(p1)) & (((X(p0)) U X(p1)) | !X(p0 U p1))",
          "G(p0 -> F(p1))",
          "!G(p0 -> X(p1 R p2))",
          "!(F(G(p0)) | F(G(p1)))",
          "G(F(p0) & F(p1))",
          "F(p0) & F(!p0)",
          "(X(p1) & p2) R X(((p3 U p0) R p2) U (p3 R p2))",
          "(G(p1 | G(F(p0))) & G(p2 | G(F(!p0)))) | G(p1) | G(p2)",
          "(G(p1 | F(G(p0))) & G(p2 | F(G(!p0)))) | G(p1) | G(p2)",
          "!((G(p1 | G(F(p0))) & G(p2 | G(F(!p0)))) | G(p1) | G(p2))",
          "!((G(p1 | F(G(p0))) & G(p2 | F(G(!p0)))) | G(p1) | G(p2))",
          "(G(p1 | X(G p0))) & (G (p2 | X(G !p0)))",
          "G(p1 | (Xp0 & X!p0))",
          // p0 U p0 can't be represented other than as p0 in Spot
          "(p0 U p0) | (p1 U p0)",
        };

        constexpr unsigned max = (sizeof formulas)/(sizeof *formulas);
        if (n < 1 || (unsigned) n > max)
          bad_number("sb-patterns", n, max);
        return spot::relabel(parse_formula(formulas[n - 1]), Pnn);
      }

      static formula
      X_n_kv_exp(formula a, int n, formula b)
      {
        formula f = X_n(a, n);
        return And_(f, G_(Implies_(b, f)));
      }

      static formula
      kv_exp(int n, std::string a, std::string b, std::string c, std::string d)
      {
        formula fa = formula::ap(a);
        formula fb = formula::ap(b);
        formula fc = formula::ap(c);
        formula fd = formula::ap(d);

        exclusive_ap m;
        m.add_group({ fa, fb, fc, fd });

        formula xn = X_(G_(fc));
        for (int i = 0; i < n; i++)
          xn = X_(And_(Or_(fa, fb), xn));
        formula f1 = U_(Not_(fd), And_(fd, xn));

        formula f_and = nullptr;
        for (int i = 1; i <= n; i++)
          f_and = And_(f_and, Or_(X_n_kv_exp(fa, i, fd),
                                  X_n_kv_exp(fb, i, fd)));

        formula f2 = F_(And_(fc, And_(f_and, X_n(fc, n + 1))));

        return m.constrain(And_(f1, f2));
      }

      static formula
      bit_ni(unsigned i, unsigned j, formula fbin[2])
      {
        return fbin[((1u << j) & (i - 1)) != 0];
      }

      static formula
      binary_ki(int k, unsigned i, formula fbin[2])
      {
        formula res = bit_ni(i, k - 1, fbin);
        for (int j = k - 1; j >= 1; j--)
          res = And_(bit_ni(i, j - 1, fbin), X_(res));
        return res;
      }

      static formula
      kr1_exp_1(int k, formula fc, formula fd, formula fbin[2])
      {
        return And_(fc, X_(Or_(binary_ki(k, 1, fbin), fd)));
      }

      static formula
      kr1_exp_2(int n, int k, formula fa, formula fb, formula fbin[2])
      {
        formula res = formula::tt();
        for (int i = 1; i <= n - 1; i++)
          res = And_(res,
                     Implies_(binary_ki(k, i, fbin),
                              X_n(And_(Or_(fa, fb),
                                       X_(binary_ki(k, i + 1, fbin))), k)));

        return G_(res);
      }

      static formula
      kr1_exp_3(int n, int k, formula fa, formula fb, formula fc, formula fd,
                formula fbin[2])
      {
        return G_(Implies_(binary_ki(k, n, fbin),
                           X_n(And_(Or_(fa, fb),
                                    X_(And_(fc,
                                            X_(Or_(binary_ki(k, 1, fbin),
                                                   Or_(fd,
                                                       G_(fc))))))), k)));
      }

      static formula
      kr1_exp_4(int n, int k, formula fc, formula fd, formula fbin[2])
      {
        return U_(Not_(fd),
                  And_(fd, X_(And_(binary_ki(k, 1, fbin),
                                   X_n(G_(fc), n * (k + 1))))));
      }

      static formula
      kr1_exp_5_r(int k, int i, formula fr, formula fd, formula fbin[2])
      {
        return And_(fr, F_(And_(fd, F_(And_(binary_ki(k, i, fbin),
                                            X_n(fr, k))))));
      }

      static formula
      kr1_exp_5(int n, int k, formula fa, formula fb, formula fc, formula fd,
                formula fbin[2])
      {
        formula fand = formula::tt();
        for (int i = 1; i <= n; i++)
          {
            formula for1 = kr1_exp_5_r(k, i, fa, fd, fbin);
            formula for2 = kr1_exp_5_r(k, i, fb, fd, fbin);
            fand = And_(fand, Implies_(binary_ki(k, i, fbin),
                                       X_n(Or_(for1, for2), k)));
          }

        return F_(And_(fc,
                       X_(And_(Not_(fc),
                               U_(fand, fc)))));
      }

      static formula
      kr1_exp(int n, std::string a, std::string b, std::string c, std::string d,
              std::string bin0, std::string bin1)
      {
        int k = ceil(log2(n)) + (n == 1);

        if (n <= 0)
          bad_number("kr-nlogn", n);

        formula fa = formula::ap(a);
        formula fb = formula::ap(b);
        formula fc = formula::ap(c);
        formula fd = formula::ap(d);

        formula fbin0 = formula::ap(bin0);
        formula fbin1 = formula::ap(bin1);

        exclusive_ap m;
        m.add_group({ fa, fb, fc, fd, fbin0, fbin1 });

        formula fbin[2] = { fbin0, fbin1 };

        formula res = formula::And({ kr1_exp_1(k, fc, fd, fbin),
              kr1_exp_2(n, k, fa, fb, fbin),
              kr1_exp_3(n, k, fa, fb, fc, fd, fbin),
              kr1_exp_4(n, k, fc, fd, fbin),
              kr1_exp_5(n, k, fa, fb, fc, fd, fbin) });

        return m.constrain(res);
      }

      static formula
      kr2_exp_1(formula* fa, formula* fb, formula fc, formula fd)
      {
        (void) fd;
        return And_(fc,
                    X_(Or_(fa[0],
                           Or_(fb[0], fd))));
      }

      static formula
      kr2_exp_2(int n, formula* fa, formula* fb)
      {
        formula res = formula::tt();
        for (int i = 1; i <= n - 1; i++)
          res = And_(res, Implies_(Or_(fa[i - 1], fb[i - 1]),
                                   X_(Or_(fa[i], fb[i]))));

        return G_(res);
      }

      static formula
      kr2_exp_3(int n, formula* fa, formula* fb, formula fc, formula fd)
      {
        return G_(Implies_(Or_(fa[n - 1], fb[n - 1]),
                           X_(And_(fc,
                                   X_(Or_(fa[0],
                                          Or_(fb[0],
                                              Or_(fd, G_(fc)))))))));
      }

      static formula
      kr2_exp_4(int n, formula* fa, formula* fb, formula fc, formula fd)
      {
        return U_(Not_(fd),
                  And_(fd, X_(And_(Or_(fa[0], fb[0]), X_n(G_(fc), n)))));
      }

      static formula
      kr2_exp_5_r(int i, formula* fr, formula fd)
      {
        return And_(fr[i - 1], F_(And_(fd, F_(fr[i - 1]))));
      }

      static formula
      kr2_exp_5(int n, formula* fa, formula* fb, formula fc, formula fd)
      {
        formula facc = formula::ff();
        for (int i = 1; i <= n; i++)
          {
            formula for1 = kr2_exp_5_r(i, fa, fd);
            formula for2 = kr2_exp_5_r(i, fb, fd);
            facc = Or_(facc, (Or_(for1, for2)));
          }

        return F_(And_(fc,
                       X_(And_(Not_(fc), U_(facc, fc)))));
      }

      static formula
      kr2_exp_mutex(int n, formula* fa, formula* fb, formula fc, formula fd)
      {
        formula f1or = formula::ff();
        formula f3and = formula::tt();

        for (int i = 1; i <= n; i++)
          {
            f1or = Or_(f1or, Or_(fa[i - 1], fb[i - 1]));
            f3and = And_(f3and, Implies_(fa[i - 1], Not_(fb[i - 1])));
          }

        formula f1 = G_(Implies_(Or_(fc, fd), Not_(f1or)));
        formula f2 = G_(Implies_(fc, Not_(fd)));
        formula f3 = G_(f3and);

        return And_(f1, And_(f2, f3));
      }

      static formula
      kr2_exp(int n, std::string a, std::string b, std::string c, std::string d)
      {
        if (n <= 0)
          bad_number("kr-n", n);

        formula fc = formula::ap(c);
        formula fd = formula::ap(d);

        formula* fa = new formula[n];
        formula* fb = new formula[n];

        for (int i = 0; i < n; i++)
          {
            fa[i] = formula::ap(a + std::to_string(i + 1));
            fb[i] = formula::ap(b + std::to_string(i + 1));
          }

        formula res = formula::And({ kr2_exp_1(fa, fb, fc, fd),
              kr2_exp_2(n, fa, fb),
              kr2_exp_3(n, fa, fb, fc, fd),
              kr2_exp_4(n, fa, fb, fc, fd),
              kr2_exp_5(n, fa, fb, fc, fd),
              kr2_exp_mutex(n, fa, fb, fc, fd) });

        delete[] fa;
        delete[] fb;
        return res;
      }

      static formula
      sejk_f(std::string a, std::string b, int n, int m)
      {
        formula left = G_(F_(formula::ap(a + std::to_string(0))));
        formula right = X_n(formula::ap(b), m);
        formula f0 = U_(left, right);
        for (int i = 1; i <= n; ++i)
          {
            formula left = G_(F_(formula::ap(a + std::to_string(i))));
            f0 = U_(left, G_(f0));
          }
        return f0;
      }

      static formula
      sejk_j(std::string a, std::string b, int n)
      {
        return formula::Implies(GF_n(a, n), GF_n(b, n));
      }

      static formula
      sejk_k(std::string a, std::string b, int n)
      {
        formula result = formula::tt();
        for (int i = 1; i <= n; ++i)
          {
            formula ai = formula::ap(a + std::to_string(i));
            formula bi = formula::ap(b + std::to_string(i));
            result = formula::And({result,
                                   formula::Or({G_(F_(ai)), F_(G_(bi))})});
          }
        return result;
      }

      static formula
      sejk_pattern(int n)
      {
        static const char* formulas[] = {
          "GF(Fa | Gb | FG(a | Xb))",
          "FG(Ga | F!b | GF(a & Xb))",
          "GF(Fa | GXb | FG(a | XXb))",
        };

        constexpr unsigned max = (sizeof formulas)/(sizeof *formulas);
        if (n < 1 || (unsigned) n > max)
          bad_number("sejk-patterns", n, max);
        return spot::relabel(parse_formula(formulas[n - 1]), Pnn);
      }
    }

    static formula
    pps_arbiter(std::string r_, std::string g_, unsigned n, bool strict_)
    {
      formula* r = new formula[n];
      formula* g = new formula[n];
      std::vector<formula> res;

      for (unsigned i = 0; i < n; ++i)
        {
          r[i] = formula::ap(r_ + std::to_string(i + 1));
          g[i] = formula::ap(g_ + std::to_string(i + 1));
        }

      formula theta_e;
      formula theta_s;
      formula psi_e;
      formula psi_s;
      formula phi_e;
      formula phi_s;
      {
        std::vector<formula> res;
        for (unsigned i = 0; i < n; ++i)
          res.push_back(formula::Not(r[i]));
        theta_e = formula::And(res);

        res.clear();
        for (unsigned i = 0; i < n; ++i)
          res.push_back(formula::Not(g[i]));
        theta_s = formula::And(res);

        res.clear();
        for (unsigned i = 0; i < n; ++i)
          {
            formula left = formula::Xor(r[i], g[i]);
            formula right = formula::Equiv(r[i], formula::X(r[i]));
            res.push_back(formula::Implies(left, right));
          }
        psi_e = formula::And(res);

        res.clear();
        for (unsigned i = 0; i < n; ++i)
          {
            for (unsigned j = 0; j < i; ++j)
              res.push_back(formula::Not(formula::And({g[i], g[j]})));
            formula left = formula::Equiv(r[i], g[i]);
            formula right = formula::Equiv(g[i], formula::X(g[i]));
            res.push_back(formula::Implies(left, right));
          }
        psi_s = formula::And(res);

        res.clear();
        for (unsigned i = 0; i < n; ++i)
          {
            formula f = formula::Not(formula::And({r[i], g[i]}));
            res.push_back(formula::G(formula::F(f)));
          }
        phi_e = formula::And(res);

        res.clear();
        for (unsigned i = 0; i < n; ++i)
          {
            res.push_back(formula::G(formula::F(formula::Equiv(r[i], g[i]))));
          }
        phi_s = formula::And(res);
      }
      delete[] r;
      delete[] g;

      if (!strict_)
        {
          formula left = formula::And({formula::G(psi_e), phi_e});
          formula imp =
            formula::Implies(left, formula::And({formula::G(psi_s), phi_s}));
          return formula::Implies(theta_e, formula::And({theta_s, imp}));
        }
      else
        {
          formula e = formula::W(psi_s, formula::Not(psi_e));
          formula imp =
            formula::Implies(formula::And({formula::G(psi_e), phi_e}), phi_s);
          return formula::Implies(theta_e, formula::And({theta_s, e, imp}));
        }
    }

    // G[0..n]((a S b) -> c) rewritten using future operators,
    // from Edmond Irani Liu (EIL).  GSI stands for "Globally Since Implies."
    static formula eil_gsi(int n, std::string a, std::string b, std::string c)
    {
      formula fa = formula::ap(a);
      formula fb = formula::ap(b);
      formula res = fb;
      for (int i = 1; i <= n; ++i)
        {
          formula tmp = formula::And({formula::strong_X(i, fa), res});
          res = formula::Or({formula::strong_X(i, fb), tmp});
        }
      return formula::Implies(res, formula::strong_X(n, formula::ap(c)));
    }

    static formula
    counter_mealy(int n)
    {
      // See section B.1 in
      // https://bitbucket.org/ijcai2816/ijcai-2816/src/master/Appendix.pdf
      if (n <= 0)
        bad_number("tv-counter-mealy", n);
      std::vector<formula> v;
      std::vector<formula> z;   // zeros
      v.reserve(3 * n + 1);
      z.reserve(n);
      formula inc = formula::ap("inc");
      formula bim1;             // b[i-1]
      formula cim1;             // c[i-1]
      for (int i = 0; i < n; ++i)
        {
          std::string si = std::to_string(i);
          formula bi = formula::ap("ob" + si);
          formula ci = formula::ap("oc" + si);
          formula ii = formula::ap("init" + si);
          v.push_back(Equiv_(bi, ii)); // b[i] <-> init[i]
          if (i == 0)
            v.push_back(G_(Equiv_(ci, inc))); // G(c[0] <-> inc)
          else                  // G(c[i] <-> (b[i-1]&c[i-1]))
            v.push_back(G_(Equiv_(ci, And_(bim1, cim1))));
          // G((X[!]b[i] -> (b[i] xor c[i])) & (X[!](!b[i]) -> (b[i] <-> c[i])))
          v.push_back(G_(And_(Implies_(Xs_(bi), Xor_(bi, ci)),
                              Implies_(Xs_(Not_(bi)), Equiv_(bi, ci)))));
          z.push_back(Not_(bi));
          bim1 = bi;
          cim1 = ci;
        }
      // G(!inc->X[!]inc) -> F(!b[0] && ... && !b[n-1])
      v.push_back(Implies_(G_(Implies_(Not_(inc), Xs_(inc))),
                           F_(formula::And(z))));
      return formula::And(v);
    }

    static formula
    counters_mealy(int n)
    {
      // See section B.2 in
      // https://bitbucket.org/ijcai2816/ijcai-2816/src/master/Appendix.pdf
      if (n <= 0)
        bad_number("tv-counter-mealy", n);
      std::vector<formula> v;
      std::vector<formula> z;   // equivalent counters
      v.reserve(6 * n + 1);
      z.reserve(n);
      formula iinc = formula::ap("iinc");
      formula oinc = formula::ap("oinc");
      formula ibim1;             // ib[i-1]
      formula obim1;             // ob[i-1]
      formula icim1;             // ic[i-1]
      formula ocim1;             // oc[i-1]
      for (int i = 0; i < n; ++i)
        {
          std::string si = std::to_string(i);
          formula ii = formula::ap("init" + si);
          formula ibi = formula::ap("obe" + si);
          formula ici = formula::ap("oce" + si);
          formula obi = formula::ap("obs" + si);
          formula oci = formula::ap("ocs" + si);
          v.push_back(Equiv_(ibi, ii)); // ib[i] <-> init[i]
          v.push_back(Not_(obi)); // !ob[i]
          if (i == 0)
            {
              v.push_back(G_(Equiv_(oci, oinc))); // G(oc[0] <-> oinc)
              v.push_back(G_(Equiv_(ici, iinc))); // G(ic[0] <-> iinc)
            }
          else
            {
              // G(oc[i] <-> (ob[i-1]&oc[i-1]))
              // G(ic[i] <-> (ib[i-1]&ic[i-1]))
              v.push_back(G_(Equiv_(oci, And_(obim1, ocim1))));
              v.push_back(G_(Equiv_(ici, And_(ibim1, icim1))));
            }
          // G((X[!]ob[i]->(ob[i] xor oc[i]))&(X[!](!ob[i])->(ob[i]<->oc[i])))
          // G((X[!]ib[i]->(ib[i] xor ic[i]))&(X[!](!ib[i])->(ib[i]<->ic[i])))
          v.push_back(G_(And_(Implies_(Xs_(obi), Xor_(obi, oci)),
                              Implies_(Xs_(Not_(obi)), Equiv_(obi, oci)))));
          v.push_back(G_(And_(Implies_(Xs_(ibi), Xor_(ibi, ici)),
                              Implies_(Xs_(Not_(ibi)), Equiv_(ibi, ici)))));
          z.push_back(Equiv_(ibi, obi));
          ibim1 = ibi;
          obim1 = obi;
          icim1 = ici;
          ocim1 = oci;
        }
      // G(inc->!X[!]inc) -> F((ib[0]<->ob[o]) && ... && (ib[n-1]<->ob[n-1]))
      v.push_back(Implies_(G_(Implies_(iinc, Not_(Xs_(iinc)))),
                           F_(formula::And(z))));
      return formula::And(v);
    }

    static formula
    nim_mealy(int n, int m)
    {
      // Nim game with n heaps of size m.
      // See section B.3 in
      // https://bitbucket.org/ijcai2816/ijcai-2816/src/master/Appendix.pdf
      if (n <= 0)
        bad_number("tv-nim-mealy", n);
      if (m <= 0)
        bad_number("tv-nim-mealy", m);

      std::vector<formula> sel_env;
      sel_env.reserve(n);
      std::vector<formula> chg_env;
      chg_env.reserve(m);
      std::vector<formula> sel_sys;
      sel_sys.reserve(n);
      std::vector<formula> chg_sys;
      chg_sys.reserve(m);
      std::vector<formula> heap;
      heap.reserve(n * (m + 1));
#define Heap_(h, c) (heap[(h) * (m + 1) + (c)])
      formula t_env = formula::ap("oti");
      formula t_sys = formula::ap("oto");
      formula one_chg_env = formula::ff();
      formula one_chg_sys = formula::ff();
      formula one_sel_env = formula::ff();
      formula one_sel_sys = formula::ff();
      formula nonempty = formula::ff();
      formula empty = formula::tt();
      for (int h = 0; h < n; ++h)
        {
          std::string hs = std::to_string(h);
          formula ish = formula::ap("is" + hs);
          sel_env.push_back(ish);
          one_sel_env = Or_(one_sel_env, ish);
          formula osh = formula::ap("os" + hs);
          sel_sys.push_back(osh);
          one_sel_sys = Or_(one_sel_sys, osh);
          for (int c = 0; c <= m; ++c)
            {
              std::string cs = std::to_string(c);
              formula hh = formula::ap("o" + hs + "h" + cs);
              heap.push_back(hh);
              if (c == 0)
                {
                  nonempty = Or_(nonempty, Not_(hh));
                  empty = And_(empty, hh);
                }
              if (h == 0 && c < m)
                {
                  formula chge = formula::ap("ic" + cs);
                  chg_env.push_back(chge);
                  one_chg_env = Or_(one_chg_env, chge);
                  formula chgs = formula::ap("oc" + cs);
                  chg_sys.push_back(chgs);
                  one_chg_sys = Or_(one_chg_sys, chgs);
                }
            }
        }

      // system starts playing
      std::vector<formula> init = {And_(t_sys, Not_(t_env))};
      // when its your turn you have to select a one heap
      std::vector<formula> rules_s = {Implies_(t_sys, one_sel_sys)};
      std::vector<formula> rules_e = {Implies_(t_env, one_sel_env)};
      for (int h = 0; h < n; ++h)
        {
          // If env hasn't selected heap h for its first move, that
          // heap has m tokens.
          init.push_back(Implies_(Not_(sel_sys[h]), Heap_(h, m)));
          // If heap h is selected, one change value must be true.
          init.push_back(Implies_(sel_sys[h], one_chg_sys));

          // Heaps are mutually exclusive
          for (int h2 = h + 1; h2 < n; ++h2)
            {
              rules_s.push_back(Not_(And_(sel_sys[h], sel_sys[h2])));
              rules_e.push_back(Not_(And_(sel_env[h], sel_env[h2])));
            }
          // changes are mutually exclusive
          for (int c = 0; c < m; ++c)
            for (int o = 0; o < c; ++o)
              {
                rules_e.push_back(Not_(And_(chg_env[o], chg_env[c])));
                rules_s.push_back(Not_(And_(chg_sys[o], chg_sys[c])));
              }
          for (int c = 0; c <= m; ++c)
            {
              // The content of the heap restrict the possible choice on
              // next turn.
              std::vector<formula> opte;
              std::vector<formula> opts;
              opte.reserve(c);
              opts.reserve(c);
              for (int o = 0; o < c; ++o)
                {
                  opte.push_back(chg_env[o]);
                  opts.push_back(chg_sys[o]);
                }
              formula opts_e = Xs_(formula::Or(opte));
              formula opts_s = Xs_(formula::Or(opts));
              formula hhc = And_(nonempty, Heap_(h, c));
              rules_e.push_back(Implies_(And_(hhc, Xs_(sel_env[h])), opts_e));
              rules_s.push_back(Implies_(And_(hhc, Xs_(sel_sys[h])), opts_s));
            }
        }
      // turns alternate between system and environment
      rules_s.push_back(Xor_(t_env, t_sys));
      rules_s.push_back(Implies_(Xs_(t_sys), t_env));
      rules_s.push_back(Implies_(Xs_(t_env), t_sys));
      // each heap can have only one value at a time.
      for (int h = 0; h < n; ++h)
        for (int c = 0; c <= m; ++c)
          for (int c2 = c + 1; c2 <= m; ++c2)
            rules_s.push_back(Not_(And_(Heap_(h, c), Heap_(h, c2))));
      // updating the heap
      for (int h = 0; h < n; ++h)
        {
          formula seh = And_(t_env, sel_env[h]);
          formula ssh = And_(t_sys, sel_sys[h]);
          for (int c = 0; c < m; ++c)
            {
              rules_s.push_back(Implies_(And_(seh, chg_env[c]), Heap_(h, c)));
              rules_s.push_back(Implies_(And_(ssh, chg_sys[c]), Heap_(h, c)));
            }
        }
      for (int h = 0; h < n; ++h)
        {
          formula xsenh = Xs_(And_(t_env, Not_(sel_env[h])));
          formula xssnh = Xs_(And_(t_sys, Not_(sel_sys[h])));
          for (int c = 0; c <= m; ++c)
            {
              rules_s.push_back(Implies_(And_(xsenh, Heap_(h, c)),
                                         Xs_(Heap_(h, c))));
              rules_s.push_back(Implies_(And_(xssnh, Heap_(h, c)),
                                         Xs_(Heap_(h, c))));
            }
        }


      formula rul_e = G_(formula::And(rules_e));
      formula rul_s = G_(formula::And(rules_s));
      init.push_back(Implies_(rul_e, And_(rul_s,
                                          U_(nonempty,
                                             And_(t_env, empty)))));
      return formula::And(init);
    }

    static formula
    chomp_mealy(int n, int m)
    {
      // Chomp game on grid of width N, height Mn heaps of size m.
      // https://en.wikipedia.org/wiki/Chomp
      if (n <= 0)
        bad_number("chomp-mealy", n);
      if (m <= 0)
        bad_number("chomp-mealy", m);

#define Pos_(x, y) (pos[(x) + (y) * n])
      std::vector<formula> pos;
      pos.reserve(n * m);
      std::vector<formula> ix;
      ix.reserve(n);
      std::vector<formula> iy;
      iy.reserve(m);
      std::vector<formula> ox;
      ox.reserve(n);
      std::vector<formula> oy;
      oy.reserve(m);
      formula t_env = formula::ap("oti");
      formula t_sys = formula::ap("oto");
      for (int y = 0; y < m; ++y)
        {
          std::string ys = std::to_string(y);
          oy.push_back(formula::ap("oy" + ys));
          iy.push_back(formula::ap("iy" + ys));
          for (int x = 0; x < n; ++x)
            {
              std::string xs = std::to_string(x);
              formula posxy = formula::ap("o" + xs + "b" + ys);
              pos.push_back(posxy);
              if (y == 0)
                {
                  ox.push_back(formula::ap("ox" + xs));
                  ix.push_back(formula::ap("ix" + xs));
                }
            }
        }

      // system starts playing
      std::vector<formula> init = {t_sys, Not_(t_env)};
      // G((oti xor oto) & oti->Xoto & oto->Xoti)
      init.push_back(G_(Xor_(t_sys, t_env)));
      init.push_back(G_(Implies_(t_sys, X_(t_env))));
      init.push_back(G_(Implies_(t_env, X_(t_sys))));

      // oIbJ is true initially IFF ox[x] and oy[y] are false
      for (int y = 0; y < m; ++y)
        for (int x = 0; x < n; ++x)
          //    Pos(x,y) xor (ox[x] & oy[y])
          init.push_back(formula::Xor(Pos_(x, y), And_(ox[x], oy[y])));

      std::vector<formula> orules; // rules for output player
      std::vector<formula> irules; // rules for input player
      // ox[x] -> ox[x+1]
      // oy[y] -> oy[y+1]
      for (int x = 0; x < n - 1; ++x)
        {
          orules.push_back(Implies_(ox[x], ox[x+1]));
          irules.push_back(Implies_(ix[x], ix[x+1]));
        }
      for (int y = 0; y < m - 1; ++y)
        {
          orules.push_back(Implies_(oy[y], oy[y+1]));
          irules.push_back(Implies_(iy[y], iy[y+1]));
        }

      // at least one square must be taken at each turn
      // as long as (0,0) is available.
      {
        std::vector<formula> tmp;
        tmp.reserve(n * m);
        for (int y = 0; y < m; ++y)
          for (int x = 0; x < n; ++x)
            tmp.push_back(And_(Pos_(x, y), X_(And_(ix[x], iy[y]))));
        formula change = formula::Or(tmp);
        irules.push_back(Implies_(And_(Pos_(0, 0), t_sys), change));
        tmp.clear();
        for (int y = 0; y < m; ++y)
          for (int x = 0; x < n; ++x)
            tmp.push_back(And_(Pos_(x, y), X_(And_(ox[x], oy[y]))));
        change = formula::Or(tmp);
        orules.push_back(Implies_(And_(Pos_(0, 0), t_env), change));
      }

      // when its not the environment/controller turn, nothing should
      // be selected.  Also make sure the system selects something
      // initially.
      {
        std::vector<formula> onegs;
        onegs.reserve(n + m);
        std::vector<formula> inegs;
        inegs.reserve(n + m);
        std::vector<formula> otmp;
        otmp.reserve(std::max(n, m));
        for (int x = 0; x < n; ++x)
          {
            otmp.push_back(ox[x]);
            onegs.push_back(Not_(ox[x]));
            inegs.push_back(Not_(ix[x]));
          }
        // system has to play something initially
        init.push_back(formula::Or(otmp));
        otmp.clear();
        for (int y = 0; y < m; ++y)
        {
          otmp.push_back(oy[y]);
          onegs.push_back(Not_(oy[y]));
          inegs.push_back(Not_(iy[y]));
        }
        // system has to play something initially
        init.push_back(formula::Or(otmp));

        formula oneg = formula::And(onegs);
        formula ineg = formula::And(inegs);
        orules.push_back(Implies_(Not_(t_sys), oneg));
        irules.push_back(Implies_(Not_(t_env), ineg));
      }


      // !Pos(x,y) -> X(!Pos(x,y))
      // Pos(x,y) -> X(Pos(x,y) | (ix[x] & iy[y] | (ox[x] & oy[y])))
      // This could be a strong X, but it does not have to because
      // continuation is ensured by the U constraint later.  Keeping
      // the X weak makes it possible to use this as LTL too.
      // ((ox[x] & oy[y]) | (ix[x] & iy[y])) -> !Pos(x,y)
      {
        for (int y = 0; y < m; ++y)
          for (int x = 0; x < n; ++x)
            {
              formula npos = Not_(Pos_(x, y));
              orules.push_back(Implies_(npos, X_(npos)));
              formula osel = And_(ox[x], oy[y]);
              formula isel = And_(ix[x], iy[y]);

              orules.push_back(Implies_(Pos_(x, y),
                                        X_(Or3_(Pos_(x, y), osel, isel))));
              orules.push_back(Implies_(Or_(osel, isel), npos));
            }
      }


      formula irules_g = G_(formula::And(irules));
      formula orules_g = G_(formula::And(orules));

      // The game ends when (0,0) is taken, and we want the
      // environment to take it for the system to win.
      formula last = Pos_(0, 0);
      init.push_back(Implies_(irules_g, And_(orules_g,
                                             U_(last,
                                                And_(t_env, Not_(last))))));
      return formula::And(init);
    }


    formula ltl_pattern(ltl_pattern_id pattern, int n, int m)
    {
      if (n < 0)
        {
          std::ostringstream err;
          err << "pattern argument for " << ltl_pattern_name(pattern)
              << " should be positive";
          throw std::runtime_error(err.str());
        }
      if ((m >= 0) ^ (ltl_pattern_argc(pattern) == 2))
        {
          std::ostringstream err;
          err << "unexpected number of arguments for "
              << ltl_pattern_name(pattern);
          throw std::runtime_error(err.str());
        }

      switch (pattern)
        {
          // Keep this alphabetically-ordered!
        case LTL_AND_F:
          return combunop_n("p", n, op::F, true);
        case LTL_AND_FG:
          return FG_n("p", n, true);
        case LTL_AND_GF:
          return GF_n("p", n, true);
        case LTL_CCJ_ALPHA:
          return formula::And({E_n("p", n), E_n("q", n)});
        case LTL_CCJ_BETA:
          return formula::And({N_n("p", n), N_n("q", n)});
        case LTL_CCJ_BETA_PRIME:
          return formula::And({N_prime_n("p", n), N_prime_n("q", n)});
        case LTL_DAC_PATTERNS:
          return dac_pattern(n);
        case LTL_EH_PATTERNS:
          return eh_pattern(n);
        case LTL_EIL_GSI:
          return eil_gsi(n, "a", "b", "c");
        case LTL_FXG_OR:
          return FXG_or_n("p", n);
        case LTL_GF_EQUIV:
          return GF_equiv_implies(n, "a", "z", true);
        case LTL_GF_EQUIV_XN:
          return GF_equiv_implies_xn(n, "a", true);
        case LTL_GF_IMPLIES:
          return GF_equiv_implies(n, "a", "z", false);
        case LTL_GF_IMPLIES_XN:
          return GF_equiv_implies_xn(n, "a", false);
        case LTL_GH_Q:
          return Q_n("p", n);
        case LTL_GH_R:
          return R_n("p", n);
        case LTL_GO_THETA:
          return fair_response("p", "q", "r", n);
        case LTL_GXF_AND:
          return GXF_and_n("p", n);
        case LTL_HKRSS_PATTERNS:
          return hkrss_pattern(n);
        case LTL_KR_N:
          return kr2_exp(n, "a", "b", "c", "d");
        case LTL_KR_NLOGN:
          return kr1_exp(n, "a", "b", "c", "d", "y", "z");
        case LTL_KV_PSI:
          return kv_exp(n, "a", "b", "c", "d");
        case LTL_LILY_PATTERNS:
          return lily_pattern(n);
        case LTL_MS_EXAMPLE:
          return ms_example("a", "b", n, m);
        case LTL_MS_PHI_H:
          return ms_phi_h("a", "b", n);
        case LTL_MS_PHI_R:
          return ms_phi_rs("a", "b", n, true);
        case LTL_MS_PHI_S:
          return ms_phi_rs("a", "b", n, false);
        case LTL_OR_FG:
          return FG_n("p", n, false);
        case LTL_OR_G:
          return combunop_n("p", n, op::G, false);
        case LTL_OR_GF:
          return GF_n("p", n, false);
        case LTL_P_PATTERNS:
          return p_pattern(n);
        case LTL_PPS_ARBITER_STANDARD:
          return pps_arbiter("i", "o", n, false);
        case LTL_PPS_ARBITER_STRICT:
          return pps_arbiter("i", "o", n, true);
        case LTL_R_LEFT:
          return bin_n("p", n, op::R, false);
        case LTL_R_RIGHT:
          return bin_n("p", n, op::R, true);
        case LTL_RV_COUNTER_CARRY:
          return ltl_counter_carry("b", "m", "c", n, false);
        case LTL_RV_COUNTER_CARRY_LINEAR:
          return ltl_counter_carry("b", "m", "c", n, true);
        case LTL_RV_COUNTER:
          return ltl_counter("b", "m", n, false);
        case LTL_RV_COUNTER_LINEAR:
          return ltl_counter("b", "m", n, true);
        case LTL_SB_PATTERNS:
          return sb_pattern(n);
        case LTL_SEJK_F:
          return sejk_f("a", "b", n, m);
        case LTL_SEJK_J:
          return sejk_j("a", "b", n);
        case LTL_SEJK_K:
          return sejk_k("a", "b", n);
        case LTL_SEJK_PATTERNS:
          return sejk_pattern(n);
        case LTL_TV_F1:
          return tv_f1("p", "q", n);
        case LTL_TV_F2:
          return tv_f2("p", "q", n);
        case LTL_TV_G1:
          return tv_g1("p", "q", n);
        case LTL_TV_G2:
          return tv_g2("p", "q", n);
        case LTL_TV_UU:
          return tv_uu("p", n);
        case LTL_U_LEFT:
          return bin_n("p", n, op::U, false);
        case LTL_U_RIGHT:
          return bin_n("p", n, op::U, true);
        case LTLF_CHOMP_MEALY:
          return chomp_mealy(n, m);
        case LTLF_TV_COUNTER_MEALY:
          return counter_mealy(n);
        case LTLF_TV_DOUBLE_COUNTERS_MEALY:
          return counters_mealy(n);
        case LTLF_TV_NIM_MEALY:
          return nim_mealy(n, m);
        case LTL_END:
          break;
        }
      throw std::runtime_error("unsupported pattern");
    }


    const char* ltl_pattern_name(ltl_pattern_id pattern)
    {
      static const char* const class_name[] =
        {
          "and-f",
          "and-fg",
          "and-gf",
          "ccj-alpha",
          "ccj-beta",
          "ccj-beta-prime",
          "dac-patterns",
          "eh-patterns",
          "eil-gsi",
          "fxg-or",
          "gf-equiv",
          "gf-equiv-xn",
          "gf-implies",
          "gf-implies-xn",
          "gh-q",
          "gh-r",
          "go-theta",
          "gxf-and",
          "hkrss-patterns",
          "kr-n",
          "kr-nlogn",
          "kv-psi",
          "lily-patterns",
          "ms-example",
          "ms-phi-h",
          "ms-phi-r",
          "ms-phi-s",
          "or-fg",
          "or-g",
          "or-gf",
          "p-patterns",
          "pps-arbiter-standard",
          "pps-arbiter-strict",
          "r-left",
          "r-right",
          "rv-counter",
          "rv-counter-carry",
          "rv-counter-carry-linear",
          "rv-counter-linear",
          "sb-patterns",
          "sejk-f",
          "sejk-j",
          "sejk-k",
          "sejk-patterns",
          "tv-f1",
          "tv-f2",
          "tv-g1",
          "tv-g2",
          "tv-uu",
          "u-left",
          "u-right",
          "chomp-mealy",
          "tv-counter-mealy",
          "tv-double-counters-mealy",
          "tv-nim-mealy",
        };
      // Make sure we do not forget to update the above table every
      // time a new pattern is added.
      static_assert(sizeof(class_name)/sizeof(*class_name)
                    == LTL_END - LTL_BEGIN, "size mismatch");
      if (pattern < LTL_BEGIN || pattern >= LTL_END)
        throw std::runtime_error("unsupported pattern");
      return class_name[pattern - LTL_BEGIN];
    }

    int ltl_pattern_max(ltl_pattern_id pattern)
    {
      switch (pattern)
        {
          // Keep this alphabetically-ordered!
        case LTL_AND_F:
        case LTL_AND_FG:
        case LTL_AND_GF:
        case LTL_CCJ_ALPHA:
        case LTL_CCJ_BETA:
        case LTL_CCJ_BETA_PRIME:
          return 0;
        case LTL_DAC_PATTERNS:
          return 55;
        case LTL_EH_PATTERNS:
          return 12;
        case LTL_EIL_GSI:
        case LTL_FXG_OR:
        case LTL_GF_EQUIV:
        case LTL_GF_EQUIV_XN:
        case LTL_GF_IMPLIES:
        case LTL_GF_IMPLIES_XN:
        case LTL_GH_Q:
        case LTL_GH_R:
        case LTL_GO_THETA:
        case LTL_GXF_AND:
          return 0;
        case LTL_HKRSS_PATTERNS:
          return 55;
        case LTL_KR_N:
        case LTL_KR_NLOGN:
        case LTL_KV_PSI:
          return 0;
        case LTL_LILY_PATTERNS:
          return 23;
        case LTL_MS_EXAMPLE:
        case LTL_MS_PHI_H:
        case LTL_MS_PHI_R:
        case LTL_MS_PHI_S:
        case LTL_OR_FG:
        case LTL_OR_G:
        case LTL_OR_GF:
          return 0;
        case LTL_P_PATTERNS:
          return 20;
        case LTL_PPS_ARBITER_STANDARD:
        case LTL_PPS_ARBITER_STRICT:
        case LTL_R_LEFT:
        case LTL_R_RIGHT:
        case LTL_RV_COUNTER_CARRY:
        case LTL_RV_COUNTER_CARRY_LINEAR:
        case LTL_RV_COUNTER:
        case LTL_RV_COUNTER_LINEAR:
          return 0;
        case LTL_SB_PATTERNS:
          return 27;
        case LTL_SEJK_F:
        case LTL_SEJK_J:
        case LTL_SEJK_K:
          return 0;
        case LTL_SEJK_PATTERNS:
          return 3;
        case LTL_TV_F1:
        case LTL_TV_F2:
        case LTL_TV_G1:
        case LTL_TV_G2:
        case LTL_TV_UU:
        case LTL_U_LEFT:
        case LTL_U_RIGHT:
        case LTLF_CHOMP_MEALY:
        case LTLF_TV_COUNTER_MEALY:
        case LTLF_TV_DOUBLE_COUNTERS_MEALY:
        case LTLF_TV_NIM_MEALY:
          return 0;
        case LTL_END:
          break;
        }
      throw std::runtime_error("unsupported pattern");
    }

    int ltl_pattern_argc(ltl_pattern_id pattern)
    {
      switch (pattern)
        {
          // Keep this alphabetically-ordered!
        case LTL_AND_F:
        case LTL_AND_FG:
        case LTL_AND_GF:
        case LTL_CCJ_ALPHA:
        case LTL_CCJ_BETA:
        case LTL_CCJ_BETA_PRIME:
        case LTL_DAC_PATTERNS:
        case LTL_EH_PATTERNS:
        case LTL_EIL_GSI:
        case LTL_FXG_OR:
        case LTL_GF_EQUIV:
        case LTL_GF_EQUIV_XN:
        case LTL_GF_IMPLIES:
        case LTL_GF_IMPLIES_XN:
        case LTL_GH_Q:
        case LTL_GH_R:
        case LTL_GO_THETA:
        case LTL_GXF_AND:
        case LTL_HKRSS_PATTERNS:
        case LTL_KR_N:
        case LTL_KR_NLOGN:
        case LTL_KV_PSI:
        case LTL_LILY_PATTERNS:
          return 1;
        case LTL_MS_EXAMPLE:
          return 2;
        case LTL_MS_PHI_H:
        case LTL_MS_PHI_R:
        case LTL_MS_PHI_S:
        case LTL_OR_FG:
        case LTL_OR_G:
        case LTL_OR_GF:
        case LTL_P_PATTERNS:
        case LTL_PPS_ARBITER_STANDARD:
        case LTL_PPS_ARBITER_STRICT:
        case LTL_R_LEFT:
        case LTL_R_RIGHT:
        case LTL_RV_COUNTER_CARRY:
        case LTL_RV_COUNTER_CARRY_LINEAR:
        case LTL_RV_COUNTER:
        case LTL_RV_COUNTER_LINEAR:
        case LTL_SB_PATTERNS:
          return 1;
        case LTL_SEJK_F:
          return 2;
        case LTL_SEJK_J:
        case LTL_SEJK_K:
        case LTL_SEJK_PATTERNS:
        case LTL_TV_F1:
        case LTL_TV_F2:
        case LTL_TV_G1:
        case LTL_TV_G2:
        case LTL_TV_UU:
        case LTL_U_LEFT:
        case LTL_U_RIGHT:
          return 1;
        case LTLF_CHOMP_MEALY:
          return 2;
        case LTLF_TV_COUNTER_MEALY:
        case LTLF_TV_DOUBLE_COUNTERS_MEALY:
          return 1;
        case LTLF_TV_NIM_MEALY:
          return 2;
        case LTL_END:
          break;
        }
      throw std::runtime_error("unsupported pattern");
    }
  }
}
