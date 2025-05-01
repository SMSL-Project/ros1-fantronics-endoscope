# ros1_fantronics_endoscope

A ROS Noetic node for the Geek szitman “supercamera” endoscope, delivering a live image stream on `/supercamera/image_raw`.


## Prerequisite

First install `libusb-1.0-0-dev` and `pkg-config` if you haven't done so

```bash
sudo apt-get update
sudo apt-get install -y \
    libusb-1.0-0-dev \
    pkg-config \
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

build it with `catkin build` and run it with
```bash
rosrun ros1_fantronics_endoscope endocam
```
you will find the images are streamed to rostopic `/supercamera/image_raw`
