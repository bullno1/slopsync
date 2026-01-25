add_executable(slopsync-tests
	"main.c"
	"shared.c"
	"object.c"
	"world.c"
)
target_link_libraries(slopsync-tests PRIVATE blibs slopsync-client slopsync-server)
