#!/usr/bin/python3

"""
Main module that exposes the command line interface.
"""

import sys, getopt
import parse
import spot
import cause


def main(argv):
    sysfile = ""
    effectfile = ""
    tracefile = ""
    contingencies = False
    limitassumption = False
    causecheck = False
    usage = (
        "Usage: corp.py -s <systemfile> -e <effectfile> -t <tracefile> -o <outputfile> [options]"
    )
    options = (
        "Options are:\n\t--contingencies, -c \n\t\ttoggles the inclusion of contingencies.\n\t"
        + "--assumelimit, -a \n\t\tuses the distance metric that satisfies the limit assumption."
    )
    man = "%s\n\n%s" % (usage, options)

    try:
        opts, args = getopt.getopt(
            argv,
            "hs:e:t:o:ca",
            [
                "help",
                "system=",
                "effect=",
                "trace=",
                "output=",
                "contingencies",
                "assumelimit",
                "check=",
            ],
        )
    except getopt.GetoptError:
        print(man)
        sys.exit(2)

    for opt, arg in opts:
        if opt == "-h":
            print(man)
            sys.exit(2)
        elif opt in ("-s", "--system"):
            sysfile = arg
        elif opt in ("-e", "--effect"):
            effectfile = arg
        elif opt in ("-t", "--trace"):
            tracefile = arg
        elif opt in ("-o", "--output"):
            outputfile = arg
        elif opt in ("-c", "--contingencies"):
            contingencies = True
        elif opt in ("-a", "--assumelimit"):
            limitassumption = True
        elif opt in ("--check"):
            causecheck = True
            candidatefile = arg

    system = spot.automaton(sysfile)
    effect = parse.effectfile(effectfile)
    trace = parse.tracefile(tracefile)

    result = cause.synthesize(system, trace, effect, limitassumption, contingencies)
    if result.is_empty():
        if not causecheck:
            print("No cause exists.")
            exit(0)
        candidate = parse.propertyfile(candidatefile)
        if candidate.is_empty():
            print("Is cause.")
            exit(0)
        else:
            print("No cause.")
            exit(0)
    else:
        result.save(outputfile)
        if not causecheck:
            print("Cause found by CORP.")
            exit(0)
        candidate = parse.propertyfile(candidatefile)
        if candidate.equivalent_to(result):
            print("Is cause.")
            exit(0)
        else:
            print("No cause.")
            exit(0)


if __name__ == "__main__":
    main(sys.argv[1:])
