'''
This module contains the algorithms for cause synthesis.
'''

import spot
import auto
 
def synthesize(system,trace,effect_automaton_neg,limit_assumption,contingencies):
    '''
    Given a system as a automaton, a set of inputs (from the atomic propositions), a trace in lasso-shape, 
    and an effect as an automaton this function synthesizes the cause property and returns it as a Büchi automaton.
    ''' 

    outputs = spot.get_synthesis_output_aps(system)
    inputs = set(str(a) for a in system.ap()).difference(set(str(p) for p in outputs))

    if contingencies:
        system = auto.construct_counterfactual_automaton(system,trace)

    # Construct distance metric.
    distance_metric = "G (True"
    for i in inputs:
        distance_metric += " & (!(" + i + "_actual <-> " + i + "_close) -> !(" + i + "_actual <-> " + i + "_far))"
        if limit_assumption:
            distance_metric = "((G F !(" + i + "_actual <-> " + i + "_close)) -> (G (" + i + "_close <-> " + i + "_far))) &" + distance_metric
    distance_metric += ")"
    distance_automaton = spot.translate(spot.formula(distance_metric))

    # Add suffixes to effect.
    effect_automaton_neg = auto.add_suffix(effect_automaton_neg,"_close")

    # Intersect effect and distance metric.
    intersection = spot.product(distance_automaton,effect_automaton_neg)

    # Construct NBA for inner product.
    inner_product = spot.postprocess(spot.product(auto.add_suffix(system,"_close"),intersection),'buchi','state-based','low')

    # Project close APs existentially.
    after_projection = auto.project_existentially(inner_product,[str(a) + "_close" for a in system.ap()])

    # Central complementation.
    intermediate_result = spot.complement(after_projection)

    # Intersect with actual trace and project away APs
    actual_trace = auto.add_suffix(trace.as_automaton(),"_actual")
    actual_result = spot.product(intermediate_result,actual_trace)
    actual_projection = auto.project_existentially(actual_result,[str(a) + "_actual" for a in system.ap()])

    intermediate_result = spot.postprocess(actual_projection,'buchi','state-based','small','high')

    # Map APs back to inputs by removing the dummy suffix.
    return auto.remove_suffix(intermediate_result,"_far")