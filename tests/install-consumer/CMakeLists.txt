cmake_minimum_required(VERSION 3.19)
project(backend_ndl_install_consumer LANGUAGES C)

find_package(backend_ndl CONFIG REQUIRED)

add_executable(backend_ndl_install_consumer main.c)
target_link_libraries(backend_ndl_install_consumer PRIVATE
  backend_ndl::backend_ndl
)

if(backend_ndl_WITH_GSTREAMER)
  target_compile_definitions(backend_ndl_install_consumer PRIVATE TEST_WITH_GSTREAMER=1)
endif()

enable_testing()
add_test(NAME backend-ndl-install-consumer-run
  COMMAND backend_ndl_install_consumer
)
