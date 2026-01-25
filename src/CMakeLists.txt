add_library(slopsync-shared STATIC "shared.c")
target_link_libraries(slopsync-shared PRIVATE blibs)
target_include_directories(slopsync-shared PUBLIC "../include")

add_library(slopsync-client STATIC "client.c")
target_link_libraries(slopsync-client PRIVATE blibs slopsync-shared $<$<PLATFORM_ID:Linux>:m>)
target_include_directories(slopsync-client PUBLIC "../include")

add_library(slopsync-server STATIC "server.c")
target_link_libraries(slopsync-server PRIVATE blibs slopsync-shared)
target_include_directories(slopsync-server PUBLIC "../include")

add_library(slopsync-static-schema STATIC "schema.c")
target_link_libraries(slopsync-static-schema PRIVATE blibs slopsync-client)
target_include_directories(slopsync-static-schema PUBLIC "../include")

if (NOT EMSCRIPTEN)
	add_library(slopsyncd SHARED "slopsyncd.c")
	target_link_libraries(slopsyncd PRIVATE blibs slopsync-server)
	set_target_properties(slopsyncd PROPERTIES
		C_VISIBILITY_PRESET hidden
		PREFIX ""
	)
	if (NOT MSVC)
		target_link_options(slopsyncd PRIVATE
			-Wl,--exclude-libs,ALL
			-Wl,--no-whole-archive
			#-Wl,--no-undefined
		)
	endif ()
endif ()
