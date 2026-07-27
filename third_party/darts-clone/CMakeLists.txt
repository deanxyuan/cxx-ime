# Upstream: https://github.com/s-yata/darts-clone
# Release: v0.32h
# Commit: 87b71afd6cf784953e3c08f24c64203397f3b724
# DARTS_VERSION: 0.32
# darts.h SHA-256: acfb9cb7522d4c6a6f6099b981f7f12edb17a9b77fe33b3d38da9c20cd77301a
project(darts_clone NONE)

add_library(darts_clone INTERFACE)
target_include_directories(darts_clone INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
