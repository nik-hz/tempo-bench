#!/usr/bin/env bash
: '
Automata pipeline
Authors: Nikolaus Holzer, Will Fishell
Date: September 2025 

Pipeline to convert a tlsf input file into hoa and run clis to extract reasoning traces
'


generate_trace() {
    : '
    ########################################
    generate_trace
    Uses the APS array created below.
    ########################################
    '
    local trace=()
    local line raw present assignment

    # Read from stdin (the output of a command piped in)
    while IFS= read -r line; do
        # Extract inside { ... }
        raw=$(echo "$line" | sed -n 's/.*{\(.*\)}.*/\1/p')
        [[ -z "$raw" ]] && continue

        # Parse tokens inside {}
        present=()
        for tok in $(echo "$raw" | tr ',' '\n'); do
            tok=$(echo "$tok" | sed "s/['\"]//g" | xargs)
            [[ -n "$tok" ]] && present+=("$tok")
        done

        # Build assignment string
        assignment=()
        for ap in "${APS[@]}"; do
            if printf '%s\n' "${present[@]}" | grep -qx "$ap"; then
                assignment+=("$ap")
            else
                assignment+=("!$ap")
            fi
        done
        joined=$(echo "${assignment[*]}" | tr ' ' '&')
        trace+=("$joined")
    done

    # Join into Spot word
    echo "$(IFS=";"; echo "${trace[*]}");cycle{1}"
}


make_replacement() {
    : '
    ########################################
    make_replacement
    Generate the replacement string for the regex to be used in clean_rawhoa.
    ########################################
    '
    local parts=()
    for i in "${!APS[@]}"; do
        if [ "$i" == 0 ]; then
            parts+=("'!${APS[$i]}'")
        else
            parts+=(" '!${APS[$i]}'")
        fi
    done
    printf '{%s}' "$(IFS=", " ; echo "${parts[*]}")"
}


clean_rawhoa() {
    : '
    ########################################
    clean_rawhoa
    pipe in the output of hoax tool and replace set() with {!ap...}
    Takes the APs and constructs the regex replace to match
    ########################################
    '
    local pattern="set\(\)"
    local replacement
    replacement=$(make_replacement)

    sed -E "s/${pattern}/${replacement}/g"
}


: '
########################################
Environment variables
########################################
'
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILENAME=$(echo "$1" | cut -d'.' -f1)
TRACE_FILE="$FILENAME".trace
HOA_FILE="$FILENAME".hoa
HOAX_FILE="$FILENAME".hoax

echo "Running ltlsynt on $1 into $HOA_FILE"
ltlsynt --tlsf "$1" | tail -n +2 > "$HOA_FILE" # we need to remove the first line of the hoa file for hoax to run

# cat "$HOA_FILE"
mapfile -t APS < <(sed -n '4p' "$HOA_FILE" | grep -oE '"[^"]+"' | tr -d '"')

echo "Running hoax on $HOA_FILE"
hoax "$HOA_FILE" --config "$SCRIPT_DIR/random_generator.toml" | sed '$d' | clean_rawhoa > "$HOAX_FILE"

echo "Generating trace"
cat "$HOAX_FILE" | generate_trace > "$TRACE_FILE"

# show automata stats
cat "$HOA_FILE" | autfilt --stats='%s states, %e edges, %a acc-sets, %c SCCs, det=%d'

cat "$HOA_FILE" | autfilt --accept-word="$(<"$TRACE_FILE")" > "output/autfilt.out"

if [ "$?" != 0 ]
then 
    echo "Did not pass."
else 
    echo "Pass."
fi

# autfilt output
# effect.txt whatever the cause is affecting written as a temporal logic thing
#     G(g_0->cause)
# output