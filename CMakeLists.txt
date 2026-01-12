add_library(slopsync-shared STATIC "src/shared.c")
target_link_libraries(slopsync-shared PRIVATE blibs)
target_include_directories(slopsync-shared PUBLIC include)

add_library(slopsync-client STATIC "src/client.c")
target_link_libraries(slopsync-client PRIVATE blibs slopsync-shared)
target_include_directories(slopsync-client PUBLIC include)
