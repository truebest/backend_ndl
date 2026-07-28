cmake_minimum_required(VERSION 3.19)
project(backend_ndl_install_consumer LANGUAGES C)

find_package(backend_ndl CONFIG REQUIRED)

add_executable(backend_ndl_install_consumer main.c)
target_link_libraries(backend_ndl_install_consumer PRIVATE
  backend_ndl::backend_ndl
)

enable_testing()
add_test(NAME backend-ndl-install-consumer-run
  COMMAND backend_ndl_install_consumer
)
