add_executable(builder "code/analyzer.cpp" code/expression.cpp code/generator.cpp code/Machine.cpp code/main.cpp)
target_include_directories(builder PUBLIC code)