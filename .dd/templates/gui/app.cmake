if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "GUI apps currently require Windows/MSVC.")
endif()
platform_add_app(app SOURCES src/main.cpp OUTPUT_NAME app)