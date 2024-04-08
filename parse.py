import spot

'''
Parses the given counterexample trace and returns it as a tuple of lists over sets of APs.
'''
def tracefile(filename):
    contents = open(filename).read()  
    return spot.parse_word(contents.rstrip())

'''
Parses any property from the given file and handles both LTL formulas and HOA automata.
'''
def propertyfile(filename):
    try:
        return spot.postprocess(spot.automaton(filename),'buchi','state-based','small','high')
    except:
        return spot.postprocess(spot.translate(spot.formula(open(filename).read())),'buchi','state-based','small','high')

'''
Parses the effect from the given file and handles both LTL formulas and HOA automata.
IMMEDIATELY NEGATES THE PROPERTY! (For runtime gains with formulas).
'''
def effectfile(filename):
    try:
        return spot.postprocess(spot.complement(spot.automaton(filename)),'buchi','state-based','small','high')
    except:
        return spot.postprocess(spot.translate(spot.formula("!" + open(filename).read())),'buchi','state-based','small','high')