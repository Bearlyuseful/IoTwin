add_library(common STATIC log.c)
target_include_directories(common PUBLIC ${PROJECT_SOURCE_DIR}/include)