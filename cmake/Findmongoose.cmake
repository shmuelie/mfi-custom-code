include(FetchContent)
FetchContent_Declare(
	mongoose
	GIT_REPOSITORY https://github.com/cesanta/mongoose.git
	GIT_TAG        52997c6c5170050148d23436d2ed398811f48c30 # 7.13
)
FetchContent_MakeAvailable(mongoose)
set(MONGOOSE_DIR ${FETCHCONTENT_BASE_DIR}/mongoose-src/)
if(NOT TARGET mongoose)
	add_library(mongoose STATIC ${MONGOOSE_DIR}/mongoose.h ${MONGOOSE_DIR}/mongoose.c)
	target_include_directories(mongoose PUBLIC ${MONGOOSE_DIR})
endif()
