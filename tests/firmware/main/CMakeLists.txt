idf_component_register(SRCS "tests.cpp" INCLUDE_DIRS "." REQUIRES quiz_core unity)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
