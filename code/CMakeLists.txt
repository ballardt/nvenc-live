set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BUINARY_DIR})

add_executable(encode
    nvenc_encode.cpp
    stitch.cpp)

target_include_directories(encode
    PRIVATE
    ${LIBAV_INCLUDE_DIRS})

target_link_libraries(encode
    ${LIBAV_LIBRARIES})

