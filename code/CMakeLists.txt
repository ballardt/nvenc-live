set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BUINARY_DIR})

add_executable(encode
    nvenc_encode.c
    stitch.cpp)

target_link_libraries(encode
    ${LIBAV_LIBRARIES})

