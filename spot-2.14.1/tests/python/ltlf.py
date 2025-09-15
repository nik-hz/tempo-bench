# -*- mode: python; coding: utf-8 -*-
# Copyright (C) by the Spot authors, see the AUTHORS file for details.
#
# This file is part of Spot, a model checking library.
#
# Spot is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Spot is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
# License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import spot
from unittest import TestCase
tc = TestCase()

lcc = spot.language_containment_checker()

formulas = ['GFa', 'FGa', '(GFa) U b',
            '(a U b) U c', 'a U (b U c)',
            '(a W b) W c', 'a W (b W c)',
            '(a R b) R c', 'a R (b R c)',
            '(a M b) M c', 'a M (b M c)',
            '(a R b) U c', 'a U (b R c)',
            '(a M b) W c', 'a W (b M c)',
            '(a U b) R c', 'a R (b U c)',
            '(a W b) M c', 'a M (b W c)',
            ]

# The rewriting assume the atomic proposition will not change
# once we reache the non-alive part.
cst = spot.formula('G(X!alive => ((a <=> Xa) && (b <=> Xb) && (c <=> Xc)))')

for f in formulas:
    f1 = spot.formula(f)
    f2 = f1.unabbreviate()
    f3 = spot.formula_And([spot.from_ltlf(f1), cst])
    f4 = spot.formula_And([spot.from_ltlf(f2), cst])
    print(f"{f1}\t=>\t{f3}")
    print(f"{f2}\t=>\t{f4}")
    tc.assertTrue(lcc.equal(f3, f4))
    print()


ls = spot.ltlf_simplifier()

for i, j in [('!X!X!a', 'X[!]X!a'),
             ('X!X!a', 'XX[!]a'),
             ('!(a U (b W (c R (d M e))))', '!a R (!b M (!c U (!d W !e)))'),
             ('!GFGa', 'FGF!a'),
             ('(Fa & Fb) | (Fa & Fc)', 'Fa & F(b | c)'),
             ('(Ga & Gb & Gd) | (Ga & Gc & Gd)', 'G(a & b & d) | G(a & c & d)'),
             ('(Xa & Fb & Gd) | (Xa & X[!]c & Gd) | Gd', 'Gd'),
             ('(Xa | Gb) & (Xa | Fc)', 'Xa | (Gb & Fc)'),
             ('(Xa | Fb | Gd ) & (Xa | Fc | Gd)', 'Xa | Gd | (Fb & Fc)'),
             ('(Xa | Fb | Gd) & (Xa | Fc | Gd) & Gd', 'Gd'),
             ('!Xa -> b', 'Xa | b'),
             ('Xa -> Gb', 'Xa -> Gb'),
             ('Xa -> !Gb', 'Xa -> F!b'),
             ('!Xa -> !Gb', 'Xa | F!b'),
             ('!(Xa -> Gb)', 'Xa & F!b'),
             ('!(!Xa -> Gb)', 'X[!]!a & F!b'),
             ('!(Xa -> !Gb)', 'Xa & Gb'),
             ('!(!Xa -> !Gb)', 'X[!]!a & Gb'),
             ('(Ga -> b) & (Gb -> c) & (Ga -> d) & Gf & Gg',
              '(F!a | (b & d)) & (c | F!b) & G(f & g)'),
             ('(a -> Gb) | (c -> Gd) | (Fe -> Gb) | Fg | Fh',
              '!a | !c | Gb | Gd | F(g | h) | G!e'),
             ('Xa <->Gb', 'Xa<->Gb'),
             ('!Xa xor Gb', 'Xa<->Gb'),
             ('!Xa <-> !Gb', 'Xa<->Gb'),
             ('!Xa xor Gb', 'Xa<->Gb'),
             ('Xa <-> !Gb', 'Xa xor Gb'),
             ('X(a) | X(!b) | Gc | Fd | Fe', 'X(a | !b) | Gc | F(d | e)'),
             ('X(a) & X(!b) & Gc & Gd & Fe & Ff',
              'X(a & !b) & G(c & d) & Fe & Ff'),
             ('X(a) & G(!b) & GFc & GFd & Fe & Ff',
              'X(a) & G(!b & F(c & d)) & Fe & Ff'),
             ]:
    f1 = spot.formula(i)
    f2 = spot.formula(j)
    f3 = ls.simplify(f1)
    print(f1, "  =>  ", f3)
    tc.assertEqual(f2, f3)
    a = spot.ltlf_to_mtdfa(f1)
    b = spot.ltlf_to_mtdfa(f2)
    tc.assertTrue(spot.product_xor(a, b).is_empty())
