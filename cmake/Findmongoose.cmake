include(FetchContent)
FetchContent_Declare(
	mongoose
	GIT_REPOSITORY https://github.com/cesanta/mongoose.git
	GIT_TAG        236724ea2948396b4f83b68c366a169e795aa7ef # 7.20
)
FetchContent_MakeAvailable(mongoose)
set(MONGOOSE_DIR ${FETCHCONTENT_BASE_DIR}/mongoose-src/)
if(NOT TARGET mongoose)
	add_library(mongoose STATIC ${MONGOOSE_DIR}/mongoose.h ${MONGOOSE_DIR}/mongoose.c)
	target_include_directories(mongoose PUBLIC ${MONGOOSE_DIR})
endif()
