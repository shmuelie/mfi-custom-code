include(FetchContent)
FetchContent_Declare(
	mongoose
	GIT_REPOSITORY https://github.com/cesanta/mongoose.git
	GIT_TAG        52997c6c5170050148d23436d2ed398811f48c30 # 7.13
)
set(MONGOOSE_DIR ${FETCHCONTENT_BASE_DIR}/mongoose-src/)
if(EXISTS ${MONGOOSE_DIR}/mongoose.h)
	add_library(mongoose STATIC ${MONGOOSE_DIR}/mongoose.h ${MONGOOSE_DIR}/mongoose.c)
	target_include_directories(mongoose PUBLIC ${MONGOOSE_DIR})
else()
	FetchContent_MakeAvailable(mongoose)
endif()
