# CMake generated Testfile for 
# Source directory: /home/aprokurov/Projects/mimalloc
# Build directory: /home/aprokurov/Projects/mimalloc
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_api, "mimalloc-test-api")
set_tests_properties(test_api, PROPERTIES  _BACKTRACE_TRIPLES "/home/aprokurov/Projects/mimalloc/CMakeLists.txt;370;add_test;/home/aprokurov/Projects/mimalloc/CMakeLists.txt;0;")
add_test(test_stress, "mimalloc-test-stress")
set_tests_properties(test_stress, PROPERTIES  _BACKTRACE_TRIPLES "/home/aprokurov/Projects/mimalloc/CMakeLists.txt;371;add_test;/home/aprokurov/Projects/mimalloc/CMakeLists.txt;0;")
