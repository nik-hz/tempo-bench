.\" -*- coding: utf-8 -*-
[NAME]
ltlf2dfa \- translate LTLf formulas into DFA

[NOTE ON OUTPUT FORMATS]
Spot currently has little support for finite automata, and does
not really support any DFA output format at the moment.  When
the \fB\-\-dot\fR or \fB\-\-hoaf\fR opetions are used, the DFA is
converted into a Büchi automaton before being displayed.

[BIBLIOGRAPHY]
The following paper describes how direct translation from LTLf to MTDFA works.

.TP
\(bu
Alexandre Duret-Lutz, Shufang Zhu, Nir Piterman, Giuseppe De Giacomo,
and Moshe Y. Vardi: Engineering an LTLf Synthesis Tool. Proceedings
of CIAA'25. To appear.

[SEE ALSO]
.BR ltlfsynt (1)
