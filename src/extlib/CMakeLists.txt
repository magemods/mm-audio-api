target_sources(${TARGET_NAME} PUBLIC
    "lib_main.cpp"
    "sqlite3.c"
)

add_subdirectory("./ach_data")
