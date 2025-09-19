#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILENAME=$(echo "$1" | cut -d'.' -f1)
HOA_FILE="$FILENAME".hoa

make_spot_word() {
    local aps=("$@")   # APs passed as arguments
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
        for ap in "${aps[@]}"; do
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

echo "Running ltlsynt on $1 into $HOA_FILE"
ltlsynt --tlsf "$1" | tail -n +2 > "$HOA_FILE" # we need to remove the first line of the hoa file for hoax to run

# cat "$HOA_FILE"
APS="$(sed -n '4p' "$HOA_FILE" | grep -oE '"[^"]+"' | tr -d '"' | xargs)"

echo "Running hoax on $HOA_FILE"
hoax "$HOA_FILE" --config "$SCRIPT_DIR/random_generator.toml" | make_spot_word "$APS" > "$FILENAME".raw.hoa


# autfilt "$HOA_FILE" --accept-word=

