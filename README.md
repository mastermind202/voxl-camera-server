# RB5 Camera Server

## Build Instructions

1. Requires the rb5-flight-emulator (found [here](https://developer.modalai.com/asset)) to run docker ARM image
    * (PC) cd [Path To]/voxl-camera-server
    * (PC) rb5-flight-docker
2. Build project binary:
    * (RB5-FLIGHT-EMULATOR) ./install_build_deps.sh
    * (RB5-FLIGHT-EMULATOR) ./clean.sh
    * (RB5-FLIGHT-EMULATOR) ./build.sh
    * (RB5-FLIGHT-EMULATOR) ./make_package.sh

## Installing and running on VOXL

* (PC) ./install_on_voxl.sh
* (RB5) rb5-configure-cameras
* (RB5) systemctl start rb5-camera-server

### Testing

You can test with the tracking camera running the following command on VOXL:

```
$ voxl-portal
```

And then pulling up the drone's IP address on a mobile or desktop device connected to the same network can pull up a web view of any of the cameras.

#### ModalAI Auto-Exposure

ModalAI Cameras use our internal auto-exposure algorithm using histograms. The code for the auto-exposure algorithm can be found [here](https://gitlab.com/voxl-public/modal-pipe-architecture/voxl-mpa-exposure)

Questions
=========
* For any questions/comments please direct them to our [forum](https://forum.modalai.com/category/20/modal-pipe-architecture-mpa)
