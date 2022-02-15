#!/bin/bash

set -e
set -x

podman pod rm -f compat
podman rmi -f smoketest/newbase:1
podman rmi -f smoketest/oldbase:1
podman rmi -f smoketest/interiornew:1
podman rmi -f smoketest/interiorold:1
podman rmi -f smoketest/edgenew:1
podman rmi -f smoketest/edgeold:1

set +x
