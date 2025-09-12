#!/bin/bash
./bin/cp1/testcase_line_easy -a; echo; # PASS
./bin/cp1/testcase_line_hard -a; echo; # PASS
./bin/cp1/testcase_tree_easy -a; echo; # PASS
./bin/cp1/testcase_tree_hard -a; echo; # PASS
./bin/cp1/testcase_ring_easy -a; echo; # PASS
./bin/cp1/testcase_ring_hard -a; echo;
./bin/cp1/testcase_full_mesh_easy -a; echo; # PASS
./bin/cp1/testcase_full_mesh_hard -a; echo; # PASS
./bin/cp1/testcase_tiebreak_pathlen -a; echo; # PASS
./bin/cp1/testcase_tiebreak_parent_ring -a; echo; # PASS
./bin/cp1/testcase_tiebreak_parent_mesh -a; echo; # PASS
./bin/cp1/testcase_tiebreak_multi -a; echo; # PASS
./bin/cp1/testcase_link_failure_root -a; echo; # PASS
./bin/cp1/testcase_link_failure_ring -a; echo; # PASS
./bin/cp1/testcase_link_failure_mesh -a; echo;
./bin/cp1/testcase_unreachable -a; echo; # PASS