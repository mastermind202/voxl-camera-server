# voxl-camera-server

This document explains the internal details of the camera server and how clients interact with the server. The build steps and instructions to deploy it on VOXL are given at the end of the document.

## Build Instructions

NOTE: v0.5.0+ of camera server requires voxl system image 3.2+ to run and emulator build 1.5+ to build [(details)](https://docs.modalai.com/voxl-system-image/)

1. Requires the voxl-emulator (found [here](https://gitlab.com/voxl-public/voxl-docker)) to run docker ARM image
    * (PC) cd [Path To]/voxl-camera-server
    * (PC) sudo voxl-docker -i voxl-emulator
2. Build project binary:
    * (VOXL-EMULATOR) ./install_build_deps.sh
    * (VOXL-EMULATOR) ./clean.sh
    * (VOXL-EMULATOR) ./build.sh
    * (VOXL-EMULATOR) ./make_package.sh

## Installing and running on VOXL

* (PC) cd [Path To]/voxl-camera-server
* (PC) ./install_on_voxl.sh
* (VOXL) voxl-configure-camera-server -f hires stereo tracking

  __It is strongly recommended that you install the latest version of [voxl-utils](https://gitlab.com/voxl-public/utilities/voxl-utils) and use the voxl-configure-cameras script to set up the configuration file for this server instead of voxl-configure-camera-server. If you do not wish to install voxl-utils, you can run voxl-configure-camera-server to set up only this server, but this will not safety check supported voxl camera configurations and may yield unexpected results if invalid setups are provided__
* (VOXL) voxl-camera-server

### Testing

You can test with the tracking camera running the following command on VOXL:

```
$ voxl-streamer -c tracking
```

#### ModalAI Auto-Exposure

To test modalai auto-exposure algorithm, currently only supported on tracking and stereo cameras:

edit /etc/modalai/voxl-camera-server.conf for the "tracking" entry:

```
"auto_exposure_mode":   "modalai"
```

The code for the auto-exposure algorithm can be found [here](https://gitlab.com/voxl-public/modal-pipe-architecture/voxl-mpa-exposure)

Questions
=========
* For any questions/comments please direct them to our [forum](https://forum.modalai.com/category/20/modal-pipe-architecture-mpa)
