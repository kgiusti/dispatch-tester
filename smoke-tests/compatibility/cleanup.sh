#!/bin/bash

# set -x
podman stop Edge1
podman stop Edge2
podman stop InteriorA
podman stop InteriorB

podman rm Edge1
podman rm Edge2
podman rm InteriorA
podman rm InteriorB

podman rmi dispatch-tester/edge1:1
podman rmi dispatch-tester/edge2:1
podman rmi dispatch-tester/interiora:1
podman rmi dispatch-tester/interiorb:1
