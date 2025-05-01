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

add_executable(endocam src/endocam_node.cpp)


target_link_libraries(endocam
  ${catkin_LIBRARIES}
  ${OpenCV_LIBRARIES}
  ${LIBUSB_LIBRARIES}
)

