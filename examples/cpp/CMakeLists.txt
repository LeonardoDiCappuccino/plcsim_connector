add_executable(di_do_demo di_do_demo.cpp)
target_link_libraries(di_do_demo PRIVATE plcsim::core)
target_compile_features(di_do_demo PRIVATE cxx_std_20)
