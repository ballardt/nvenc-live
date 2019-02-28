set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BUINARY_DIR})

add_executable(encode
    	nvenc_encode.cpp
    	stitch.cpp
	context_group.cpp
	nvenc_config.cpp
	nvenc_hw.cpp)

target_include_directories(encode
    PRIVATE
    ${LIBAV_INCLUDE_DIRS})

target_link_libraries(encode
    ${LIBAV_LIBRARIES})

