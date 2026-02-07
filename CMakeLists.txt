cmake_minimum_required(VERSION 3.0.2)
project(ros1_fantronics_endoscope)

find_package(catkin REQUIRED COMPONENTS
  cv_bridge
  image_transport
  roscpp
  sensor_msgs
)

find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

catkin_package(

)


include_directories(
# include
${catkin_INCLUDE_DIRS}
${OpenCV_INCLUDE_DIRS}
${LIBUSB_INCLUDE_DIRS}
)

link_directories(${LIBUSB_LIBRARY_DIRS})

add_executable(supercamera_node src/endocam_node.cpp)

target_link_libraries(supercamera_node
  ${catkin_LIBRARIES}
  ${OpenCV_LIBRARIES}
  ${LIBUSB_LIBRARIES}
)

# Standalone applications (without ROS dependencies)
add_executable(endoscope_tcp_server src/endoscope_tcp_server.cpp)
target_link_libraries(endoscope_tcp_server
  ${OpenCV_LIBRARIES}
  ${LIBUSB_LIBRARIES}
  pthread
)

add_executable(endoscope_udp_server src/endoscope_udp_server.cpp)
target_link_libraries(endoscope_udp_server
  ${OpenCV_LIBRARIES}
  ${LIBUSB_LIBRARIES}
  pthread
)

add_executable(tcp_client src/tcp_client.cpp)
target_link_libraries(tcp_client
  ${OpenCV_LIBRARIES}
  pthread
)

