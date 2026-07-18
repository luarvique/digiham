add_library(digiham_p25_decoder OBJECT p25_decoder.cpp p25_meta.cpp p25_phase.cpp nid.cpp link_control.cpp)
target_compile_options(digiham_p25_decoder PRIVATE "-fPIC")

add_executable(p25_decoder p25_cli.cpp)
target_link_libraries(p25_decoder digiham)
install(TARGETS p25_decoder DESTINATION ${CMAKE_INSTALL_BINDIR})
