#!/bin/bash
#
set -e
usage="Usage: $0 [-p <proton-branch>] [-d <dispatch-branch>] [-u <git-url>]"
usage+=" Example url for branches:"
usage+=" -d DISPATCH-2305 -u https://github.com/kgiusti/dispatch.git"

while getopts "p:d:P:D:u:U:c:" opt; do
    case $opt in
        p)
            proton_branch=$OPTARG
            ;;
        d)
            dispatch_branch=$OPTARG
            ;;
        u)
            dispatch_url=$OPTARG
            ;;
        ?)
            echo $usage
            exit 1
            ;;
    esac
done

proton_branch=${proton_branch:-"main"}
dispatch_branch=${dispatch_branch:-"main"}
dispatch_url=${dispatch_url:-"https://gitbox.apache.org/repos/asf/qpid-dispatch.git"}

set -x

podman build --tag smoketest/newbase:1 \
       --file ../../containers/Container-custom \
       --build-arg proton_branch=$proton_branch \
       --build-arg dispatch_branch=$dispatch_branch \
       --build-arg dispatch_url=$dispatch_url .

podman build --tag smoketest/oldbase:1 \
       --file ../../containers/Container-fedora .

##

podman build --tag smoketest/edgeold:1 \
       --file ../../containers/Container-config \
       --build-arg base_image=smoketest/oldbase:1 \
       --build-arg config_file=config/EdgeOld.conf .

podman build --tag smoketest/interiorold:1 \
       --file ../../containers/Container-config \
       --build-arg base_image=smoketest/oldbase:1 \
       --build-arg config_file=config/InteriorOld.conf .

podman build --tag smoketest/edgenew:1 \
       --file ../../containers/Container-config \
       --build-arg base_image=smoketest/newbase:1 \
       --build-arg config_file=config/EdgeNew.conf .

podman build --tag smoketest/interiornew:1 \
       --file ../../containers/Container-config \
       --build-arg base_image=smoketest/newbase:1 \
       --build-arg config_file=config/InteriorNew.conf .

##

# use port 10000 to snoop inter-router messages
# use port 20000 and 20001 to snoop edge messages
podman pod create --name compat \
       -p 10000:10000 \
       -p 20000:20000 \
       -p 20001:20001 \
       -p 45672:45672 \
       -p 35672:35672 \
       -p 25672:25672 \
       -p 15672:15672

podman run -d --rm --pod compat --name EdgeOld \
       smoketest/edgeold:1
podman run -d --rm --pod compat --name InteriorNew \
       smoketest/interiornew:1
podman run -d --rm --pod compat --name InteriorOld \
       smoketest/interiorold:1
podman run -d --rm --pod compat --name EdgeNew \
       smoketest/edgenew:1
set +x

