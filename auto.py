'''
This module provides the necessary automata transformations that are not already exposed by the spot Python bindings.
'''

import spot
import buddy
from itertools import chain, combinations

def powerset(iterable):
    s = list(iterable)
    return chain.from_iterable(combinations(s, r) for r in range(len(s)+1))

def construct_counterfactual_automaton(system,trace):
    '''
    Given a system and a trace in lasso-shape constructs an counterfactual automaton.
    ''' 

    # We start with the original alphabet (which will be projected away in the end) and later add fresh APs for the inputs, 
    # which will stay until the end, where they have to be renamed again to match the original alphabet.
    bbdict = system.get_dict()
    result = spot.make_twa_graph(bbdict)

    # We remember what internal state the (state,position) from the integral automaton maps to in this map.
    smap = {}
    # Function for looking up this correspondence and adding a new internal state to the integral automaton if necessary.
    def lookup(state, position):
        pair = (state, position)
        p = smap.get(pair)
        if p is None:
            p = result.new_state()
            smap[pair] = p
        return p
    
    # We organize the (virtual) states that need to be constructed in this todo list, and the fully expanded ones in the picked list.
    todo = [(system.get_init_state_number(),0)]
    picked = set()

    # Save length of trace prefix and loop
    len_prefix = len(trace.prefix)
    len_loop = len(trace.cycle)

    # Function for mapping a trace index to a concrete index in the (prefix,loop) tuple.
    def tr(i):
        if i < len_prefix:
            return trace.prefix[i]
        elif i < len_prefix + len_loop:
            return trace.cycle[i - len_prefix]
        else:
            raise Exception("Accessing trace out of bounds.")
        
    # Separate APs into inputs and outputs
    outputs = spot.get_synthesis_output_aps(system)
    inputs = set(str(a) for a in system.ap()).difference(set(str(p) for p in outputs))

    # construct preconditions for contingency edge construction
    pre = {}
    # We construct the precondition of each state, projected to the output variables (this will later decide whether a contingency edge to this state exists).
    for e in system.edges():
        cond = e.cond
        for i in inputs:
            ibdd = buddy.bdd_ithvar(result.register_ap(i))
            buddy.bdd_exist(cond,ibdd)
        if pre.get(e.dst) is None:
            pre[e.dst] = cond
        else:
            pre[e.dst] = pre[e.dst] | cond 

    # Main loop iterates over the virtual states of the integral automaton.
    for (q,n) in todo:

        # It may happen that a state is added to the todo list several times, so we first check whether it was picked before.
        if (q,n) in picked:
            continue

        # We remember that we picked (q,n) and will subsequently fully expand it.
        picked.add((q,n))

        # We move to the next implicit copy of the system, except at the end of the loop, where we return to the loop entry point.
        next = n + 1 if n + 1 < len_prefix + len_loop else len_prefix


        # We proceed to construct the edges that go from the virtual state to the next copy in the integral automaton.
        for edge in system.out(q):

            # Build the guard for the inputs.
            res_guard_in = project_away(system,edge.cond,outputs)
            
            # The output constraints depend on whether contingencies are enabled, but are always based on the original guard.
            guard_out = project_away(system,edge.cond,inputs)

            # Build the guard for the outputs+contingencies by constructing th latter and possibly adding new edges to states if their precondition allows.
            for (state,precon) in pre.items():
                res_guard_out = buddy.bddfalse
                for subset in powerset(list(outputs)):
                    guard_aux = project_away(system,guard_out,subset)
                    for o in subset:
                        oorig = buddy.bdd_ithvar(result.register_ap(o))
                        if buddy.bdd_and(tr(n),oorig) != buddy.bddfalse:
                            guard_aux = buddy.bdd_and(guard_aux,oorig)
                        else:
                            guard_aux = buddy.bdd_and(guard_aux,buddy.bdd_not(oorig))
                    
                    if buddy.bdd_and(precon,guard_aux) != buddy.bddfalse:
                        res_guard_out = buddy.bdd_or(res_guard_out,guard_aux)

                if res_guard_out != buddy.bddfalse:
                    result.new_edge(lookup(q,n),lookup(state,next),buddy.bdd_and(res_guard_in,res_guard_out))
                # Target state comes into the todo list if not already expanded.
                if not (state,next) in picked:
                    todo.append((edge.dst,next))

    return result

def project_away(automaton,bdd,aps):
    '''
    Transforms the given bdd to its existential projection w.r.t. the list of APs. 
    ''' 
    result = bdd
    for a in aps:
        abdd = buddy.bdd_ithvar(automaton.register_ap(a))
        result = buddy.bdd_exist(result,abdd)

    return result

def project_existentially(automaton,aps):
    '''
    Transforms the given automaton to its existential projection w.r.t. the list of APs. 
    If it is not given as an NBA with state-based acceptance, it will be transformed to such first.
    ''' 

    # Translate to state-based Büchi acceptance if necessary.
    if not automaton.is_sba():
        automaton = spot.postprocess(automaton,'buchi','state-based')

    # Apply the existenial projection to all guards on all edges.
    for e in automaton.edges():
        e.cond = project_away(automaton,e.cond,aps)

    # The automaton may lose properties in the process, so we reset all.
    automaton.prop_reset()
    # Remove the APs that were projected away.
    automaton.remove_unused_ap()

    return automaton

def add_suffix(automaton,suffix):
    '''
    Adds a suffix to all atomic propositions of the automaton.
    '''
    return spot.automaton(automaton.to_str().replace('\" ', suffix + '\"').replace('\"\n', suffix + '\"'))

def remove_suffix(automaton,suffix):
    '''
    Removes a suffix from all atomic propositions of the automaton.
    '''
    return spot.automaton(automaton.to_str().replace(suffix + '\"','\"'))
