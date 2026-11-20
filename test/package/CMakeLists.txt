# A program outside libturborel's tree, as its users write one: it finds
# the installed package and links turborel::turborel (RULES.md D13).
cmake_minimum_required(VERSION 3.25)
project(turborel_consumer LANGUAGES C)

find_package(turborel REQUIRED)

add_executable(consumer consumer.c)
target_link_libraries(consumer PRIVATE turborel::turborel)
set_target_properties(consumer PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

enable_testing()
add_test(NAME consumer COMMAND consumer)
