# source this file to enable benchmark mode
#
export -n MALLOC_PERTURB_; export -n MALLOC_CHECK_
sudo cpupower -c all frequency-set -g performance
