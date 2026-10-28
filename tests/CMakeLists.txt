add_executable(rogue
main.cpp
termbox2.h
)

target_link_libraries(rogue PRIVATE FovLib)
