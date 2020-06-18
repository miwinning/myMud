###############################################################################

cmake_minimum_required(VERSION 3.5)
project(myMud)

file(GLOB_RECURSE sources src/*.c src/*.h)

###############################################################################
## target definitions #########################################################
###############################################################################

# add the data to the target, so it becomes visible in some IDE
add_executable(myMud ${sources} )

# just for example add some compiler flags
target_compile_options(myMud PUBLIC -O2 -g -Wall )

target_link_libraries(myMud PUBLIC
  -lcrypt
)
