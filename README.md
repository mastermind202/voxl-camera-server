# RB5 Camera Server

## Build Instructions

1. Requires the voxl-cross>=v1.9 docker image (found [here](https://developer.modalai.com/asset))
    * (PC) cd [Path To]/qrb5165-camera-server
    * (PC) voxl-docker -i voxl-cross
2. Build project binary:
    * (voxl-cross) ./install_build_deps.sh qrb5165 [repo]
    * (voxl-cross) ./clean.sh
    * (voxl-cross) ./build.sh
    * (voxl-cross) ./make_package.sh

## Installing and running on VOXL

* (PC) ./deploy_to_voxl.sh
* (RB5) voxl-configure-cameras
* (RB5) systemctl start voxl-camera-server

### Testing

You can test with the tracking camera running the following command on VOXL:

```
$ voxl-portal
```

And then pulling up the drone's IP address on a mobile or desktop device connected to the same network can pull up a web view of any of the cameras.

#### ModalAI Auto-Exposure

ModalAI Cameras use our internal auto-exposure algorithm using histograms. The code for the auto-exposure algorithm can be found [here](https://gitlab.com/voxl-public/voxl-sdk/core-libs/libmodal-exposure)

Questions
=========
* For any questions/comments please direct them to our [forum](https://forum.modalai.com/category/20/modal-pipe-architecture-mpa)
