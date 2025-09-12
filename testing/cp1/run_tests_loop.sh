#!/bin/bash

tests=(
    "./bin/cp1/testcase_line_easy -a"
    "./bin/cp1/testcase_line_hard -a"
    "./bin/cp1/testcase_tree_easy -a"
    "./bin/cp1/testcase_tree_hard -a"
    "./bin/cp1/testcase_ring_easy -a"
    "./bin/cp1/testcase_ring_hard -a"
    "./bin/cp1/testcase_full_mesh_easy -a"
    "./bin/cp1/testcase_full_mesh_hard -a"
    "./bin/cp1/testcase_tiebreak_pathlen -a"
    "./bin/cp1/testcase_tiebreak_parent_ring -a"
    "./bin/cp1/testcase_tiebreak_parent_mesh -a"
    "./bin/cp1/testcase_tiebreak_multi -a"
    "./bin/cp1/testcase_unreachable -a"
    "./bin/cp1/testcase_link_failure_root -a"
    "./bin/cp1/testcase_link_failure_ring -a"
    "./bin/cp1/testcase_link_failure_mesh -a"
)

iteration=1

while true; do
    echo "===== Iteration $iteration ====="

    for test in "${tests[@]}"; do
        echo "Running $test..."
        output=$($test)
        echo "$output"
        echo
        if echo "$output" | grep -q "FAIL"; then
            echo "FAIL detected in '$test' (Iteration $iteration). Stopping."
            exit 1
        fi
    done

    iteration=$((iteration + 1))
done

