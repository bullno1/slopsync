add_executable(slopsync-tests
	"main.c"
	"shared.c"
	"serialization.c"
)
target_link_libraries(slopsync-tests PRIVATE blibs slopsync-client)
