# CMake generated Testfile for 
# Source directory: /home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine
# Build directory: /home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(EngineUnitTests "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/build/unit_tests")
set_tests_properties(EngineUnitTests PROPERTIES  _BACKTRACE_TRIPLES "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;515;add_test;/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;0;")
add_test(DatasetUnitTests "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/build/dataset_tests")
set_tests_properties(DatasetUnitTests PROPERTIES  WORKING_DIRECTORY "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine" _BACKTRACE_TRIPLES "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;542;add_test;/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;0;")
add_test(TensorOpsUnitTests "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/build/tensor_ops_tests")
set_tests_properties(TensorOpsUnitTests PROPERTIES  _BACKTRACE_TRIPLES "/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;576;add_test;/home/anshu_gondi/projects/FINTALLY-Finance-Monitoring-app/fintally_core/fintally_chatbot/cpp_engine/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
subdirs("_deps/googlebenchmark-build")
