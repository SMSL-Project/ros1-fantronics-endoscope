# ros1_fantronics_endoscope

A ROS Noetic node for the Geek szitman "supercamera" endoscope, delivering a live image stream. Supports multiple endoscopes simultaneously.

## 🆕 Standalone Applications (No ROS Required)

This repository now includes **standalone applications** that can stream endoscope images via **TCP/IP or UDP** without requiring ROS!

- **endoscope_tcp_server**: Stream images over TCP/IP
- **endoscope_udp_server**: Stream images over UDP
- **tcp_client**: Example client to receive streams

Perfect for embedded systems, edge devices, or any application where ROS is not available.

**📚 Documentation:**
- [QUICKSTART.md](QUICKSTART.md) - Get started in 5 minutes
- [STANDALONE_README.md](STANDALONE_README.md) - Complete documentation
- [examples/README.md](examples/README.md) - Client examples in multiple languages

## Prerequisite

First install `libusb-1.0-0-dev` and `pkg-config` if you haven't done so

```bash
sudo apt-get update
sudo apt-get install -y \
    libusb-1.0-0-dev \
    pkg-config \
    ros-noetic-compressed-image-transport
```

Create the USB rule file so that the USB port can by default be accessed by non-root user

```bash
echo 'SUBSYSTEMS=="usb", ENV{DEVTYPE}=="usb_device", ATTRS{idVendor}=="2ce3", ATTRS{idProduct}=="3828", MODE="0666"' | sudo tee /etc/udev/rules.d/99-supercamera.rules
```

Then reload and trigger the rules

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

**[Important] Re-plug the device**: Disconnect and reconnect the USB cable so that udev applies the new permissions.

To verify the permissions, check it with
```bash
$ lsusb | grep 2ce3:3828
# Expected -> Bus 001 Device 009: ID 2ce3:3828
$ ls -l /dev/bus/usb/001/009 # 001 is the BUS ID, 009 is the Device ID, adjust it to your case
# Expected -> crw-rw-rw- 1 root root 189, 8 Apr 30 21:58 /dev/bus/usb/001/009
```

## Build and Run

Once you finished all the steps, clone this repo to ${YOUR WORKSPACE}/src by:
```bash
git clone git@github.com:SMSL-Project/ros1_fantronics_endoscope.git
```

Build it with `catkin build`, source the workspace.

### Auto-detection Mode (Recommended)

Simply run the node without parameters to auto-detect all connected endoscopes:

```bash
rosrun ros1_fantronics_endoscope endocam
```

The node will automatically detect all connected endoscopes and start publishing images on separate topics:
- `/supercamera/camera0/image_raw`
- `/supercamera/camera1/image_raw`
- ...etc.

### Manual Configuration Mode

If you want more control over the specific cameras, you can use the following parameters:

```bash
rosrun ros1_fantronics_endoscope endocam _num_cameras:=2 _camera0/bus:=1 _camera0/device:=9 _camera0/name:=left_camera _camera1/bus:=1 _camera1/device:=10 _camera1/name:=right_camera
```

This will initialize two cameras with custom names and specific USB bus/device addresses. The images will be published on:
- `/supercamera/left_camera/image_raw`
- `/supercamera/right_camera/image_raw`

### Compressed Image Streaming

For better network performance, especially when streaming over LAN, you can use compressed image streaming. This significantly reduces bandwidth usage while maintaining acceptable image quality.

To use compressed streaming:

1. Launch with the compressed configuration:
```bash
roslaunch ros1_fantronics_endoscope endocam_compressed.launch
```

The images will be published on compressed topics:
- `/supercamera/cam1/image_raw/compressed`
- `/supercamera/cam2/image_raw/compressed`

2. On the receiving PC, you can subscribe to the compressed topics and republish them as raw images if needed:
```bash
rosrun image_transport republish compressed in:=/supercamera/cam1/image_raw/compressed raw out:=/supercamera/cam1/image_raw
```

You can adjust the JPEG compression quality (1-100) in the launch file. Higher values give better quality but larger size:
- 80 (default): Good balance between quality and size
- 90+: Higher quality, larger size
- 60-: Lower quality, smaller size

## Identifying USB Device Information

To determine the USB bus number and device address for your endoscopes, you can use:

```bash
lsusb | grep 2ce3:3828
```

This will output something like:
```
Bus 001 Device 009: ID 2ce3:3828 ...
Bus 001 Device 010: ID 2ce3:3828 ...
```

Use these bus and device numbers in your launch configuration.

## Launch File Examples

### Basic Launch File
```xml
<launch>
  <node name="endocam" pkg="ros1_fantronics_endoscope" type="endocam" output="screen">
    <param name="num_cameras" value="2"/>
    <param name="camera0/name" value="left_camera"/>
    <param name="camera0/bus" value="1"/>
    <param name="camera0/device" value="9"/>
    <param name="camera1/name" value="right_camera"/>
    <param name="camera1/bus" value="1"/>
    <param name="camera1/device" value="10"/>
  </node>
</launch>
```

### Compressed Streaming Launch File
```xml
<launch>
    <!-- Camera 1 -->
    <node pkg="ros1_fantronics_endoscope" type="endocam_node" name="camera1" output="screen">
        <param name="camera_name" value="cam1"/>
        <param name="bus" value="1"/>
        <param name="device" value="2"/>
        <param name="use_compressed" value="true"/>
        <param name="jpeg_quality" value="80"/>
    </node>

    <!-- Camera 2 -->
    <node pkg="ros1_fantronics_endoscope" type="endocam_node" name="camera2" output="screen">
        <param name="camera_name" value="cam2"/>
        <param name="bus" value="1"/>
        <param name="device" value="3"/>
        <param name="use_compressed" value="true"/>
        <param name="jpeg_quality" value="80"/>
    </node>
</launch>
```