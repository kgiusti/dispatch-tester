#!/bin/bash

# set -x
podman stop smoketest-broker
podman rm   smoketest-broker
podman rmi  smoketest/broker:1

podman stop smoketest-routerA
podman rm   smoketest-routerA
podman rmi  smoketest/routera:1

podman stop smoketest-routerB
podman rm   smoketest-routerB
podman rmi  smoketest/routerb:1

