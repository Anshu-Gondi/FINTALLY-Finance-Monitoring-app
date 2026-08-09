# CMake generated Testfile for 
# Source directory: /home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine
# Build directory: /home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/build_release
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(EngineUnitTests "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/build_release/unit_tests")
set_tests_properties(EngineUnitTests PROPERTIES  _BACKTRACE_TRIPLES "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;136;add_test;/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
subdirs("_deps/googlebenchmark-build")
